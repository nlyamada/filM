#include "drag_drop.h"

#include <ole2.h>
#include <shlobj.h>

namespace {

// --- ドラッグ元: IDropSource ---------------------------------------------

class SimpleDropSource : public IDropSource {
public:
    virtual ~SimpleDropSource() = default;

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return ++refCount_; }
    ULONG __stdcall Release() override {
        ULONG r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    HRESULT __stdcall QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override {
        if (fEscapePressed) return DRAGDROP_S_CANCEL;
        if (!(grfKeyState & (MK_LBUTTON | MK_RBUTTON))) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT __stdcall GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

private:
    ULONG refCount_ = 1;
};

// filePaths から、シェル標準の SHCreateDataObject で IDataObject を作る。
// 自作の IDataObject は使わない (自前実装で発生したクラッシュを避けるため)。
// 項目は全て同じ親フォルダにあるものとして扱う (先頭項目の親を使う)。
IDataObject* CreateFilesDataObject(const std::vector<std::wstring>& filePaths) {
    if (filePaths.empty()) {
        return nullptr;
    }

    std::vector<PIDLIST_ABSOLUTE> fullPidls;
    for (const std::wstring& path : filePaths) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
            fullPidls.push_back(pidl);
        }
    }

    IDataObject* dataObject = nullptr;
    if (!fullPidls.empty()) {
        // SHCreateDataObject は「親フォルダの絶対PIDL」と「親から見た相対(子)PIDL」を要求する。
        PIDLIST_ABSOLUTE pidlParent = ILClone(fullPidls[0]);
        if (pidlParent) {
            ILRemoveLastID(pidlParent);
            std::vector<PCUITEMID_CHILD> children;
            for (PIDLIST_ABSOLUTE p : fullPidls) {
                children.push_back(ILFindLastID(p));
            }
            if (FAILED(SHCreateDataObject(pidlParent, static_cast<UINT>(children.size()),
                                           children.data(), nullptr, IID_PPV_ARGS(&dataObject)))) {
                dataObject = nullptr;
            }
            ILFree(pidlParent);
        }
    }
    for (PIDLIST_ABSOLUTE p : fullPidls) {
        CoTaskMemFree(p);
    }
    return dataObject;
}

UINT PreferredDropEffectFormat() {
    static UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    return format;
}

}  // namespace

DWORD BeginFileDrag(HWND hwndSource, const std::vector<std::wstring>& filePaths) {
    (void)hwndSource;

    DWORD effect = DROPEFFECT_NONE;
    IDataObject* dataObject = CreateFilesDataObject(filePaths);
    if (dataObject) {
        auto* dropSource = new SimpleDropSource();
        DoDragDrop(dataObject, dropSource, DROPEFFECT_COPY | DROPEFFECT_MOVE, &effect);
        dropSource->Release();
        dataObject->Release();
    }
    return effect;
}

DWORD BeginFileDrag(HWND hwndSource, const std::wstring& filePath) {
    return BeginFileDrag(hwndSource, std::vector<std::wstring>{filePath});
}

bool SetFilesToClipboard(const std::vector<std::wstring>& filePaths, bool cut) {
    IDataObject* dataObject = CreateFilesDataObject(filePaths);
    if (!dataObject) {
        return false;
    }

    // Explorer と同じく "Preferred DropEffect" で コピー/切り取り を伝える。
    HGLOBAL hEffect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (hEffect) {
        DWORD* p = static_cast<DWORD*>(GlobalLock(hEffect));
        *p = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
        GlobalUnlock(hEffect);

        FORMATETC fmt{};
        fmt.cfFormat = static_cast<CLIPFORMAT>(PreferredDropEffectFormat());
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        fmt.tymed = TYMED_HGLOBAL;
        STGMEDIUM medium{};
        medium.tymed = TYMED_HGLOBAL;
        medium.hGlobal = hEffect;
        if (FAILED(dataObject->SetData(&fmt, &medium, TRUE))) {
            GlobalFree(hEffect);  // 所有権が移っていない場合のみ解放する
        }
    }

    bool ok = SUCCEEDED(OleSetClipboard(dataObject));
    dataObject->Release();
    return ok;
}

bool GetFilesFromClipboard(std::vector<std::wstring>& filePaths, bool& isCut) {
    filePaths.clear();
    isCut = false;

    IDataObject* dataObject = nullptr;
    if (FAILED(OleGetClipboard(&dataObject)) || !dataObject) {
        return false;
    }

    FORMATETC fmtDrop{};
    fmtDrop.cfFormat = CF_HDROP;
    fmtDrop.dwAspect = DVASPECT_CONTENT;
    fmtDrop.lindex = -1;
    fmtDrop.tymed = TYMED_HGLOBAL;
    STGMEDIUM medium{};
    if (SUCCEEDED(dataObject->GetData(&fmtDrop, &medium))) {
        HDROP hDrop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
        if (hDrop) {
            UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
                std::wstring path(len, L'\0');
                DragQueryFileW(hDrop, i, path.data(), len + 1);
                filePaths.push_back(std::move(path));
            }
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
    }

    FORMATETC fmtEffect = fmtDrop;
    fmtEffect.cfFormat = static_cast<CLIPFORMAT>(PreferredDropEffectFormat());
    STGMEDIUM effectMedium{};
    if (SUCCEEDED(dataObject->GetData(&fmtEffect, &effectMedium))) {
        DWORD* p = static_cast<DWORD*>(GlobalLock(effectMedium.hGlobal));
        if (p) {
            isCut = (*p & DROPEFFECT_MOVE) != 0 && (*p & DROPEFFECT_COPY) == 0;
            GlobalUnlock(effectMedium.hGlobal);
        }
        ReleaseStgMedium(&effectMedium);
    }

    dataObject->Release();
    return !filePaths.empty();
}

void ClearFileClipboard() {
    OleSetClipboard(nullptr);
}

void FlushFileClipboard() {
    OleFlushClipboard();
}

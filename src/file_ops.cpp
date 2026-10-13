#include "file_ops.h"

#include <shobjidl.h>
#include <shlobj.h>

#include <algorithm>
#include <functional>

namespace {

// IFileOperation の結果 (実際に作成された新しいパス) を受け取るための進捗シンク。
// すべてのメソッドが S_OK を返すだけの単純な実装 (多メソッドの IDataObject とは違い、
// 状態を持つのは done だけ)。
class OpSink : public IFileOperationProgressSink {
public:
    virtual ~OpSink() = default;
    std::vector<OpPair> done;

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink) {
            *ppv = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = static_cast<ULONG>(InterlockedDecrement(&refs_));
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP StartOperations() override { return S_OK; }
    STDMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
    STDMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PostRenameItem(DWORD, IShellItem* item, LPCWSTR, HRESULT hr, IShellItem* created) override {
        Record(hr, item, created);
        return S_OK;
    }
    STDMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PostMoveItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT hr,
                              IShellItem* created) override {
        Record(hr, item, created);
        return S_OK;
    }
    STDMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PostCopyItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT hr,
                              IShellItem* created) override {
        Record(hr, item, created);
        return S_OK;
    }
    STDMETHODIMP PreDeleteItem(DWORD, IShellItem*) override { return S_OK; }
    STDMETHODIMP PostDeleteItem(DWORD, IShellItem* item, HRESULT hr, IShellItem* created) override {
        Record(hr, item, created);
        return S_OK;
    }
    STDMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override {
        return S_OK;
    }
    STDMETHODIMP UpdateProgress(UINT, UINT) override { return S_OK; }
    STDMETHODIMP ResetTimer() override { return S_OK; }
    STDMETHODIMP PauseTimer() override { return S_OK; }
    STDMETHODIMP ResumeTimer() override { return S_OK; }

private:
    static std::wstring PathOf(IShellItem* item) {
        std::wstring result;
        if (!item) return result;
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            result = path;
            CoTaskMemFree(path);
        }
        return result;
    }
    void Record(HRESULT hr, IShellItem* item, IShellItem* created) {
        if (FAILED(hr)) return;
        done.push_back(OpPair{PathOf(item), PathOf(created)});
    }

    LONG refs_ = 1;
};

IShellItem* CreateShellItem(const std::wstring& path) {
    if (path.empty()) return nullptr;  // 空文字列はデスクトップを指してしまうので拒否する
    IShellItem* item = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item));
    return SUCCEEDED(hr) ? item : nullptr;
}

// IFileOperation を作り、queue でキューに積み、実行する。結果を done に返す。
bool RunFileOperation(HWND hwndOwner, DWORD flags, std::vector<OpPair>* done,
                      const std::function<bool(IFileOperation*)>& queue) {
    IFileOperation* fileOp = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&fileOp)))) {
        return false;
    }
    OpSink* sink = new OpSink();
    DWORD cookie = 0;
    bool advised = SUCCEEDED(fileOp->Advise(sink, &cookie));

    bool ok = false;
    fileOp->SetOwnerWindow(hwndOwner);
    fileOp->SetOperationFlags(flags);
    if (queue(fileOp) && SUCCEEDED(fileOp->PerformOperations())) {
        BOOL aborted = FALSE;
        fileOp->GetAnyOperationsAborted(&aborted);
        ok = !aborted;
    }

    if (advised) fileOp->Unadvise(cookie);
    if (done) *done = sink->done;
    sink->Release();
    fileOp->Release();
    return ok;
}

// sources の各項目を destDir に対して op (CopyItem / MoveItem) する。
bool Transfer(HWND hwndOwner, const std::vector<std::wstring>& sources, const std::wstring& destDir,
              bool isMove, std::vector<OpPair>* done) {
    IShellItem* destItem = CreateShellItem(destDir);
    if (!destItem) {
        if (done) done->clear();
        return false;
    }
    bool ok = RunFileOperation(
        hwndOwner, FOF_ALLOWUNDO | FOFX_ADDUNDORECORD, done, [&](IFileOperation* fileOp) {
            bool any = false;
            for (const std::wstring& src : sources) {
                IShellItem* item = src.empty() ? nullptr : CreateShellItem(src);
                if (!item) continue;
                HRESULT hr = isMove ? fileOp->MoveItem(item, destItem, nullptr, nullptr)
                                    : fileOp->CopyItem(item, destItem, nullptr, nullptr);
                item->Release();
                any |= SUCCEEDED(hr);
            }
            return any;
        });
    destItem->Release();
    return ok;
}

// ごみ箱の項目から取り出す文字列・日時のプロパティキー (System.Recycle.*)。
const PROPERTYKEY kDeletedFrom = {{0x9B174B33, 0x40FF, 0x11D2, {0xA2, 0x7E, 0x00, 0xC0, 0x4F, 0xC3, 0x08, 0x71}}, 2};
const PROPERTYKEY kDateDeleted = {{0x9B174B33, 0x40FF, 0x11D2, {0xA2, 0x7E, 0x00, 0xC0, 0x4F, 0xC3, 0x08, 0x71}}, 3};

bool EqualNoCase(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() &&
           CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()),
                                TRUE) == CSTR_EQUAL;
}

}  // namespace

bool CopyItems(HWND hwndOwner, const std::vector<std::wstring>& sources, const std::wstring& destDir,
               std::vector<OpPair>* done) {
    return Transfer(hwndOwner, sources, destDir, false, done);
}

bool MoveItems(HWND hwndOwner, const std::vector<std::wstring>& sources, const std::wstring& destDir,
               std::vector<OpPair>* done) {
    return Transfer(hwndOwner, sources, destDir, true, done);
}

bool DeleteItems(HWND hwndOwner, const std::vector<std::wstring>& sources, bool permanent,
                 std::vector<OpPair>* done) {
    DWORD flags = permanent ? FOF_NOCONFIRMATION : (FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOFX_ADDUNDORECORD);
    return RunFileOperation(hwndOwner, flags, done, [&](IFileOperation* fileOp) {
        bool any = false;
        for (const std::wstring& src : sources) {
            IShellItem* item = src.empty() ? nullptr : CreateShellItem(src);
            if (!item) continue;
            any |= SUCCEEDED(fileOp->DeleteItem(item, nullptr));
            item->Release();
        }
        return any;
    });
}

bool RenameItem(HWND hwndOwner, const std::wstring& fullPath, const std::wstring& newName,
                std::wstring* newPath) {
    std::vector<OpPair> done;
    bool ok = RunFileOperation(hwndOwner, FOF_ALLOWUNDO | FOFX_ADDUNDORECORD, &done,
                               [&](IFileOperation* fileOp) {
                                   IShellItem* item = CreateShellItem(fullPath);
                                   if (!item) return false;
                                   HRESULT hr = fileOp->RenameItem(item, newName.c_str(), nullptr);
                                   item->Release();
                                   return SUCCEEDED(hr);
                               });
    if (newPath) *newPath = done.empty() ? L"" : done[0].to;
    return ok;
}

bool DuplicateItem(HWND hwndOwner, const std::wstring& fullPath, const std::wstring& newName,
                   std::wstring* newPath) {
    size_t sep = fullPath.find_last_of(L'\\');
    if (sep == std::wstring::npos) return false;
    std::wstring parent = fullPath.substr(0, sep == 2 && fullPath[1] == L':' ? 3 : sep);

    IShellItem* destItem = CreateShellItem(parent);
    if (!destItem) return false;
    std::vector<OpPair> done;
    bool ok = RunFileOperation(hwndOwner, FOF_ALLOWUNDO | FOFX_ADDUNDORECORD, &done,
                               [&](IFileOperation* fileOp) {
                                   IShellItem* item = CreateShellItem(fullPath);
                                   if (!item) return false;
                                   HRESULT hr = fileOp->CopyItem(item, destItem, newName.c_str(), nullptr);
                                   item->Release();
                                   return SUCCEEDED(hr);
                               });
    destItem->Release();
    if (newPath) *newPath = done.empty() ? L"" : done[0].to;
    return ok;
}

bool RestoreFromRecycleBin(HWND hwndOwner, const std::wstring& originalPath, const std::wstring& binPath) {
    size_t sep = originalPath.find_last_of(L'\\');
    if (sep == std::wstring::npos) return false;
    std::wstring folder = originalPath.substr(0, sep == 2 && originalPath[1] == L':' ? 3 : sep);
    while (folder.size() > 3 && folder.back() == L'\\') folder.pop_back();
    std::wstring fileName = originalPath.substr(sep + 1);
    // 拡張子を非表示にしている場合、ごみ箱内の表示名 (SIGDN_NORMALDISPLAY) は拡張子の無い
    // 元のフルパスになる。
    size_t dot = fileName.find_last_of(L'.');
    std::wstring pathNoExt = dot != std::wstring::npos && dot > 0
                                 ? originalPath.substr(0, sep + 1 + dot) : originalPath;
    std::wstring binName;
    if (!binPath.empty()) {
        size_t bsep = binPath.find_last_of(L'\\');
        binName = bsep == std::wstring::npos ? binPath : binPath.substr(bsep + 1);
    }

    IShellItem* bin = nullptr;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_RecycleBinFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&bin)))) {
        return false;
    }

    // ごみ箱の項目を列挙して探す。exactOnly が true なら、ごみ箱側の名前 ($Rxxxxxx.ext) の一致だけで
    // 判定する (項目が多いごみ箱でも速い)。false なら削除前のパスと削除元フォルダから探す。
    auto find = [&](bool exactOnly) -> IShellItem* {
        IEnumShellItems* items = nullptr;
        if (FAILED(bin->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items)))) {
            return nullptr;
        }
        IShellItem* best = nullptr;
        FILETIME bestTime{};
        IShellItem* child = nullptr;
        ULONG fetched = 0;
        while (items->Next(1, &child, &fetched) == S_OK && fetched == 1) {
            bool match = false;
            FILETIME deletedAt{};

            PWSTR parsing = nullptr;
            if (!binName.empty() && SUCCEEDED(child->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &parsing))) {
                match = EqualNoCase(parsing, binName);
            }
            if (parsing) CoTaskMemFree(parsing);

            if (match && exactOnly) {
                best = child;  // 名前が一致すれば確定 (同じ名前は2つと無い)
                break;
            }
            if (!match && !exactOnly) {
                IShellItem2* item2 = nullptr;
                if (SUCCEEDED(child->QueryInterface(IID_PPV_ARGS(&item2)))) {
                    PWSTR from = nullptr;
                    PWSTR display = nullptr;
                    if (SUCCEEDED(item2->GetString(kDeletedFrom, &from)) &&
                        SUCCEEDED(child->GetDisplayName(SIGDN_NORMALDISPLAY, &display))) {
                        std::wstring deletedFrom = from;
                        while (deletedFrom.size() > 3 && deletedFrom.back() == L'\\') deletedFrom.pop_back();
                        match = EqualNoCase(deletedFrom, folder) &&
                                (EqualNoCase(display, originalPath) || EqualNoCase(display, pathNoExt) ||
                                 EqualNoCase(display, fileName));
                    }
                    if (from) CoTaskMemFree(from);
                    if (display) CoTaskMemFree(display);
                    if (match) item2->GetFileTime(kDateDeleted, &deletedAt);
                    item2->Release();
                }
            }
            if (match && (!best || CompareFileTime(&deletedAt, &bestTime) > 0)) {
                if (best) best->Release();
                best = child;
                bestTime = deletedAt;
            } else {
                child->Release();
            }
        }
        items->Release();
        return best;
    };

    IShellItem* best = binName.empty() ? nullptr : find(true);
    if (!best) best = find(false);
    bin->Release();
    if (!best) return false;
    bool ok = false;
    IContextMenu* menu = nullptr;
    if (SUCCEEDED(best->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&menu)))) {
        HMENU hMenu = CreatePopupMenu();
        menu->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL);
        CMINVOKECOMMANDINFO info{};
        info.cbSize = sizeof(info);
        info.hwnd = hwndOwner;
        info.lpVerb = "undelete";
        info.nShow = SW_SHOWNORMAL;
        ok = SUCCEEDED(menu->InvokeCommand(&info));
        DestroyMenu(hMenu);
        menu->Release();
    }
    best->Release();
    return ok;
}

std::wstring PickFolderDialog(HWND hwndOwner) {
    std::wstring result;

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) {
        return result;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);

    if (SUCCEEDED(dialog->Show(hwndOwner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }

    dialog->Release();
    return result;
}







#include "image_view.h"

#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "i18n.h"

namespace {

constexpr wchar_t kViewClass[] = L"FilMImageView";

// これを超える画素数の画像は開かない / これを超える画素数は縮小して読み込む (メモリと時間を守るため)。
constexpr unsigned long long kMaxSourcePixels = 400ull * 1000 * 1000;
constexpr unsigned long long kMaxBitmapPixels = 40ull * 1000 * 1000;

template <typename T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

struct View {
    HWND hwnd = nullptr;

    // 読み込み済みの画像 (上から下の 32bit DIB)
    HBITMAP bitmap = nullptr;
    HDC memDc = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    int imgW = 0, imgH = 0;      // 表示用ビットマップの大きさ
    int origW = 0, origH = 0;    // 元の画像の大きさ (縮小して読んだときだけ imgW と違う)

    bool fit = true;
    double zoom = 1.0;
    double offX = 0, offY = 0;  // 画像の左上の位置 (クライアント座標。fit でない間だけ使う)

    bool dragging = false;
    POINT dragStart{};
    double dragOffX = 0, dragOffY = 0;

    std::wstring error;
};

IWICImagingFactory* GetFactory() {
    static IWICImagingFactory* factory = nullptr;
    if (!factory) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    }
    return factory;
}

View* GetView(HWND hwnd) {
    return reinterpret_cast<View*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

// EXIF の向き (1〜8) を読む。無ければ 1。
UINT ReadOrientation(IWICBitmapFrameDecode* frame) {
    IWICMetadataQueryReader* reader = nullptr;
    UINT orientation = 1;
    if (SUCCEEDED(frame->GetMetadataQueryReader(&reader)) && reader) {
        for (const wchar_t* query : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}", L"System.Photo.Orientation"}) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(reader->GetMetadataByName(query, &v))) {
                if (v.vt == VT_UI2) orientation = v.uiVal;
                PropVariantClear(&v);
                if (orientation >= 1 && orientation <= 8) break;
            }
        }
        reader->Release();
    }
    return (orientation >= 1 && orientation <= 8) ? orientation : 1;
}

void FreeBitmap(View* v) {
    if (v->memDc) {
        SelectObject(v->memDc, v->oldBitmap);
        DeleteDC(v->memDc);
        v->memDc = nullptr;
    }
    if (v->bitmap) {
        DeleteObject(v->bitmap);
        v->bitmap = nullptr;
    }
    v->imgW = v->imgH = v->origW = v->origH = 0;
}

void NotifyInfo(View* v) {
    HWND parent = GetParent(v->hwnd);
    if (parent) PostMessageW(parent, kMsgImageViewInfo, 0, 0);
}

// path を読み込んで v のビットマップにする。失敗したら false (v->error に理由)。
bool LoadImageInto(View* v, const std::wstring& path) {
    v->error.clear();
    IWICImagingFactory* factory = GetFactory();
    if (!factory) {
        v->error = Tr(L"画像コンポーネント (WIC) を使えません。");
        return false;
    }
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapSource* source = nullptr;
    IWICBitmapFlipRotator* rotator = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool ok = false;
    bool tooBig = false;

    if (SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame))) {
        UINT fw = 0, fh = 0;
        frame->GetSize(&fw, &fh);
        if (static_cast<unsigned long long>(fw) * fh > kMaxSourcePixels) {
            tooBig = true;
        } else {
            source = frame;
            source->AddRef();
            UINT orientation = ReadOrientation(frame);
            WICBitmapTransformOptions opt = WICBitmapTransformRotate0;
            switch (orientation) {
                case 2: opt = WICBitmapTransformFlipHorizontal; break;
                case 3: opt = WICBitmapTransformRotate180; break;
                case 4: opt = WICBitmapTransformFlipVertical; break;
                case 5: opt = static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal); break;
                case 6: opt = WICBitmapTransformRotate90; break;
                case 7: opt = static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal); break;
                case 8: opt = WICBitmapTransformRotate270; break;
                default: break;
            }
            if (opt != WICBitmapTransformRotate0 && SUCCEEDED(factory->CreateBitmapFlipRotator(&rotator)) &&
                SUCCEEDED(rotator->Initialize(source, opt))) {
                source->Release();
                source = rotator;
                source->AddRef();
            }
            UINT sw = 0, sh = 0;
            source->GetSize(&sw, &sh);
            UINT w = sw, h = sh;
            // 画素数が多すぎるものは、縦横比を保って縮小しながら読む。
            if (static_cast<unsigned long long>(sw) * sh > kMaxBitmapPixels) {
                double k = std::sqrt(static_cast<double>(kMaxBitmapPixels) / (static_cast<double>(sw) * sh));
                w = std::max<UINT>(1, static_cast<UINT>(sw * k));
                h = std::max<UINT>(1, static_cast<UINT>(sh * k));
                if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
                    SUCCEEDED(scaler->Initialize(source, w, h, WICBitmapInterpolationModeFant))) {
                    source->Release();
                    source = scaler;
                    source->AddRef();
                } else {
                    w = sw;
                    h = sh;
                }
            }
            if (SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                SUCCEEDED(converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                                WICBitmapPaletteTypeCustom))) {
                converter->GetSize(&w, &h);
                if (w > 0 && h > 0 && w < 65536 && h < 65536) {
                    BITMAPINFO bi{};
                    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bi.bmiHeader.biWidth = static_cast<LONG>(w);
                    bi.bmiHeader.biHeight = -static_cast<LONG>(h);  // 上から下
                    bi.bmiHeader.biPlanes = 1;
                    bi.bmiHeader.biBitCount = 32;
                    bi.bmiHeader.biCompression = BI_RGB;
                    void* bits = nullptr;
                    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
                    if (bmp && bits && SUCCEEDED(converter->CopyPixels(nullptr, w * 4, w * 4 * h, static_cast<BYTE*>(bits)))) {
                        FreeBitmap(v);
                        HDC screen = GetDC(nullptr);
                        v->memDc = CreateCompatibleDC(screen);
                        ReleaseDC(nullptr, screen);
                        v->bitmap = bmp;
                        v->oldBitmap = SelectObject(v->memDc, bmp);
                        v->imgW = static_cast<int>(w);
                        v->imgH = static_cast<int>(h);
                        v->origW = static_cast<int>(sw);
                        v->origH = static_cast<int>(sh);
                        ok = true;
                    } else if (bmp) {
                        DeleteObject(bmp);
                    }
                }
            }
        }
    }
    SafeRelease(converter);
    SafeRelease(scaler);
    SafeRelease(rotator);
    SafeRelease(source);
    SafeRelease(frame);
    SafeRelease(decoder);
    if (!ok) v->error = tooBig ? Tr(L"画像が大きすぎて開けません。") : Tr(L"この画像を読み込めません。");
    return ok;
}

double FitZoom(const View* v, const RECT& rc) {
    if (v->imgW <= 0 || v->imgH <= 0) return 1.0;
    double zx = static_cast<double>(rc.right) / v->imgW;
    double zy = static_cast<double>(rc.bottom) / v->imgH;
    return std::min(1.0, std::min(zx, zy));
}

double CurrentZoom(const View* v, const RECT& rc) {
    return v->fit ? FitZoom(v, rc) : v->zoom;
}

// 画像の左上の位置 (クライアント座標)。fit や画像が窓より小さいときは中央に置く。
void CurrentOrigin(const View* v, const RECT& rc, double zoom, double& x, double& y) {
    double w = v->imgW * zoom, h = v->imgH * zoom;
    x = (w <= rc.right) ? (rc.right - w) / 2 : v->offX;
    y = (h <= rc.bottom) ? (rc.bottom - h) / 2 : v->offY;
    if (w > rc.right) x = std::clamp(x, rc.right - w, 0.0);
    if (h > rc.bottom) y = std::clamp(y, rc.bottom - h, 0.0);
}

void Redraw(View* v) {
    InvalidateRect(v->hwnd, nullptr, FALSE);
    NotifyInfo(v);
}

void SetZoom(View* v, double newZoom, double anchorX, double anchorY) {
    RECT rc;
    GetClientRect(v->hwnd, &rc);
    double oldZoom = CurrentZoom(v, rc);
    double ox, oy;
    CurrentOrigin(v, rc, oldZoom, ox, oy);
    newZoom = std::clamp(newZoom, 0.02, 32.0);
    // アンカー (マウス位置など) の下にある画像上の点を動かさない。
    double imgX = (anchorX - ox) / oldZoom, imgY = (anchorY - oy) / oldZoom;
    v->fit = false;
    v->zoom = newZoom;
    v->offX = anchorX - imgX * newZoom;
    v->offY = anchorY - imgY * newZoom;
    Redraw(v);
}

void Paint(View* v, HDC hdc) {
    RECT rc;
    GetClientRect(v->hwnd, &rc);
    HDC buf = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max<LONG>(rc.right, 1), std::max<LONG>(rc.bottom, 1));
    HGDIOBJ old = SelectObject(buf, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(24, 24, 24));
    FillRect(buf, &rc, bg);
    DeleteObject(bg);

    if (v->memDc && v->imgW > 0) {
        double zoom = CurrentZoom(v, rc);
        double ox, oy;
        CurrentOrigin(v, rc, zoom, ox, oy);
        // 画面に出る部分だけを描く。
        double x0 = std::max(0.0, ox), y0 = std::max(0.0, oy);
        double x1 = std::min<double>(rc.right, ox + v->imgW * zoom), y1 = std::min<double>(rc.bottom, oy + v->imgH * zoom);
        if (x1 > x0 && y1 > y0) {
            int sx = static_cast<int>(std::floor((x0 - ox) / zoom)), sy = static_cast<int>(std::floor((y0 - oy) / zoom));
            int sx2 = static_cast<int>(std::ceil((x1 - ox) / zoom)), sy2 = static_cast<int>(std::ceil((y1 - oy) / zoom));
            sx = std::clamp(sx, 0, v->imgW - 1);
            sy = std::clamp(sy, 0, v->imgH - 1);
            sx2 = std::clamp(sx2, sx + 1, v->imgW);
            sy2 = std::clamp(sy2, sy + 1, v->imgH);
            int dx = static_cast<int>(std::lround(ox + sx * zoom)), dy = static_cast<int>(std::lround(oy + sy * zoom));
            int dw = static_cast<int>(std::lround((sx2 - sx) * zoom)), dh = static_cast<int>(std::lround((sy2 - sy) * zoom));
            // 縮小は HALFTONE で滑らかに、拡大は元のドットが見えるよう最近傍で。
            SetStretchBltMode(buf, zoom < 1.0 ? HALFTONE : COLORONCOLOR);
            SetBrushOrgEx(buf, 0, 0, nullptr);
            StretchBlt(buf, dx, dy, dw, dh, v->memDc, sx, sy, sx2 - sx, sy2 - sy, SRCCOPY);
        }
    } else if (!v->error.empty()) {
        SetBkMode(buf, TRANSPARENT);
        SetTextColor(buf, RGB(220, 220, 220));
        HGDIOBJ oldFont = SelectObject(buf, GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(buf, v->error.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(buf, oldFont);
    }
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, buf, 0, 0, SRCCOPY);
    SelectObject(buf, old);
    DeleteObject(bmp);
    DeleteDC(buf);
}

LRESULT CALLBACK ViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    View* v = GetView(hwnd);
    switch (msg) {
        case WM_NCCREATE: {
            auto* nv = new View();
            nv->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(nv));
            break;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (v) Paint(v, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            if (v) Redraw(v);
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS;
        case WM_KEYDOWN: {
            if (!v) break;
            RECT rc;
            GetClientRect(hwnd, &rc);
            double cx = rc.right / 2.0, cy = rc.bottom / 2.0;
            switch (wParam) {
                case VK_ADD: case VK_OEM_PLUS:
                    SetZoom(v, CurrentZoom(v, rc) * 1.25, cx, cy);
                    return 0;
                case VK_SUBTRACT: case VK_OEM_MINUS:
                    SetZoom(v, CurrentZoom(v, rc) / 1.25, cx, cy);
                    return 0;
                case '0': case VK_NUMPAD0: case 'F':
                    v->fit = true;
                    Redraw(v);
                    return 0;
                case '1': case VK_NUMPAD1:
                    SetZoom(v, 1.0, cx, cy);
                    return 0;
                default:
                    if (HWND parent = GetParent(hwnd)) SendMessageW(parent, kMsgImageViewKey, wParam, 0);
                    return 0;
            }
        }
        case WM_MOUSEWHEEL: {
            if (!v) break;
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            RECT rc;
            GetClientRect(hwnd, &rc);
            double factor = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.25 : 1.0 / 1.25;
            SetZoom(v, CurrentZoom(v, rc) * factor, pt.x, pt.y);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!v) break;
            SetFocus(hwnd);
            SetCapture(hwnd);
            v->dragging = true;
            v->dragStart = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT rc;
            GetClientRect(hwnd, &rc);
            double ox, oy;
            CurrentOrigin(v, rc, CurrentZoom(v, rc), ox, oy);
            v->dragOffX = ox;
            v->dragOffY = oy;
            return 0;
        }
        case WM_MOUSEMOVE:
            if (v && v->dragging) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                if (v->fit) {
                    v->zoom = FitZoom(v, rc);
                    v->fit = false;
                }
                v->offX = v->dragOffX + (GET_X_LPARAM(lParam) - v->dragStart.x);
                v->offY = v->dragOffY + (GET_Y_LPARAM(lParam) - v->dragStart.y);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (v && v->dragging) {
                v->dragging = false;
                ReleaseCapture();
                return 0;
            }
            break;
        case WM_LBUTTONDBLCLK:
            if (v) {
                if (v->fit) {
                    SetZoom(v, 1.0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                } else {
                    v->fit = true;
                    Redraw(v);
                }
                return 0;
            }
            break;
        case WM_NCDESTROY:
            if (v) {
                FreeBitmap(v);
                delete v;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

HWND ImageViewCreate(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = ViewProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kViewClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    return CreateWindowExW(0, kViewClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

bool ImageViewLoad(HWND view, const std::wstring& path) {
    View* v = GetView(view);
    if (!v) return false;
    bool ok = LoadImageInto(v, path);
    if (!ok) FreeBitmap(v);
    v->fit = true;
    Redraw(v);
    return ok;
}

void ImageViewClear(HWND view) {
    View* v = GetView(view);
    if (!v) return;
    FreeBitmap(v);
    v->error.clear();
    Redraw(v);
}

std::wstring ImageViewInfo(HWND view) {
    View* v = GetView(view);
    if (!v || v->imgW <= 0) return L"";
    RECT rc;
    GetClientRect(view, &rc);
    int percent = static_cast<int>(std::lround(CurrentZoom(v, rc) * 100));
    // 縮小して読み込んだときは、元の大きさを出し、倍率も元の画像に対するものにする。
    if (v->origW > 0 && v->imgW != v->origW) percent = static_cast<int>(std::lround(CurrentZoom(v, rc) * 100.0 * v->imgW / v->origW));
    return std::to_wstring(v->origW) + L" x " + std::to_wstring(v->origH) + L"  -  " + std::to_wstring(percent) + L"%";
}

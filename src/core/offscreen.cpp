#include "core/offscreen.h"

#include <d2d1.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/images.h"
#include "core/render.h"
#include "core/theme.h"

namespace mdv {

ComPtr<IWICBitmap> RenderToBitmap(Document& doc, const OffscreenOptions& opt, OffscreenStats* stats) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        return nullptr;
    }
    ComPtr<ID2D1Factory> d2d;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf()))) return nullptr;
    ComPtr<IDWriteFactory> dw;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dw.GetAddressOf())))) {
        return nullptr;
    }

    const Theme& theme = GetTheme(opt.layout.dark);
    Palette palette;
    ImageStore images;
    images.Attach(&doc, nullptr, opt.layout.dark, opt.allow_remote_images);
    images.LoadAll();

    LayoutEngine layout;
    layout.Init(dw.Get(), &palette, &images);
    layout.SetConfig(opt.layout);
    layout.SetDocument(&doc);
    const double t0 = NowMs();
    layout.Layout(opt.width, opt.scroll_y, opt.scroll_y + opt.max_height);
    const double t1 = NowMs();

    const float doc_h = layout.doc_height();
    const float view_h = std::max(1.f, std::min(doc_h - opt.scroll_y, opt.max_height));
    const UINT px_w = static_cast<UINT>(std::ceil(opt.width * opt.scale));
    const UINT px_h = static_cast<UINT>(std::ceil(view_h * opt.scale));

    ComPtr<IWICBitmap> bitmap;
    if (FAILED(wic->CreateBitmap(px_w, px_h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap))) {
        return nullptr;
    }
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f * opt.scale,
        96.f * opt.scale);
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &rt))) return nullptr;
    if (!palette.Create(rt.Get(), theme)) return nullptr;
    layout.InvalidateTextLayouts();  // brushes now exist
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    Renderer renderer;
    RenderState st;
    st.scroll_y = opt.scroll_y;
    st.view_w = opt.width;
    st.view_h = view_h;
    st.dpi_scale = opt.scale;
    rt->BeginDraw();
    rt->Clear(opt.transparent ? D2D1::ColorF(0, 0, 0, 0) : theme.Color(kColorBackground));
    renderer.Draw(rt.Get(), layout, st);
    const HRESULT hr = rt->EndDraw();
    const double t2 = NowMs();
    if (stats) {
        stats->doc_height = doc_h;
        stats->layout_ms = t1 - t0;
        stats->render_ms = t2 - t1;
    }
    if (FAILED(hr)) return nullptr;
    return bitmap;
}

bool SaveBitmapPng(IWICBitmap* bitmap, const std::wstring& path) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        return false;
    }
    ComPtr<IWICStream> stream;
    if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) {
        return false;
    }
    ComPtr<IWICBitmapEncoder> enc;
    if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
        FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        return false;
    }
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(enc->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr))) return false;
    UINT w = 0, h = 0;
    bitmap->GetSize(&w, &h);
    frame->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&fmt);
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(bitmap, fmt, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        return false;
    }
    if (FAILED(frame->WriteSource(conv.Get(), nullptr)) || FAILED(frame->Commit())) return false;
    return SUCCEEDED(enc->Commit());
}

HBITMAP BitmapToHBitmap(IWICBitmap* bitmap) {
    UINT w = 0, h = 0;
    if (!bitmap || FAILED(bitmap->GetSize(&w, &h)) || w == 0 || h == 0) return nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = static_cast<LONG>(w);
    bi.bmiHeader.biHeight = -static_cast<LONG>(h);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hbm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbm) return nullptr;
    WICRect rc{0, 0, static_cast<INT>(w), static_cast<INT>(h)};
    if (FAILED(bitmap->CopyPixels(&rc, w * 4, w * h * 4, static_cast<BYTE*>(bits)))) {
        DeleteObject(hbm);
        return nullptr;
    }
    return hbm;
}

}  // namespace mdv

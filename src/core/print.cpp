#include "core/print.h"

#include <commdlg.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "comdlg32.lib")

namespace mdv {

bool PageRenderer::Init(Document& doc, const LayoutConfig& config, float width, float dpi, UINT band_px) {
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_))) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dw_.GetAddressOf())))) {
        return false;
    }
    width_ = width;
    scale_ = dpi / 96.f;
    width_px_ = static_cast<UINT>(std::ceil(width * scale_));
    band_px_ = band_px;
    // Synchronous loading: remote images only from the download cache (the
    // viewer has fetched those on screen), never over the network here.
    images_.Attach(&doc, nullptr, config.dark, false);
    images_.LoadAll();
    layout_.Init(dw_.Get(), &palette_, &images_);
    layout_.SetConfig(config);
    layout_.SetDocument(&doc);
    if (FAILED(wic_->CreateBitmap(width_px_, band_px_, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand,
                                  &bitmap_))) {
        return false;
    }
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpi, dpi);
    if (FAILED(d2d_->CreateWicBitmapRenderTarget(bitmap_.Get(), props, &rt_))) return false;
    if (!palette_.Create(rt_.Get(), GetTheme(config.dark))) return false;
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    // Everything is measured exactly (pagination needs real line positions).
    layout_.Layout(width, 0.f, 1e12f);
    return true;
}

std::vector<std::pair<float, float>> PageRenderer::Paginate(float page_h) {
    std::vector<float> cand;
    Document* doc = layout_.document();
    std::vector<std::pair<Block*, bool>> todo{{doc->root, false}};
    while (!todo.empty()) {
        auto [b, in_table] = todo.back();
        todo.pop_back();
        if (!b->visible) continue;
        if (b->type != BlockType::Document) {
            cand.push_back(b->y);
            cand.push_back(b->y + b->h);
        }
        const bool table = in_table || b->type == BlockType::Table;
        if (b->IsLeaf() && !table) {
            // Line tops inside paragraphs and code blocks.
            if (IDWriteTextLayout* tl = layout_.TextLayout(b)) {
                UINT32 n = 0;
                tl->GetLineMetrics(nullptr, 0, &n);
                std::vector<DWRITE_LINE_METRICS> lines(n);
                if (n && SUCCEEDED(tl->GetLineMetrics(lines.data(), n, &n))) {
                    float y = layout_.TextOrigin(b).y;
                    for (const auto& lm : lines) {
                        cand.push_back(y);
                        y += lm.height;
                    }
                }
            }
        }
        for (Block* c : b->children) {
            if (b->type == BlockType::TableRow) continue;  // never split inside a row
            todo.push_back({c, table});
        }
    }
    std::sort(cand.begin(), cand.end());
    const float total = layout_.doc_height();
    std::vector<std::pair<float, float>> pages;
    float top = 0;
    while (top < total - 0.5f && pages.size() < 100000) {
        const float limit = top + page_h;
        if (limit >= total) {
            pages.emplace_back(top, total);
            break;
        }
        auto it = std::upper_bound(cand.begin(), cand.end(), limit);
        float brk = limit;
        if (it != cand.begin()) {
            const float c = *(it - 1);
            if (c > top + page_h * 0.35f) brk = c;
        }
        pages.emplace_back(top, brk);
        top = brk;
    }
    return pages;
}

IWICBitmap* PageRenderer::RenderBand(float y, float height, float clip_bottom) {
    images_.BeginPaint();  // (each band a paint: the pictures of past ones may go)
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->Clear(palette_.theme().Color(kColorBackground));
    const float visible = std::min(height, clip_bottom - y);
    if (visible > 0) {
        rt_->PushAxisAlignedClip(D2D1::RectF(0, 0, width_, visible), D2D1_ANTIALIAS_MODE_ALIASED);
        RenderState st;
        st.scroll_y = y;
        st.view_w = width_;
        st.view_h = visible;
        st.dpi_scale = scale_;
        renderer_.Draw(rt_.Get(), layout_, st);
        rt_->PopAxisAlignedClip();
    }
    rt_->EndDraw();
    images_.TrimMemory();
    return bitmap_.Get();
}

HGLOBAL PrinterDevNames(const wchar_t* printer) {
    // "winspool", the printer, no port (the printer's own); offsets in
    // characters from the start of the structure.
    constexpr wchar_t kDriver[] = L"winspool";
    constexpr size_t kHeader = sizeof(DEVNAMES) / sizeof(wchar_t);
    constexpr size_t kDriverLength = std::size(kDriver);  // (with its terminator)
    const size_t n = printer ? wcslen(printer) : 0;
    if (n == 0 || n > 4096) return nullptr;
    HGLOBAL h = GlobalAlloc(GHND, (kHeader + kDriverLength + n + 2) * sizeof(wchar_t));  // (zeroed)
    auto* names = h ? static_cast<DEVNAMES*>(GlobalLock(h)) : nullptr;
    if (!names) {
        if (h) GlobalFree(h);
        return nullptr;
    }
    names->wDriverOffset = static_cast<WORD>(kHeader);
    names->wDeviceOffset = static_cast<WORD>(kHeader + kDriverLength);
    names->wOutputOffset = static_cast<WORD>(kHeader + kDriverLength + n + 1);
    wchar_t* chars = reinterpret_cast<wchar_t*>(names);
    wmemcpy(chars + names->wDriverOffset, kDriver, kDriverLength);
    wmemcpy(chars + names->wDeviceOffset, printer, n);
    GlobalUnlock(h);
    return h;
}

bool PrintDocument(HWND owner, Document& doc, const LayoutConfig& config, const std::wstring& title,
                   const wchar_t* printer, const RECT* margins) {
    if (!SystemDllLoads(L"comdlg32.dll")) return false;
    constexpr DWORD kFlags = PD_RETURNDC | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE | PD_ALLPAGES;
    PRINTDLGW pd{};
    pd.lStructSize = sizeof pd;
    pd.hwndOwner = owner;
    pd.Flags = kFlags;
    pd.nMinPage = 1;
    pd.nMaxPage = 9999;
    pd.nFromPage = 1;
    pd.nToPage = 9999;
    pd.nCopies = 1;
    // Released however printing ends, an exception (out of memory) included:
    // a job started is ended or aborted, never left open in the spooler.
    struct Job {
        PRINTDLGW& pd;
        HFONT font = nullptr;
        bool started = false, ended = false;
        ~Job() {
            if (started && !ended) AbortDoc(pd.hDC);
            if (pd.hDC) DeleteDC(pd.hDC);  // (first: the font may still be selected)
            if (font) DeleteObject(font);
            if (pd.hDevMode) GlobalFree(pd.hDevMode);
            if (pd.hDevNames) GlobalFree(pd.hDevNames);
        }
    } job{pd};
    // The dialog starts with the printer chosen in Total Commander. One it
    // doesn't take (removed since: PDERR_PRINTERNOTFOUND) is left out.
    pd.hDevNames = PrinterDevNames(printer);
    if (pd.hDevNames) pd.Flags |= PD_NOWARNING;
    BOOL chosen = PrintDlgW(&pd);
    if (!chosen && pd.hDevNames && CommDlgExtendedError() != 0 && IsWindow(owner)) {
        GlobalFree(pd.hDevNames);
        pd.hDevNames = nullptr;
        if (pd.hDevMode) GlobalFree(pd.hDevMode);
        pd.hDevMode = nullptr;
        pd.Flags = kFlags;
        chosen = PrintDlgW(&pd);
    }
    if (!chosen) return CommDlgExtendedError() == 0;  // cancelled, or failed (no printer...)
    const HDC dc = pd.hDC;
    // The owner (the viewer, which owns `doc`) may have been closed while a
    // dialog was up: here, and in StartDoc (a printer to a file asks for its
    // name). Nothing touches `doc` before both.
    if (!dc || !IsWindow(owner)) return false;
    const int dpi_x = GetDeviceCaps(dc, LOGPIXELSX), dpi_y = GetDeviceCaps(dc, LOGPIXELSY);
    const int phys_w = GetDeviceCaps(dc, PHYSICALWIDTH), phys_h = GetDeviceCaps(dc, PHYSICALHEIGHT);
    const int off_x = GetDeviceCaps(dc, PHYSICALOFFSETX), off_y = GetDeviceCaps(dc, PHYSICALOFFSETY);
    const int res_w = GetDeviceCaps(dc, HORZRES), res_h = GetDeviceCaps(dc, VERTRES);
    const RECT m = margins ? *margins : RECT{150, 150, 150, 150};  // 15 mm
    auto mm10 = [](LONG v, int dpi) { return MulDiv(static_cast<int>(v), dpi, 254); };
    const int left = std::max(0, mm10(m.left, dpi_x) - off_x);
    const int top = std::max(0, mm10(m.top, dpi_y) - off_y);
    const int right = std::min(res_w, phys_w - mm10(m.right, dpi_x) - off_x);
    const int bottom = std::min(res_h, phys_h - mm10(m.bottom, dpi_y) - off_y);
    const int footer_h = dpi_y / 3;
    const int content_w = right - left;
    const int content_h = bottom - top - footer_h;
    if (content_w < dpi_x || content_h < dpi_y) return false;
    const float render_dpi = static_cast<float>(std::min(dpi_x, 300));
    const float width_dip = content_w * 96.f / dpi_x;
    const float page_h_dip = content_h * 96.f / dpi_y;

    DOCINFOW di{};
    di.cbSize = sizeof di;
    di.lpszDocName = title.c_str();
    if (StartDocW(dc, &di) <= 0) return false;
    job.started = true;
    PageRenderer pr;
    if (!IsWindow(owner) || !pr.Init(doc, config, width_dip, render_dpi, 512)) return false;
    const auto pages = pr.Paginate(page_h_dip);
    const HFONT font = job.font =
        CreateFontW(-MulDiv(8, dpi_y, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    const float band_dip = static_cast<float>(pr.band_px()) / pr.scale();
    bool ok = true;
    for (size_t p = 0; p < pages.size() && ok; ++p) {
        if (!IsWindow(owner)) {  // (in case a printer driver shows UI per page)
            ok = false;
            break;
        }
        const int page_no = static_cast<int>(p) + 1;
        if ((pd.Flags & PD_PAGENUMS) && (page_no < pd.nFromPage || page_no > pd.nToPage)) continue;
        if (StartPage(dc) <= 0) {
            ok = false;
            break;
        }
        const float ptop = pages[p].first, pbottom = pages[p].second;
        for (float by = 0; by < pbottom - ptop; by += band_dip) {
            IWICBitmap* bmp = pr.RenderBand(ptop + by, band_dip, pbottom);
            const UINT rows = std::min<UINT>(pr.band_px(),
                                             static_cast<UINT>(std::ceil((pbottom - ptop - by) * pr.scale())));
            if (rows == 0) break;
            WICRect rc{0, 0, static_cast<INT>(pr.width_px()), static_cast<INT>(rows)};
            ComPtr<IWICBitmapLock> lock;
            if (FAILED(bmp->Lock(&rc, WICBitmapLockRead, &lock))) continue;
            UINT size = 0;
            BYTE* data = nullptr;
            lock->GetDataPointer(&size, &data);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = static_cast<LONG>(pr.width_px());
            bi.bmiHeader.biHeight = -static_cast<LONG>(rows);
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            const int dst_y = top + static_cast<int>(std::lround(by * dpi_y / 96.f));
            const int dst_h = static_cast<int>(std::lround(rows * dpi_y / render_dpi));
            StretchDIBits(dc, left, dst_y, content_w, dst_h, 0, 0, static_cast<int>(pr.width_px()),
                          static_cast<int>(rows), data, &bi, DIB_RGB_COLORS, SRCCOPY);
        }
        // Footer: file name and page number.
        HGDIOBJ old_font = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(110, 118, 129));
        RECT fr{left, bottom - footer_h / 2, right, bottom};
        DrawTextW(dc, title.c_str(), -1, &fr, DT_LEFT | DT_SINGLELINE | DT_BOTTOM | DT_END_ELLIPSIS | DT_NOPREFIX);
        const std::wstring num = std::to_wstring(page_no) + L" / " + std::to_wstring(pages.size());
        DrawTextW(dc, num.c_str(), -1, &fr, DT_RIGHT | DT_SINGLELINE | DT_BOTTOM | DT_NOPREFIX);
        SelectObject(dc, old_font);
        if (EndPage(dc) <= 0) ok = false;
    }
    if (ok) job.ended = EndDoc(dc) > 0;  // (else Job aborts it)
    return ok;
}

}  // namespace mdv

#include "core/svg.h"

#include <shlwapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "core/html.h"
#include "core/layout.h"

namespace mdv {
namespace {

bool ParseNumber(std::wstring_view s, size_t& i, float& out) {
    while (i < s.size() && (IsSpace(s[i]) || s[i] == L',')) ++i;
    const size_t start = i;
    if (i < s.size() && (s[i] == L'-' || s[i] == L'+')) ++i;
    bool digits = false;
    while (i < s.size() && ((s[i] >= L'0' && s[i] <= L'9') || s[i] == L'.')) {
        digits = true;
        ++i;
    }
    if (digits && i < s.size() && (s[i] == L'e' || s[i] == L'E')) {
        size_t j = i + 1;
        if (j < s.size() && (s[j] == L'-' || s[j] == L'+')) ++j;
        if (j < s.size() && s[j] >= L'0' && s[j] <= L'9') {
            i = j;
            while (i < s.size() && s[i] >= L'0' && s[i] <= L'9') ++i;
        }
    }
    if (!digits) {
        i = start;
        return false;
    }
    out = std::wcstof(std::wstring(s.substr(start, i - start)).c_str(), nullptr);
    return true;
}

// "20", "20px", "1.5em" -> CSS px. Percentages are rejected.
bool ParseSvgLength(std::wstring_view s, float& out) {
    s = Trim(s);
    size_t i = 0;
    float v;
    if (!ParseNumber(s, i, v)) return false;
    std::wstring_view unit = Trim(s.substr(i));
    if (unit.empty() || unit == L"px") out = v;
    else if (unit == L"pt") out = v * 4.f / 3.f;
    else if (unit == L"em") out = v * 16.f;
    else if (unit == L"in") out = v * 96.f;
    else if (unit == L"cm") out = v * 96.f / 2.54f;
    else if (unit == L"mm") out = v * 96.f / 25.4f;
    else return false;
    return true;
}

int HexVal(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

// Parses an SVG paint. Returns false for "none"/unsupported values.
bool ParseColor(std::wstring_view s, uint32_t& rgb) {
    s = Trim(s);
    if (s.empty()) return false;
    if (s[0] == L'#') {
        if (s.size() == 4) {
            int r = HexVal(s[1]), g = HexVal(s[2]), b = HexVal(s[3]);
            if (r < 0 || g < 0 || b < 0) return false;
            rgb = static_cast<uint32_t>((r * 17) << 16 | (g * 17) << 8 | (b * 17));
            return true;
        }
        if (s.size() >= 7) {
            uint32_t v = 0;
            for (size_t i = 1; i < 7; ++i) {
                const int h = HexVal(s[i]);
                if (h < 0) return false;
                v = (v << 4) | static_cast<uint32_t>(h);
            }
            rgb = v;
            return true;
        }
        return false;
    }
    if (StartsWithNoCase(s, L"rgb")) {
        size_t i = s.find(L'(');
        if (i == std::wstring_view::npos) return false;
        ++i;
        float c[3];
        for (float& v : c) {
            if (!ParseNumber(s, i, v)) return false;
            if (i < s.size() && s[i] == L'%') {
                v = v * 2.55f;
                ++i;
            }
        }
        rgb = static_cast<uint32_t>(std::clamp(c[0], 0.f, 255.f)) << 16 |
              static_cast<uint32_t>(std::clamp(c[1], 0.f, 255.f)) << 8 |
              static_cast<uint32_t>(std::clamp(c[2], 0.f, 255.f));
        return true;
    }
    static constexpr struct {
        const wchar_t* name;
        uint32_t rgb;
    } kNamed[] = {
        {L"black", 0x000000}, {L"white", 0xFFFFFF}, {L"red", 0xFF0000}, {L"green", 0x008000},
        {L"blue", 0x0000FF}, {L"gray", 0x808080}, {L"grey", 0x808080}, {L"silver", 0xC0C0C0},
        {L"orange", 0xFFA500}, {L"yellow", 0xFFFF00}, {L"purple", 0x800080}, {L"navy", 0x000080},
        {L"teal", 0x008080}, {L"maroon", 0x800000}, {L"olive", 0x808000}, {L"lime", 0x00FF00},
        {L"aqua", 0x00FFFF}, {L"fuchsia", 0xFF00FF}, {L"darkgray", 0xA9A9A9}, {L"lightgray", 0xD3D3D3},
        {L"dimgray", 0x696969}, {L"brown", 0xA52A2A}, {L"crimson", 0xDC143C}, {L"gold", 0xFFD700},
    };
    for (const auto& n : kNamed) {
        if (EqualsNoCase(s, n.name)) {
            rgb = n.rgb;
            return true;
        }
    }
    return false;
}

D2D1_MATRIX_3X2_F ParseTransform(std::wstring_view s) {
    D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Identity();
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (IsSpace(s[i]) || s[i] == L',')) ++i;
        const size_t name_start = i;
        while (i < s.size() && ((s[i] >= L'a' && s[i] <= L'z') || (s[i] >= L'A' && s[i] <= L'Z'))) ++i;
        const std::wstring_view name = s.substr(name_start, i - name_start);
        const size_t open = s.find(L'(', i);
        const size_t close = s.find(L')', i);
        if (name.empty() || open == std::wstring_view::npos || close == std::wstring_view::npos || close < open) break;
        std::wstring_view args = s.substr(open + 1, close - open - 1);
        float a[6] = {0, 0, 0, 0, 0, 0};
        int n = 0;
        size_t k = 0;
        while (n < 6 && ParseNumber(args, k, a[n])) ++n;
        D2D1_MATRIX_3X2_F t = D2D1::Matrix3x2F::Identity();
        if (name == L"translate") t = D2D1::Matrix3x2F::Translation(a[0], n > 1 ? a[1] : 0.f);
        else if (name == L"scale") t = D2D1::Matrix3x2F::Scale(a[0], n > 1 ? a[1] : a[0]);
        else if (name == L"rotate") t = D2D1::Matrix3x2F::Rotation(a[0], D2D1::Point2F(a[1], a[2]));
        else if (name == L"matrix" && n == 6) t = D2D1::Matrix3x2F(a[0], a[1], a[2], a[3], a[4], a[5]);
        // SVG applies the leftmost transform last.
        m = t * m;
        i = close + 1;
    }
    return m;
}

constexpr int kMaxSvgNesting = 2;         // SVG in <image> in SVG...
constexpr size_t kMaxSvgImages = 32;      // <image> elements per document
constexpr UINT kMaxEmbeddedRaster = 1024; // width/height of embedded bitmaps
// Decoded embedded bitmaps of one SVG, those of nested SVGs included: the
// limits above allow some 1000 of 4 MB each, and a 1024 x 1024 PNG of one
// color takes 3 KB of source.
constexpr size_t kMaxSvgRasterBytes = 16u << 20;

// Decodes an embedded bitmap (PNG, JPEG, GIF...) to 32bpp PBGRA, if its
// pixels take at most `budget` bytes.
bool DecodeRaster(const std::string& bytes, size_t budget, UINT& w, UINT& h, std::vector<uint8_t>& out) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        return false;
    }
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()), static_cast<UINT>(bytes.size())));
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    if (!stream ||
        FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0 ||
        w > kMaxEmbeddedRaster || h > kMaxEmbeddedRaster || static_cast<uint64_t>(w) * h * 4 > budget ||
        FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeMedianCut))) {
        return false;
    }
    out.resize(static_cast<size_t>(w) * h * 4);
    return SUCCEEDED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.size()), out.data()));
}

// Direct2D drops the whole subtree of elements it doesn't know, and badges
// (shields.io "flat" style) wrap everything in an <a>: unwrap links.
void UnwrapLinks(std::string& svg) {
    std::string out;
    out.reserve(svg.size());
    size_t i = 0;
    while (i < svg.size()) {
        const bool open = svg.compare(i, 2, "<a") == 0 && i + 2 < svg.size() &&
                          (svg[i + 2] == ' ' || svg[i + 2] == '>' || svg[i + 2] == '\t' || svg[i + 2] == '\n' ||
                           svg[i + 2] == '\r');
        const bool close = svg.compare(i, 4, "</a>") == 0;
        if (open) {
            const size_t end = svg.find('>', i);
            if (end == std::string::npos) break;
            i = end + 1;
        } else if (close) {
            i += 4;
        } else {
            out.push_back(svg[i++]);
        }
    }
    svg.swap(out);
}

std::wstring PickFamily(IDWriteFactory* dw, std::wstring_view list) {
    size_t i = 0;
    while (i < list.size()) {
        size_t e = list.find(L',', i);
        if (e == std::wstring_view::npos) e = list.size();
        std::wstring_view f = Trim(list.substr(i, e - i));
        if (!f.empty() && (f.front() == L'\'' || f.front() == L'"')) f = f.substr(1, f.size() >= 2 ? f.size() - 2 : 0);
        std::wstring name(f);
        if (EqualsNoCase(name, L"sans-serif") || EqualsNoCase(name, L"system-ui")) return L"Segoe UI";
        if (EqualsNoCase(name, L"serif")) return L"Times New Roman";
        if (EqualsNoCase(name, L"monospace")) return L"Consolas";
        if (!name.empty() && FontFamilyExists(dw, name.c_str())) return name;
        i = e + 1;
    }
    return L"Segoe UI";
}

}  // namespace

std::shared_ptr<SvgImage> SvgImage::Parse(std::string bytes) {
    size_t raster_left = kMaxSvgRasterBytes;
    return Parse(std::move(bytes), 0, &raster_left);
}

std::shared_ptr<SvgImage> SvgImage::Parse(std::string bytes, int depth, size_t* raster_left) {
    if (bytes.find("<svg") == std::string::npos) return nullptr;  // (after any prolog: a license in a comment...)
    auto img = std::make_shared<SvgImage>();
    img->source_ = std::move(bytes);
    img->depth_ = depth;
    img->raster_left_ = raster_left;
    img->ParseStructure();
    img->raster_left_ = nullptr;
    if (img->source_.find("<a") != std::string::npos) UnwrapLinks(img->source_);
    return img;
}

// Only embedded data is used: an image never makes us fetch anything.
void SvgImage::AddImage(Image im, std::wstring_view href) {
    if (images_.size() >= kMaxSvgImages || depth_ >= kMaxSvgNesting) return;
    std::string bytes;
    if (!DecodeDataUri(Trim(href), bytes) || bytes.size() > (4u << 20)) return;
    if (auto nested = Parse(bytes, depth_ + 1, raster_left_)) {
        im.svg = std::move(nested);
    } else if (!raster_left_ || !DecodeRaster(bytes, *raster_left_, im.px_w, im.px_h, im.pixels)) {
        return;
    } else {
        *raster_left_ -= im.pixels.size();
    }
    images_.push_back(std::move(im));
}

void SvgImage::ReleaseDeviceResources() {
    doc_.Reset();
    doc_rt_ = nullptr;
    doc_failed_ = false;
    for (Image& im : images_) {
        im.bitmap.Reset();
        if (im.svg) im.svg->ReleaseDeviceResources();
    }
}

size_t SvgImage::MemoryBytes() const {
    // (Direct2D's document of a 4 MB drawing of rectangles took some 30 MB.)
    size_t n = source_.size() * (doc_ ? 9 : 1) + texts_.size() * sizeof(Text);
    for (const Image& im : images_) {
        n += im.pixels.size() * (im.bitmap ? 2 : 1);
        if (im.svg) n += im.svg->MemoryBytes();
    }
    return n;
}

void SvgImage::ParseStructure() {
    const std::wstring w = Utf8ToWide(source_);
    HtmlTokenizer tz(w);
    HtmlToken tok;
    struct Inherited {
        std::wstring family = L"sans-serif";
        float size = 16;
        uint16_t weight = 400;
        bool italic = false;
        int anchor = 0;
        uint32_t fill = 0x000000;
        float fill_opacity = 1;
        bool no_fill = false;
        bool hidden = false;  // not drawn
        bool gone = false;    // display:none or in <defs>: visibility can't bring it back
        D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Identity();
    };
    // Every element starts from a copy of its parent's state: past this
    // depth, elements share the deepest one (their end tags just count down).
    constexpr size_t kMaxDepth = 256;
    std::vector<Inherited> stack(1);
    size_t beyond = 0;  // open elements past kMaxDepth
    bool root_seen = false;
    int text_index = -1;
    auto apply = [](Inherited& in, std::wstring_view name, std::wstring_view value) {
        if (name == L"font-family") {
            if (value.size() <= 256) in.family = std::wstring(value);  // (copied into every element below)
        } else if (name == L"font-size") {
            ParseSvgLength(value, in.size);
        } else if (name == L"font-weight") {
            if (value == L"bold" || value == L"bolder") in.weight = 700;
            else if (value == L"normal") in.weight = 400;
            else if (int v = _wtoi(std::wstring(value).c_str()); v >= 100 && v <= 900) in.weight = static_cast<uint16_t>(v);
        } else if (name == L"font-style") in.italic = value == L"italic" || value == L"oblique";
        else if (name == L"text-anchor") in.anchor = value == L"middle" ? 1 : value == L"end" ? 2 : 0;
        else if (name == L"fill") {
            uint32_t rgb;
            if (ParseColor(value, rgb)) {
                in.fill = rgb;
                in.no_fill = false;
            } else if (value == L"none") {
                in.no_fill = true;
            }
        } else if (name == L"fill-opacity" || name == L"opacity") {
            size_t i = 0;
            float v;
            if (ParseNumber(value, i, v)) in.fill_opacity *= std::clamp(v, 0.f, 1.f);
        } else if (name == L"display") {
            if (value == L"none") in.hidden = in.gone = true;
        } else if (name == L"visibility") {
            in.hidden = in.gone || value == L"hidden";
        }
    };
    while (tz.Next(tok)) {
        if (tok.kind == HtmlToken::StartTag) {
            Inherited in = stack.back();
            for (const HtmlAttr& a : tok.attrs) {
                if (a.name == L"transform") {
                    in.m = ParseTransform(a.value) * in.m;
                } else if (a.name == L"style") {
                    std::wstring_view st = a.value;
                    size_t i = 0;
                    while (i < st.size()) {
                        size_t e = st.find(L';', i);
                        if (e == std::wstring_view::npos) e = st.size();
                        const std::wstring_view decl = st.substr(i, e - i);
                        const size_t colon = decl.find(L':');
                        if (colon != std::wstring_view::npos) {
                            apply(in, Trim(decl.substr(0, colon)), Trim(decl.substr(colon + 1)));
                        }
                        i = e + 1;
                    }
                } else {
                    apply(in, a.name, a.value);
                }
            }
            const std::wstring& n = tok.name;
            if (n == L"svg" && !root_seen) {
                root_seen = true;
                float vb[4];
                int cnt = 0;
                if (const auto* v = tok.Attr(L"viewbox")) {
                    size_t i = 0;
                    while (cnt < 4 && ParseNumber(*v, i, vb[cnt])) ++cnt;
                }
                if (cnt == 4 && vb[2] > 0 && vb[3] > 0 && std::isfinite(vb[0]) && std::isfinite(vb[1]) &&
                    vb[2] < 1e7f && vb[3] < 1e7f) {
                    vb_x_ = vb[0];
                    vb_y_ = vb[1];
                    vb_w_ = vb[2];
                    vb_h_ = vb[3];
                }
                float fw = 0, fh = 0;
                const auto* ws = tok.Attr(L"width");
                const auto* hs = tok.Attr(L"height");
                const bool has_w = ws && ParseSvgLength(*ws, fw) && fw > 0;
                const bool has_h = hs && ParseSvgLength(*hs, fh) && fh > 0;
                if (has_w && has_h) {
                    width_ = fw;
                    height_ = fh;
                } else if (vb_w_ > 0) {
                    if (has_w) {
                        width_ = fw;
                        height_ = fw * vb_h_ / vb_w_;
                    } else if (has_h) {
                        height_ = fh;
                        width_ = fh * vb_w_ / vb_h_;
                    } else {
                        width_ = vb_w_;
                        height_ = vb_h_;
                    }
                }
            } else if (n == L"defs" || n == L"clippath" || n == L"mask" || n == L"symbol" ||
                       n == L"title" || n == L"desc" || n == L"metadata" || n == L"style" || n == L"script" ||
                       n == L"pattern" || n == L"marker" || n == L"lineargradient" || n == L"radialgradient") {
                in.hidden = in.gone = true;
            } else if (n == L"image" && !in.hidden) {
                const std::wstring* href = tok.Attr(L"href");
                if (!href) href = tok.Attr(L"xlink:href");
                if (href) {
                    Image im;
                    auto length = [&tok](const wchar_t* name, float& out) {
                        if (const auto* v = tok.Attr(name)) ParseSvgLength(*v, out);
                    };
                    length(L"x", im.x);
                    length(L"y", im.y);
                    length(L"width", im.w);
                    length(L"height", im.h);
                    im.transform = in.m;
                    AddImage(std::move(im), *href);
                }
            } else if (n == L"tspan" && text_index >= 0 && !in.hidden &&
                       (tok.Attr(L"x") || tok.Attr(L"y"))) {
                // A positioned run starts a text of its own (badges put the
                // coordinates on the <tspan> only).
                Text t = texts_[static_cast<size_t>(text_index)];
                t.text.clear();
                t.text_length = 0;
                if (const auto* v = tok.Attr(L"x")) {
                    size_t i = 0;
                    ParseNumber(*v, i, t.x);
                }
                if (const auto* v = tok.Attr(L"y")) {
                    size_t i = 0;
                    ParseNumber(*v, i, t.y);
                }
                t.argb = (static_cast<uint32_t>(std::lround(in.fill_opacity * 255.f)) << 24) | in.fill;
                t.size = in.size;
                t.weight = in.weight;
                if (texts_[static_cast<size_t>(text_index)].text.empty()) {
                    texts_[static_cast<size_t>(text_index)] = std::move(t);
                } else {
                    texts_.push_back(std::move(t));
                    text_index = static_cast<int>(texts_.size() - 1);
                }
            } else if (n == L"text" && !in.hidden && !in.no_fill && !tok.Attr(L"filter")) {
                // (Filtered text is a blurred shadow: drawn crisp it would look
                // like a dark duplicate, so it is left out.)
                Text t;
                if (const auto* v = tok.Attr(L"x")) {
                    size_t i = 0;
                    ParseNumber(*v, i, t.x);
                }
                if (const auto* v = tok.Attr(L"y")) {
                    size_t i = 0;
                    ParseNumber(*v, i, t.y);
                }
                if (const auto* v = tok.Attr(L"textlength")) {
                    size_t i = 0;
                    ParseNumber(*v, i, t.text_length);
                }
                t.family = in.family;
                t.size = in.size;
                t.weight = in.weight;
                t.italic = in.italic;
                t.anchor = in.anchor;
                t.argb = (static_cast<uint32_t>(std::lround(in.fill_opacity * 255.f)) << 24) | in.fill;
                t.transform = in.m;
                texts_.push_back(std::move(t));
                text_index = static_cast<int>(texts_.size() - 1);
            }
            if (!tok.self_closing) {
                if (stack.size() < kMaxDepth) stack.push_back(std::move(in));
                else ++beyond;
            }
        } else if (tok.kind == HtmlToken::EndTag) {
            if (beyond > 0) --beyond;
            else if (stack.size() > 1) stack.pop_back();
            if (tok.name == L"text") text_index = -1;
        } else if (tok.kind == HtmlToken::Text && text_index >= 0 && !stack.back().hidden) {
            std::wstring& dst = texts_[static_cast<size_t>(text_index)].text;
            for (wchar_t c : tok.text) {
                if (IsSpace(c)) {
                    if (!dst.empty() && dst.back() != L' ') dst.push_back(L' ');
                } else {
                    dst.push_back(c);
                }
            }
        }
    }
    for (Text& t : texts_) {
        while (!t.text.empty() && t.text.back() == L' ') t.text.pop_back();
    }
    std::erase_if(texts_, [](const Text& t) { return t.text.empty(); });
    // std::clamp passes NaN through; absurd sizes fall back to the CSS default.
    if (!(width_ >= 1.f && width_ <= 20000.f)) width_ = std::isfinite(width_) && width_ > 20000.f ? 20000.f : 300.f;
    if (!(height_ >= 1.f && height_ <= 20000.f)) height_ = std::isfinite(height_) && height_ > 20000.f ? 20000.f : 150.f;
}

void SvgImage::Draw(ID2D1RenderTarget* rt, const D2D1_RECT_F& rect, IDWriteFactory* dw) {
    ComPtr<ID2D1DeviceContext5> dc5;
    if (FAILED(rt->QueryInterface(IID_PPV_ARGS(&dc5)))) return;
    if (doc_rt_ != rt) {
        doc_.Reset();
        doc_failed_ = false;
        doc_rt_ = rt;
        for (Image& im : images_) im.bitmap.Reset();
    }
    if (!doc_ && !doc_failed_) {
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(source_.data()),
                                        static_cast<UINT>(source_.size())));
        if (!stream || FAILED(dc5->CreateSvgDocument(stream.Get(), D2D1::SizeF(width_, height_), &doc_))) {
            doc_failed_ = true;
        }
    }
    D2D1_MATRIX_3X2_F old;
    rt->GetTransform(&old);
    // Like a browser, nothing is drawn outside the image's viewport.
    rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // Undone however drawing ends (strings for the texts may throw).
    struct Restore {
        ID2D1RenderTarget* rt;
        D2D1_MATRIX_3X2_F old;
        ~Restore() {
            rt->SetTransform(old);
            rt->PopAxisAlignedClip();
        }
    } restore{rt, old};
    const float sx = (rect.right - rect.left) / width_;
    const float sy = (rect.bottom - rect.top) / height_;
    const D2D1_MATRIX_3X2_F place = D2D1::Matrix3x2F::Scale(sx, sy) * D2D1::Matrix3x2F::Translation(rect.left, rect.top);
    if (doc_) {
        rt->SetTransform(place * old);
        dc5->DrawSvgDocument(doc_.Get());
        // Direct2D queues the drawing's every element until EndDraw: a big
        // SVG drawn many times in a frame queued hundreds of MB.
        rt->Flush();
    }
    // User space -> viewport (viewBox with preserveAspectRatio xMidYMid meet).
    D2D1_MATRIX_3X2_F view = D2D1::Matrix3x2F::Identity();
    if (vb_w_ > 0 && vb_h_ > 0) {
        const float s = std::min(width_ / vb_w_, height_ / vb_h_);
        const float ox = (width_ - vb_w_ * s) * 0.5f, oy = (height_ - vb_h_ * s) * 0.5f;
        view = D2D1::Matrix3x2F::Translation(-vb_x_, -vb_y_) * D2D1::Matrix3x2F::Scale(s, s) *
               D2D1::Matrix3x2F::Translation(ox, oy);
    }
    for (Image& im : images_) {
        const float iw = im.svg ? im.svg->width() : static_cast<float>(im.px_w);
        const float ih = im.svg ? im.svg->height() : static_cast<float>(im.px_h);
        const float w = im.w > 0 ? im.w : iw, h = im.h > 0 ? im.h : ih;
        if (iw <= 0 || ih <= 0 || w <= 0 || h <= 0) continue;
        // preserveAspectRatio="xMidYMid meet" (the default).
        const float s = std::min(w / iw, h / ih);
        const float left = im.x + (w - iw * s) * 0.5f, top = im.y + (h - ih * s) * 0.5f;
        const D2D1_RECT_F r = D2D1::RectF(left, top, left + iw * s, top + ih * s);
        rt->SetTransform(im.transform * view * place * old);
        if (im.svg) {
            im.svg->Draw(rt, r, dw);
            continue;
        }
        if (!im.bitmap) {
            const D2D1_BITMAP_PROPERTIES props =
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            rt->CreateBitmap(D2D1::SizeU(im.px_w, im.px_h), im.pixels.data(), im.px_w * 4, props, &im.bitmap);
        }
        if (im.bitmap) rt->DrawBitmap(im.bitmap.Get(), r, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    if (!texts_.empty() && dw) {
        for (const Text& t : texts_) {
            const std::wstring family = PickFamily(dw, t.family);
            ComPtr<IDWriteTextFormat> fmt;
            if (FAILED(dw->CreateTextFormat(family.c_str(), nullptr, static_cast<DWRITE_FONT_WEIGHT>(t.weight),
                                            t.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                                            DWRITE_FONT_STRETCH_NORMAL, std::max(t.size, 0.1f), L"", &fmt))) {
                continue;
            }
            ComPtr<IDWriteTextLayout> tl;
            if (FAILED(dw->CreateTextLayout(t.text.c_str(), static_cast<UINT32>(t.text.size()), fmt.Get(), 100000.f,
                                            100000.f, &tl))) {
                continue;
            }
            tl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TEXT_METRICS tm{};
            tl->GetMetrics(&tm);
            const float baseline = FirstLineBaseline(tl.Get());
            const float width = tm.widthIncludingTrailingWhitespace;
            float stretch = 1.f;
            if (t.text_length > 0 && width > 0) stretch = t.text_length / width;
            const float drawn = width * stretch;
            const float left = t.x - (t.anchor == 1 ? drawn * 0.5f : t.anchor == 2 ? drawn : 0.f);
            const D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Scale(stretch, 1.f) *
                                        D2D1::Matrix3x2F::Translation(left, t.y - baseline) * t.transform *
                                        view * place * old;
            rt->SetTransform(m);
            ComPtr<ID2D1SolidColorBrush> brush;
            const uint32_t c = t.argb;
            if (SUCCEEDED(rt->CreateSolidColorBrush(
                    D2D1::ColorF(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f,
                                 ((c >> 24) & 0xFF) / 255.f),
                    &brush))) {
                rt->DrawTextLayout(D2D1::Point2F(0, 0), tl.Get(), brush.Get(),
                                   D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            }
        }
    }
}

}  // namespace mdv

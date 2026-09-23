#include "core/diagram.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace mdv {
namespace {

// Mermaid's "default" and "dark" themes as GitHub shows them.
constexpr uint32_t kLight[kDgRoleCount] = {
    0x00000000,  // none
    0x00000000,  // explicit
    0xFF333333,  // text
    0xFF333333,  // line
    0xFFECECFF,  // node fill
    0xFF9370DB,  // node stroke
    0xFFFFFFDE,  // cluster fill
    0xFFAAAA33,  // cluster stroke
    0xCCE8E8E8,  // label background
    0xFFFFF5AD,  // note fill
    0xFFAAAA33,  // note stroke
    0xFF333333,  // note text
    0xFFECECFF,  // actor fill
    0xFF9370DB,  // actor stroke
    0xFFF4F4F4,  // activation fill
    0xFF666666,  // activation stroke
    0xFFECECFF,  // box tab fill
    0xFFFFFFFF,  // page
    0xFF6E7781,  // muted
    // categories
    0xFF8E7CE0, 0xFFF2C14E, 0xFF5FB49C, 0xFFE8736B, 0xFF4FA3E0, 0xFFB57EDC,
    0xFFF08A4B, 0xFF6CC3D5, 0xFFA3C76D, 0xFFE58FB8, 0xFF9BA7B4, 0xFFC7A36B,
};
constexpr uint32_t kDark[kDgRoleCount] = {
    0x00000000,  // none
    0x00000000,  // explicit
    0xFFE0DFDF,  // text
    0xFFD3D3D3,  // line
    0xFF1F2020,  // node fill
    0xFFCCCCCC,  // node stroke
    0xFF262C36,  // cluster fill
    0x55FFFFFF,  // cluster stroke
    0xCC585858,  // label background
    0xFF4A4533,  // note fill
    0xFFB8A95C,  // note stroke
    0xFFE0DFDF,  // note text
    0xFF1F2020,  // actor fill
    0xFFCCCCCC,  // actor stroke
    0xFF3A3F47,  // activation fill
    0xFFB0B0B0,  // activation stroke
    0xFF2D333B,  // box tab fill
    0xFF0D1117,  // page
    0xFF8B949E,  // muted
    // categories
    0xFF8E7CE0, 0xFFD9A93A, 0xFF4E9E88, 0xFFD2645C, 0xFF4592CB, 0xFFA66FCB,
    0xFFD9793E, 0xFF5BAFC0, 0xFF8FB25C, 0xFFCF7DA4, 0xFF8791A0, 0xFFB08F5A,
};

D2D1_COLOR_F ToColor(uint32_t argb) {
    return D2D1::ColorF(((argb >> 16) & 0xFF) / 255.f, ((argb >> 8) & 0xFF) / 255.f, (argb & 0xFF) / 255.f,
                        ((argb >> 24) & 0xFF) / 255.f);
}

bool Painted(const DgColor& c) { return c.role != kDgNone && (c.role != kDgArgb || (c.argb >> 24) != 0); }

uint32_t Resolve(const DgColor& c, bool dark) {
    if (c.role == kDgArgb) return c.argb;
    return c.role < kDgRoleCount ? (dark ? kDark : kLight)[c.role] : 0;
}

// Label text builder: styles toggled by tags and markdown markers.
struct LabelWriter {
    RichText out;
    bool bold = false, italic = false;
    bool space = false;  // a space is pending (collapsed)

    void Flush() {
        if (space && !out.text.empty() && out.text.back() != L'\n') Put(L' ');
        space = false;
    }
    void Put(wchar_t c) {
        if (out.spans.empty() || out.spans.back().bold != bold || out.spans.back().italic != italic ||
            out.spans.back().start + out.spans.back().length != out.text.size()) {
            out.spans.push_back(RichText::Span{static_cast<uint32_t>(out.text.size()), 0, bold, italic});
        }
        out.text.push_back(c);
        ++out.spans.back().length;
    }
    void Char(wchar_t c) {
        if (c == L'\n') {
            space = false;
            while (!out.text.empty() && out.text.back() == L' ') Unput();
            Put(L'\n');
            return;
        }
        if (IsSpace(c)) {
            space = true;
            return;
        }
        Flush();
        Put(c);
    }
    void Unput() {
        out.text.pop_back();
        if (!out.spans.empty() && --out.spans.back().length == 0) out.spans.pop_back();
    }
    void Text(std::wstring_view s) {
        for (wchar_t c : s) Char(c);
    }
};

}  // namespace

RichText ParseLabel(std::wstring_view raw, bool markdown) {
    LabelWriter w;
    for (size_t i = 0; i < raw.size();) {
        const wchar_t c = raw[i];
        // Entities: HTML's &name; and Mermaid's #name; / #123;
        if (c == L'&' || c == L'#') {
            const size_t semi = raw.find(L';', i + 1);
            if (semi != std::wstring_view::npos && semi - i <= 32 && semi > i + 1) {
                std::wstring ent(L"&");
                ent.append(raw.substr(i + 1, semi - i));
                if (c == L'#' && ent.size() > 2 && ent[1] >= L'0' && ent[1] <= L'9') ent.insert(1, 1, L'#');
                std::wstring decoded;
                if (AppendEntity(decoded, std::wstring_view(ent))) {
                    w.Text(decoded == L" " ? std::wstring(L" ") : decoded);
                    i = semi + 1;
                    continue;
                }
            }
        }
        if (c == L'<') {
            const size_t close = raw.find(L'>', i + 1);
            if (close != std::wstring_view::npos && close - i <= 200) {
                std::wstring tag;
                size_t k = i + 1;
                const bool end_tag = k < close && raw[k] == L'/';
                if (end_tag) ++k;
                while (k < close && (std::iswalnum(raw[k]))) tag.push_back(AsciiLower(raw[k++]));
                if (!tag.empty()) {
                    if (tag == L"br") {
                        w.Char(L'\n');
                    } else if (tag == L"b" || tag == L"strong") {
                        w.Flush();
                        w.bold = !end_tag;
                    } else if (tag == L"i" || tag == L"em") {
                        w.Flush();
                        w.italic = !end_tag;
                    } else if (tag == L"p" || tag == L"div") {
                        if (end_tag) w.Char(L'\n');
                    }
                    // Other tags (span, a, font...) only lose their markup.
                    i = close + 1;
                    continue;
                }
            }
        }
        if (markdown && (c == L'*' || c == L'_')) {
            const bool dbl = i + 1 < raw.size() && raw[i + 1] == c;
            const size_t len = dbl ? 2 : 1;
            // A marker next to a letter or space on the right side opens, the
            // mirror closes; stray markers stay text.
            const bool can_open = i + len < raw.size() && !IsSpace(raw[i + len]);
            const bool can_close = i > 0 && !IsSpace(raw[i - 1]);
            bool& flag = dbl ? w.bold : w.italic;
            if ((flag && can_close) || (!flag && can_open)) {
                const std::wstring_view rest = raw.substr(i + len);
                const bool closes_later = flag || rest.find(std::wstring(len, c)) != std::wstring_view::npos;
                if (closes_later) {
                    w.Flush();
                    flag = !flag;
                    i += len;
                    continue;
                }
            }
        }
        if (c == L'\r') {
            ++i;
            continue;
        }
        w.Char(markdown || c != L'\n' ? c : L' ');
        ++i;
    }
    while (!w.out.text.empty() && (w.out.text.back() == L' ' || w.out.text.back() == L'\n')) w.Unput();
    size_t lead = 0;
    while (lead < w.out.text.size() && w.out.text[lead] == L'\n') ++lead;
    if (lead) {
        RichText t;
        t.text = w.out.text.substr(lead);
        for (RichText::Span s : w.out.spans) {
            if (s.start + s.length <= lead) continue;
            const uint32_t a = std::max<uint32_t>(s.start, static_cast<uint32_t>(lead));
            s.length = s.start + s.length - a;
            s.start = a - static_cast<uint32_t>(lead);
            t.spans.push_back(s);
        }
        return t;
    }
    return w.out;
}

// ---------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------

SceneBuilder::SceneBuilder(IDWriteFactory* dw, const std::wstring& family, float size)
    : dw_(dw), family_(family), size_(size), scene_(std::make_shared<DiagramScene>()) {}

std::shared_ptr<DiagramScene> SceneBuilder::Finish(float width, float height) {
    scene_->width = std::max(width, 1.f);
    scene_->height = std::max(height, 1.f);
    return std::move(scene_);
}

void SceneBuilder::Rect(const D2D1_RECT_F& r, float radius, DgColor fill, DgColor stroke, float stroke_width,
                        uint8_t dash) {
    DiagramScene::Shape s;
    s.kind = DiagramScene::Shape::Kind::Rect;
    s.box = r;
    s.rx = s.ry = radius;
    s.fill = fill;
    s.stroke = stroke;
    s.stroke_width = stroke_width;
    s.dash = dash;
    scene_->items.push_back({false, static_cast<uint32_t>(scene_->shapes.size())});
    scene_->shapes.push_back(s);
}

void SceneBuilder::Ellipse(const D2D1_RECT_F& r, DgColor fill, DgColor stroke, float stroke_width, uint8_t dash) {
    DiagramScene::Shape s;
    s.kind = DiagramScene::Shape::Kind::Ellipse;
    s.box = r;
    s.fill = fill;
    s.stroke = stroke;
    s.stroke_width = stroke_width;
    s.dash = dash;
    scene_->items.push_back({false, static_cast<uint32_t>(scene_->shapes.size())});
    scene_->shapes.push_back(s);
}

void SceneBuilder::Begin(D2D1_POINT_2F p) {
    path_first_ = static_cast<uint32_t>(scene_->ops.size());
    path_first_point_ = static_cast<uint32_t>(scene_->points.size());
    scene_->ops.push_back(DiagramScene::Op::Move);
    scene_->points.push_back(p);
}

void SceneBuilder::LineTo(D2D1_POINT_2F p) {
    scene_->ops.push_back(DiagramScene::Op::Line);
    scene_->points.push_back(p);
}

void SceneBuilder::CubicTo(D2D1_POINT_2F c1, D2D1_POINT_2F c2, D2D1_POINT_2F p) {
    scene_->ops.push_back(DiagramScene::Op::Cubic);
    scene_->points.push_back(c1);
    scene_->points.push_back(c2);
    scene_->points.push_back(p);
}

void SceneBuilder::Close() { scene_->ops.push_back(DiagramScene::Op::Close); }

void SceneBuilder::End(DgColor fill, DgColor stroke, float stroke_width, uint8_t dash) {
    DiagramScene::Shape s;
    s.kind = DiagramScene::Shape::Kind::Path;
    s.first = path_first_;
    s.count = static_cast<uint32_t>(scene_->ops.size()) - path_first_;
    s.first_point = path_first_point_;
    s.fill = fill;
    s.stroke = stroke;
    s.stroke_width = stroke_width;
    s.dash = dash;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (size_t i = path_first_point_; i < scene_->points.size(); ++i) {
        const D2D1_POINT_2F& p = scene_->points[i];
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    s.box = D2D1::RectF(x0, y0, x1, y1);
    scene_->items.push_back({false, static_cast<uint32_t>(scene_->shapes.size())});
    scene_->shapes.push_back(s);
}

void SceneBuilder::Polygon(const std::vector<D2D1_POINT_2F>& pts, DgColor fill, DgColor stroke, float stroke_width,
                           uint8_t dash) {
    if (pts.empty()) return;
    Begin(pts[0]);
    for (size_t i = 1; i < pts.size(); ++i) LineTo(pts[i]);
    Close();
    End(fill, stroke, stroke_width, dash);
}

IDWriteTextFormat* SceneBuilder::Format(bool bold, float scale) {
    for (const FormatKey& f : formats_) {
        if (f.bold == bold && f.scale == scale) return f.format.Get();
    }
    ComPtr<IDWriteTextFormat> fmt;
    if (!dw_ || FAILED(dw_->CreateTextFormat(family_.c_str(), nullptr,
                                             bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size_ * scale, L"",
                                             &fmt))) {
        return nullptr;
    }
    formats_.push_back(FormatKey{bold, scale, fmt});
    return fmt.Get();
}

SceneBuilder::Measured SceneBuilder::Measure(const RichText& t, float wrap, bool bold, float scale, bool center) {
    Measured m;
    IDWriteTextFormat* fmt = Format(bold, scale);
    if (!fmt) return m;
    const std::wstring& text = t.text.empty() ? std::wstring(L" ") : t.text;
    if (FAILED(dw_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), fmt, wrap > 0 ? wrap : 100000.f,
                                     100000.f, &m.layout))) {
        return m;
    }
    m.layout->SetWordWrapping(wrap > 0 ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    const float line = LineHeight(scale);
    m.layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, line, line * 0.78f);
    for (const RichText::Span& s : t.spans) {
        const DWRITE_TEXT_RANGE r{s.start, s.length};
        if (s.bold) m.layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
        if (s.italic) m.layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, r);
    }
    DWRITE_TEXT_METRICS tm{};
    m.layout->GetMetrics(&tm);
    m.w = std::ceil(tm.width);
    m.h = std::max(tm.height, line);
    // Lines centered in their own box.
    m.layout->SetMaxWidth(std::max(m.w, 1.f));
    m.layout->SetTextAlignment(center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    return m;
}

void SceneBuilder::Text(Measured m, D2D1_POINT_2F origin, DgColor color, uint32_t src_start, uint32_t src_length) {
    if (!m.layout) return;
    DiagramScene::Text t;
    t.layout = std::move(m.layout);
    t.origin = origin;
    t.w = m.w;
    t.h = m.h;
    t.color = color;
    t.src_start = src_start;
    t.src_length = src_length;
    scene_->items.push_back({true, static_cast<uint32_t>(scene_->texts.size())});
    scene_->texts.push_back(std::move(t));
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

uint32_t DiagramRoleArgb(uint8_t role, bool dark) { return Resolve(Role(role), dark); }

void DrawDiagram(ID2D1RenderTarget* rt, const DiagramScene& s, const D2D1_RECT_F& dest, bool dark,
                 const std::vector<DiagramHighlight>* highlights, ID2D1Brush* const highlight_brushes[3]) {
    if (!rt || s.width <= 0 || s.height <= 0) return;
    const float k = std::min((dest.right - dest.left) / s.width, (dest.bottom - dest.top) / s.height);
    if (!(k > 0) || !std::isfinite(k)) return;
    D2D1_MATRIX_3X2_F world;
    rt->GetTransform(&world);
    const D2D1_MATRIX_3X2_F place = D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(dest.left, dest.top);
    rt->SetTransform(place * world);

    // What of the scene is on the target (scene coordinates).
    const D2D1_MATRIX_3X2_F m = place * world;
    D2D1_RECT_F visible = D2D1::RectF(-1e30f, -1e30f, 1e30f, 1e30f);
    if (m._12 == 0 && m._21 == 0 && m._11 > 0 && m._22 > 0) {
        const D2D1_SIZE_F size = rt->GetSize();
        visible = D2D1::RectF(-m._31 / m._11, -m._32 / m._22, (size.width - m._31) / m._11, (size.height - m._32) / m._22);
    }
    auto shown = [&visible](const D2D1_RECT_F& r, float margin) {
        return r.right + margin >= visible.left && r.left - margin <= visible.right && r.bottom + margin >= visible.top &&
               r.top - margin <= visible.bottom;
    };

    ComPtr<ID2D1Factory> factory;
    rt->GetFactory(&factory);
    if (s.geo_factory.Get() != factory.Get()) {
        s.geos.clear();
        s.geo_factory = factory;
    }
    s.geos.resize(s.shapes.size());
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0), &brush))) {
        rt->SetTransform(world);
        return;
    }
    struct Stroke {
        uint8_t dash;
        float width;
        ComPtr<ID2D1StrokeStyle> style;
    };
    std::vector<Stroke> strokes;
    auto stroke_style = [&](uint8_t dash, float width) -> ID2D1StrokeStyle* {
        for (const Stroke& st : strokes) {
            if (st.dash == dash && st.width == width) return st.style.Get();
        }
        D2D1_STROKE_STYLE_PROPERTIES p = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                                                     D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_ROUND);
        ComPtr<ID2D1StrokeStyle> style;
        if (dash == 0) {
            factory->CreateStrokeStyle(p, nullptr, 0, &style);
        } else {
            // Dash lengths in units of the stroke's width.
            const float w = std::max(width, 0.1f);
            const float dashes[2] = {(dash == 1 ? 5.f : 2.f) / w, (dash == 1 ? 4.f : 3.f) / w};
            p.dashStyle = D2D1_DASH_STYLE_CUSTOM;
            factory->CreateStrokeStyle(p, dashes, 2, &style);
        }
        strokes.push_back(Stroke{dash, width, style});
        return style.Get();
    };
    auto paint = [&](const DgColor& c) -> ID2D1Brush* {
        brush->SetColor(ToColor(Resolve(c, dark)));
        return brush.Get();
    };

    for (const DiagramScene::Item& item : s.items) {
        if (item.text) {
            const DiagramScene::Text& t = s.texts[item.index];
            const D2D1_RECT_F box = D2D1::RectF(t.origin.x, t.origin.y, t.origin.x + t.w, t.origin.y + t.h);
            if (!t.layout || !shown(box, 4)) continue;
            if (highlights && highlight_brushes && t.src_length > 0) {
                // The current hit over other hits over the selection.
                int best = -1, kind = -1;
                for (const DiagramHighlight& h : *highlights) {
                    if (h.start >= t.src_start + t.src_length || h.end <= t.src_start || h.kind > 2) continue;
                    const int priority = h.kind == 1 ? 2 : (h.kind == 0 ? 1 : 0);
                    if (priority > best) {
                        best = priority;
                        kind = h.kind;
                    }
                }
                if (kind >= 0 && highlight_brushes[kind]) {
                    rt->FillRectangle(D2D1::RectF(box.left - 3, box.top, box.right + 3, box.bottom), highlight_brushes[kind]);
                }
            }
            rt->DrawTextLayout(t.origin, t.layout.Get(), paint(t.color), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            continue;
        }
        const DiagramScene::Shape& sh = s.shapes[item.index];
        if (!shown(sh.box, sh.stroke_width + 2)) continue;
        const bool fill = Painted(sh.fill), stroke = Painted(sh.stroke) && sh.stroke_width > 0;
        if (!fill && !stroke) continue;
        switch (sh.kind) {
        case DiagramScene::Shape::Kind::Rect: {
            const D2D1_ROUNDED_RECT rr{sh.box, sh.rx, sh.ry};
            if (fill) {
                if (sh.rx > 0) rt->FillRoundedRectangle(rr, paint(sh.fill));
                else rt->FillRectangle(sh.box, paint(sh.fill));
            }
            if (stroke) {
                ID2D1StrokeStyle* st = stroke_style(sh.dash, sh.stroke_width);
                if (sh.rx > 0) rt->DrawRoundedRectangle(rr, paint(sh.stroke), sh.stroke_width, st);
                else rt->DrawRectangle(sh.box, paint(sh.stroke), sh.stroke_width, st);
            }
            break;
        }
        case DiagramScene::Shape::Kind::Ellipse: {
            const D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F((sh.box.left + sh.box.right) / 2, (sh.box.top + sh.box.bottom) / 2),
                                                 (sh.box.right - sh.box.left) / 2, (sh.box.bottom - sh.box.top) / 2);
            if (fill) rt->FillEllipse(e, paint(sh.fill));
            if (stroke) rt->DrawEllipse(e, paint(sh.stroke), sh.stroke_width, stroke_style(sh.dash, sh.stroke_width));
            break;
        }
        case DiagramScene::Shape::Kind::Path: {
            ComPtr<ID2D1PathGeometry>& geo = s.geos[item.index];
            if (!geo) {
                ComPtr<ID2D1PathGeometry> built;
                ComPtr<ID2D1GeometrySink> sink;
                if (factory && SUCCEEDED(factory->CreatePathGeometry(&built)) && SUCCEEDED(built->Open(&sink))) {
                    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
                    bool open = false;
                    size_t p = sh.first_point;
                    for (uint32_t i = 0; i < sh.count; ++i) {
                        switch (s.ops[sh.first + i]) {
                        case DiagramScene::Op::Move:
                            if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
                            sink->BeginFigure(s.points[p++], D2D1_FIGURE_BEGIN_FILLED);
                            open = true;
                            break;
                        case DiagramScene::Op::Line:
                            sink->AddLine(s.points[p++]);
                            break;
                        case DiagramScene::Op::Cubic:
                            sink->AddBezier(D2D1::BezierSegment(s.points[p], s.points[p + 1], s.points[p + 2]));
                            p += 3;
                            break;
                        case DiagramScene::Op::Close:
                            if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                            open = false;
                            break;
                        }
                    }
                    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    if (SUCCEEDED(sink->Close())) geo = built;
                }
            }
            if (!geo) break;
            if (fill) rt->FillGeometry(geo.Get(), paint(sh.fill));
            if (stroke) rt->DrawGeometry(geo.Get(), paint(sh.stroke), sh.stroke_width, stroke_style(sh.dash, sh.stroke_width));
            break;
        }
        }
    }
    rt->SetTransform(world);
}

}  // namespace mdv

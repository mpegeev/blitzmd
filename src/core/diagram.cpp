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
    0xFFE8E8E8,  // label background
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
    0x00000000,  // tint (explicit)
    // categories: Mermaid's pie1..pie12 (the default theme derives them from
    // its primary, secondary and tertiary colors)
    0xFFECECFF, 0xFFFFFFDE, 0xFFB5FF20, 0xFFB9B9FF, 0xFFFFFF45, 0xFFD7FF86,
    0xFFFF86FF, 0xFF20FFFF, 0xFFFF2020, 0xFFFF20FF, 0xFF20FF8F, 0xFFFF5353,
    // the same, not blended
    0xFFECECFF, 0xFFFFFFDE, 0xFFB5FF20, 0xFFB9B9FF, 0xFFFFFF45, 0xFFD7FF86,
    0xFFFF86FF, 0xFF20FFFF, 0xFFFF2020, 0xFFFF20FF, 0xFF20FF8F, 0xFFFF5353,
    // fillType0..7
    0xFFECECFF, 0xFFFFFFDE, 0xFFFFECFE, 0xFFDEFFE0, 0xFFECFFFE, 0xFFFFDEE0, 0xFFFFEFEC, 0xFFDEFBFF,
    // cScale0..11, text on them, their inverse (default theme)
    0xFF8686FF, 0xFFFFFF78, 0xFFD7FF86, 0xFFC286FF, 0xFFFF86FF, 0xFFFF86C2,
    0xFFFF8686, 0xFFFFC286, 0xFFC2FF86, 0xFF86FFC2, 0xFF86FFFF, 0xFF86C2FF,
    0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFF000000,
    0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFFFFFB9, 0xFFABABFF, 0xFFD0B9FF, 0xFFDCFFB9, 0xFFB9FFB9, 0xFFB9FFDC,
    0xFFB9FFFF, 0xFFB9DCFF, 0xFFDCB9FF, 0xFFFFB9DC, 0xFFFFB9B9, 0xFFFFDCB9,
    // git0..7 and text on them
    0xFF0000EC, 0xFFDEDE00, 0xFF9DEC00, 0xFF0076EC, 0xFF00ECEC, 0xFF00EC76, 0xFFEC00EC, 0xFFEC0000,
    0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000,
    // xychart plot colors
    0xFFECECFF, 0xFF8493A6, 0xFFFFC3A0, 0xFFDCDDE1, 0xFFB8E994,
    0xFFD1A36F, 0xFFC3CDE6, 0xFFFFB6C1, 0xFF496078, 0xFFF8F3E3,
    // quadrant chart fills (quadrant 1 to 4) and borders
    0xFFECECFF, 0xFFF1F1FF, 0xFFF6F6FF, 0xFFFBFBFF, 0xFFC7C7F1,
    // cScale brightened (computed)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // gitInv0..7
    0xFF131300, 0xFF0000A1, 0xFF310093, 0xFF934900, 0xFF930000, 0xFF930049, 0xFF009300, 0xFF009393,
    // cScalePeer0..11
    0xFF3939FF, 0xFFF7F700, 0xFFB5FF20, 0xFF9C39FF, 0xFFFF39FF, 0xFFFF399C,
    0xFFFF3939, 0xFFFF9C39, 0xFF9CFF39, 0xFF39FF9C, 0xFF39FFFF, 0xFF399CFF,
    // cScale lighter (computed)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
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
    0xFF4A4A4A,  // label background
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
    0x00000000,  // tint (explicit)
    // categories: the dark theme's pie1..pie12
    0xFF0B0000, 0xFF4D1037, 0xFF3F5258, 0xFF4F2F1B, 0xFF6E0A0A, 0xFF3B0048,
    0xFF995A01, 0xFF154706, 0xFF161722, 0xFF00296F, 0xFF01629C, 0xFF010029,
    // the same, not blended
    0xFF0B0000, 0xFF4D1037, 0xFF3F5258, 0xFF4F2F1B, 0xFF6E0A0A, 0xFF3B0048,
    0xFF995A01, 0xFF154706, 0xFF161722, 0xFF00296F, 0xFF01629C, 0xFF010029,
    // fillType0..7
    0xFF1F2020, 0xFF474949, 0xFF1F1F20, 0xFF474749, 0xFF1F201F, 0xFF474947, 0xFF201F20, 0xFF494749,
    // cScale0..11, text on them, their inverse (dark theme)
    0xFF1F2020, 0xFF0B0000, 0xFF4D1037, 0xFF3F5258, 0xFF4F2F1B, 0xFF6E0A0A,
    0xFF3B0048, 0xFF995A01, 0xFF154706, 0xFF161722, 0xFF00296F, 0xFF01629C,
    0xFFD3D3D3, 0xFFD3D3D3, 0xFFD3D3D3, 0xFFD3D3D3, 0xFFD3D3D3, 0xFFD3D3D3,
    0xFFD3D3D3, 0xFFD3D3D3, 0xFFD3D3D3, 0xFFD3D3D3, 0xFFD3D3D3, 0xFFD3D3D3,
    0xFFE0DFDF, 0xFFF4FFFF, 0xFFB2EFC8, 0xFFC0ADA7, 0xFFB0D0E4, 0xFF91F5F5,
    0xFFC4FFB7, 0xFF66A5FE, 0xFFEAB8F9, 0xFFE9E8DD, 0xFFFFD690, 0xFFFE9D63,
    // git0..7 and text on them
    0xFF797D7D, 0xFFA12273, 0xFF6A8993, 0xFF9B5C35, 0xFFCB1313, 0xFF65007B, 0xFFCC7801, 0xFF31A50E,
    0xFF2C2C2C, 0xFFD3D3D3, 0xFFD3D3D3, 0xFF2C2C2C, 0xFFD3D3D3, 0xFFD3D3D3, 0xFFD3D3D3, 0xFFD3D3D3,
    // xychart plot colors
    0xFF3498DB, 0xFF2ECC71, 0xFFE74C3C, 0xFFF1C40F, 0xFFBDC3C7,
    0xFFFFFFFF, 0xFF34495E, 0xFF9B59B6, 0xFF1ABC9C, 0xFFE67E22,
    // quadrant chart fills (quadrant 1 to 4) and borders
    0xFF1F2020, 0xFF242525, 0xFF292A2A, 0xFF2E2F2F, 0xFFCCCCCC,
    // cScale brightened (computed)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // gitInv0..7
    0xFF868282, 0xFF5EDD8C, 0xFF95766C, 0xFF64A3CA, 0xFF34EDED, 0xFF9AFF84, 0xFF3387FE, 0xFFCE5AF1,
    // cScalePeer0..11
    0xFF383A3A, 0xFF3E0000, 0xFF771955, 0xFF546E76, 0xFF754628, 0xFF9D0E0E,
    0xFF65007B, 0xFFCC7801, 0xFF23760A, 0xFF2A2C41, 0xFF003CA2, 0xFF0182CF,
    // cScale lighter (computed)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

D2D1_COLOR_F ToColor(uint32_t argb) {
    return D2D1::ColorF(((argb >> 16) & 0xFF) / 255.f, ((argb >> 8) & 0xFF) / 255.f, (argb & 0xFF) / 255.f,
                        ((argb >> 24) & 0xFF) / 255.f);
}

bool Painted(const DgColor& c) {
    return c.role != kDgNone && ((c.role != kDgArgb && c.role != kDgTint) || (c.argb >> 24) != 0);
}

uint32_t ResolveRole(const DgColor& c, bool dark);

// khroma's lighten: the HSL lightness raised by `amount` (0 to 1).
uint32_t Lighten(uint32_t argb, float amount) {
    const float r = ((argb >> 16) & 0xFF) / 255.f, g = ((argb >> 8) & 0xFF) / 255.f, b = (argb & 0xFF) / 255.f;
    const float hi = std::max({r, g, b}), lo = std::min({r, g, b}), l = (hi + lo) / 2, d = hi - lo;
    float h = 0, s = 0;
    if (d > 0) {
        s = l > 0.5f ? d / (2 - hi - lo) : d / (hi + lo);
        if (hi == r) h = (g - b) / d + (g < b ? 6.f : 0.f);
        else if (hi == g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        h /= 6;
    }
    const float l2 = std::min(l + amount, 1.f);
    auto hue = [](float p, float q, float t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.f / 6) return p + (q - p) * 6 * t;
        if (t < 0.5f) return q;
        if (t < 2.f / 3) return p + (q - p) * (2.f / 3 - t) * 6;
        return p;
    };
    float r2 = l2, g2 = l2, b2 = l2;
    if (s > 0) {
        const float q = l2 < 0.5f ? l2 * (1 + s) : l2 + s - l2 * s, p = 2 * l2 - q;
        r2 = hue(p, q, h + 1.f / 3);
        g2 = hue(p, q, h);
        b2 = hue(p, q, h - 1.f / 3);
    }
    auto byte = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.f, 1.f) * 255 + 0.5f); };
    return (argb & 0xFF000000) | (byte(r2) << 16) | (byte(g2) << 8) | byte(b2);
}

// A theme role at an opacity (RoleAt: its argb's alpha), or as it is.
uint32_t Resolve(const DgColor& c, bool dark) {
    const uint32_t v = ResolveRole(c, dark);
    if (c.role == kDgArgb || c.role == kDgTint || (c.argb >> 24) == 0) return v;
    return (v & 0xFFFFFF) | ((((v >> 24) * (c.argb >> 24) + 127) / 255) << 24);
}

uint32_t ResolveRole(const DgColor& c, bool dark) {
    if (c.role == kDgArgb) return c.argb;
    // Light backgrounds picked for a white page would glare on a dark one
    // (and hide light text): a quarter of the color there.
    if (c.role == kDgTint) return dark ? (c.argb & 0xFFFFFF) | (std::min(c.argb >> 24, 0x40u) << 24) : c.argb;
    if (c.role >= kDgRoleCount) return 0;
    const uint32_t* table = dark ? kDark : kLight;
    if (c.role >= kDgScaleBright && c.role < kDgGitInv) {
        // (Mermaid's CSS filter: brightness(120%).)
        const uint32_t v = table[kDgScale + c.role - kDgScaleBright];
        uint32_t out = 0xFF000000;
        for (int shift = 0; shift <= 16; shift += 8) out |= std::min(((v >> shift) & 0xFF) * 6 / 5, 255u) << shift;
        return out;
    }
    if (c.role >= kDgScaleLighter) return Lighten(table[kDgScale + c.role - kDgScaleLighter], 0.1f);
    if (c.role < kDgCategory || c.role >= kDgCategorySolid) return table[c.role];
    // Categories as Mermaid paints pie slices: at 70% over the page.
    const uint32_t v = table[c.role], page = table[kDgPage];
    uint32_t out = 0xFF000000;
    for (int shift = 0; shift <= 16; shift += 8) {
        const uint32_t a = (v >> shift) & 0xFF, b = (page >> shift) & 0xFF;
        out |= ((a * 7 + b * 3 + 5) / 10) << shift;
    }
    return out;
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
        // Font Awesome icons (fa:fa-car, fab:fa-github): GitHub has no icon
        // font for them either.
        if (c == L'f' && (i == 0 || IsSpace(raw[i - 1]))) {
            size_t k = i + 2;
            if (raw.substr(i, 2) == L"fa") {
                while (k < raw.size() && std::iswalpha(raw[k]) && k < i + 4) ++k;
                if (raw.substr(k, 4) == L":fa-") {
                    k += 4;
                    while (k < raw.size() && (std::iswalnum(raw[k]) || raw[k] == L'-')) ++k;
                    i = k;
                    continue;
                }
            }
        }
        // Entities: HTML's &name; and Mermaid's #name; / #123;
        if (c == L'&' || c == L'#') {
            const size_t semi = raw.find(L';', i + 1);
            if (semi != std::wstring_view::npos && semi - i <= 32 && semi > i + 1) {
                std::wstring ent(L"&");
                ent.append(raw.substr(i + 1, semi - i));
                if (c == L'#' && ent.size() > 2 && ent[1] >= L'0' && ent[1] <= L'9') ent.insert(1, 1, L'#');
                std::wstring decoded;
                if (AppendEntity(decoded, std::wstring_view(ent))) {
                    w.Text(decoded);
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
            bool can_open = i + len < raw.size() && !IsSpace(raw[i + len]);
            bool can_close = i > 0 && !IsSpace(raw[i - 1]);
            if (c == L'_') {  // not inside words (snake_case_name), as in CommonMark
                if (i > 0 && std::iswalnum(raw[i - 1])) can_open = false;
                if (i + len < raw.size() && std::iswalnum(raw[i + len])) can_close = false;
            }
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

std::shared_ptr<DiagramScene> SceneBuilder::FinishFitted(float margin) {
    DiagramScene& s = *scene_;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    auto take = [&](const D2D1_RECT_F& r, float out) {
        x0 = std::min(x0, r.left - out);
        y0 = std::min(y0, r.top - out);
        x1 = std::max(x1, r.right + out);
        y1 = std::max(y1, r.bottom + out);
    };
    for (const DiagramScene::Shape& sh : s.shapes) take(sh.box, Painted(sh.stroke) ? sh.stroke_width / 2 : 0.f);
    for (const DiagramScene::Text& t : s.texts) take(D2D1::RectF(t.origin.x, t.origin.y, t.origin.x + t.w, t.origin.y + t.h), 0);
    if (x0 > x1) x0 = y0 = x1 = y1 = 0;
    // Everything moved to start at the margin.
    const float dx = margin - x0, dy = margin - y0;
    for (DiagramScene::Shape& sh : s.shapes) sh.box = D2D1::RectF(sh.box.left + dx, sh.box.top + dy, sh.box.right + dx, sh.box.bottom + dy);
    for (D2D1_POINT_2F& p : s.points) p = D2D1::Point2F(p.x + dx, p.y + dy);
    for (DiagramScene::Text& t : s.texts) {
        t.origin = D2D1::Point2F(t.origin.x + dx, t.origin.y + dy);
        t.anchor = D2D1::Point2F(t.anchor.x + dx, t.anchor.y + dy);
    }
    return Finish(x1 - x0 + 2 * margin, y1 - y0 + 2 * margin);
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

void SceneBuilder::MoveTo(D2D1_POINT_2F p) {
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
        if (s.underline) m.layout->SetUnderline(TRUE, r);
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

void SceneBuilder::TextUp(Measured m, D2D1_POINT_2F origin, DgColor color, uint32_t src_start, uint32_t src_length) {
    const float w = m.w;
    TextTurned(std::move(m), D2D1::Point2F(origin.x, origin.y + w), -90.f, color, src_start, src_length);
}

void SceneBuilder::TextTurned(Measured m, D2D1_POINT_2F anchor, float angle, DgColor color, uint32_t src_start,
                              uint32_t src_length) {
    if (!m.layout) return;
    // Its bounds: the corners turned.
    const float a = angle * 3.14159265f / 180.f, c = std::cos(a), s = std::sin(a);
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (const float px : {0.f, m.w}) {
        for (const float py : {0.f, m.h}) {
            const float x = anchor.x + px * c - py * s, y = anchor.y + px * s + py * c;
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
    }
    m.w = x1 - x0;
    m.h = y1 - y0;
    Text(std::move(m), D2D1::Point2F(x0, y0), color, src_start, src_length);
    scene_->texts.back().angle = angle;
    scene_->texts.back().anchor = anchor;
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
    // Nothing outside the scene's box (values past an axis, a long legend...).
    rt->SetTransform(world);
    rt->PushAxisAlignedClip(D2D1::RectF(dest.left - 2, dest.top - 2, dest.left + s.width * k + 2, dest.top + s.height * k + 2),
                            D2D1_ANTIALIAS_MODE_ALIASED);
    rt->SetTransform(place * world);
    struct Stroke {
        uint8_t dash;
        float width;
        ComPtr<ID2D1StrokeStyle> style;
    };
    std::vector<Stroke> strokes;
    auto stroke_style = [&](uint8_t dash, float width) -> ID2D1StrokeStyle* {
        // Gaps under a pixel look solid anyway, and cost dearly to draw.
        if (k < 0.2f) dash = 0;
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
                    if (t.angle != 0) {
                        // Along the turned text, padded at its ends.
                        DWRITE_TEXT_METRICS tm{};
                        t.layout->GetMetrics(&tm);
                        rt->SetTransform(D2D1::Matrix3x2F::Rotation(t.angle) * D2D1::Matrix3x2F::Translation(t.anchor.x, t.anchor.y) * place * world);
                        rt->FillRectangle(D2D1::RectF(tm.left - 3, tm.top, tm.left + tm.width + 3, tm.top + tm.height), highlight_brushes[kind]);
                        rt->SetTransform(place * world);
                    } else {
                        rt->FillRectangle(D2D1::RectF(box.left - 3, box.top, box.right + 3, box.bottom), highlight_brushes[kind]);
                    }
                }
            }
            if (t.angle != 0) {
                rt->SetTransform(D2D1::Matrix3x2F::Rotation(t.angle) * D2D1::Matrix3x2F::Translation(t.anchor.x, t.anchor.y) * place * world);
                rt->DrawTextLayout(D2D1::Point2F(0, 0), t.layout.Get(), paint(t.color), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
                rt->SetTransform(place * world);
            } else {
                rt->DrawTextLayout(t.origin, t.layout.Get(), paint(t.color), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            }
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
    rt->PopAxisAlignedClip();
    rt->SetTransform(world);
}

}  // namespace mdv

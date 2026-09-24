#include "core/mermaid.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <new>

#include "core/mermaid_internal.h"

namespace mdv {
namespace mermaid {

// ---------------------------------------------------------------------------
// Source
// ---------------------------------------------------------------------------

bool IsWordChar(wchar_t c) {
    // Non-ASCII letters too, but not the Unicode spaces (no-break, em...).
    if (c >= 0x80) return c != 0xA0 && c != 0x3000 && c != 0xFEFF && !(c >= 0x2000 && c <= 0x200B);
    return c == L'_' || std::iswalnum(c);
}

Source Preprocess(std::wstring_view text) {
    Source src;
    src.text = text;
    // Front matter: a "---" line first, up to the next "---" line.
    size_t i = 0;
    while (i < text.size() && IsSpace(text[i])) ++i;
    auto line_at = [&](size_t p, size_t* next) {
        size_t e = text.find(L'\n', p);
        if (e == std::wstring_view::npos) e = text.size();
        *next = e < text.size() ? e + 1 : e;
        return Trim(text.substr(p, e - p));
    };
    size_t next = 0;
    if (i < text.size() && line_at(i, &next) == L"---") {
        size_t p = next;
        while (p < text.size()) {
            size_t after = 0;
            const size_t line_start = p;
            const std::wstring_view line = line_at(p, &after);
            if (line == L"---") {
                src.body = after;
                return src;
            }
            // Only a top-level title matters here (config is ignored).
            const std::wstring_view raw = text.substr(line_start, after - line_start);
            if (raw.starts_with(L"title:")) {
                std::wstring_view t = Trim(raw.substr(6));
                if (t.size() >= 2 && (t.front() == L'"' || t.front() == L'\'') && t.back() == t.front()) {
                    t = t.substr(1, t.size() - 2);
                }
                src.title = t;
                src.title_pos = static_cast<uint32_t>(t.data() - text.data());
                src.title_len = static_cast<uint32_t>(t.size());
            }
            if (after == p) break;
            p = after;
        }
        src.title.clear();  // not closed: not front matter
    }
    return src;
}

void Cursor::SkipBlanks() {
    while (pos_ < s_.size() && (s_[pos_] == L' ' || s_[pos_] == L'\t' || s_[pos_] == 0xA0)) ++pos_;
}

void Cursor::SkipStatementGap() {
    for (;;) {
        while (pos_ < s_.size() && (IsSpace(s_[pos_]) || s_[pos_] == L';' || s_[pos_] == 0xA0)) ++pos_;
        if (Rest().starts_with(L"%%{")) {
            const size_t e = s_.find(L"}%%", pos_ + 3);
            pos_ = e == std::wstring_view::npos ? s_.size() : e + 3;
            continue;
        }
        if (Rest().starts_with(L"%%")) {
            SkipToLineEnd();
            continue;
        }
        break;
    }
}

bool Cursor::AtStatementEnd() const {
    const wchar_t c = Peek();
    return AtEnd() || c == L'\n' || c == L'\r' || c == L';' || (c == L'%' && Peek(1) == L'%');
}

void Cursor::SkipToLineEnd() {
    while (pos_ < s_.size() && s_[pos_] != L'\n') ++pos_;
}

std::wstring_view Cursor::LineRest() {
    const size_t start = pos_;
    while (!AtStatementEnd()) ++pos_;
    return Trim(s_.substr(start, pos_ - start));
}

bool Cursor::Eat(wchar_t c) {
    if (Peek() != c || AtEnd()) return false;
    ++pos_;
    return true;
}

bool Cursor::EatStr(std::wstring_view str) {
    if (!Rest().starts_with(str)) return false;
    pos_ += str.size();
    return true;
}

bool Cursor::EatKeyword(std::wstring_view word, bool match_case) {
    const std::wstring_view r = Rest();
    if (r.size() < word.size()) return false;
    if (match_case ? r.substr(0, word.size()) != word : !EqualsNoCase(r.substr(0, word.size()), word)) return false;
    if (r.size() > word.size() && (IsWordChar(r[word.size()]) || r[word.size()] == L'-')) return false;
    pos_ += word.size();
    return true;
}

std::wstring FormatError(std::wstring_view source, const ParseError& e) {
    if (e.pos == kNoLine) return e.message;
    const size_t p = std::min(e.pos, source.size());
    const size_t line = 1 + static_cast<size_t>(std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(p), L'\n'));
    return std::wstring(Tr(L"Line ", L"Строка ")) + std::to_wstring(line) + L": " + e.message;
}

// ---------------------------------------------------------------------------
// CSS
// ---------------------------------------------------------------------------

namespace {

struct NamedColor {
    const wchar_t* name;
    uint32_t rgb;
};
constexpr NamedColor kColors[] = {
    {L"aliceblue", 0xF0F8FF}, {L"antiquewhite", 0xFAEBD7}, {L"aqua", 0x00FFFF}, {L"aquamarine", 0x7FFFD4},
    {L"azure", 0xF0FFFF}, {L"beige", 0xF5F5DC}, {L"bisque", 0xFFE4C4}, {L"black", 0x000000},
    {L"blanchedalmond", 0xFFEBCD}, {L"blue", 0x0000FF}, {L"blueviolet", 0x8A2BE2}, {L"brown", 0xA52A2A},
    {L"burlywood", 0xDEB887}, {L"cadetblue", 0x5F9EA0}, {L"chartreuse", 0x7FFF00}, {L"chocolate", 0xD2691E},
    {L"coral", 0xFF7F50}, {L"cornflowerblue", 0x6495ED}, {L"cornsilk", 0xFFF8DC}, {L"crimson", 0xDC143C},
    {L"cyan", 0x00FFFF}, {L"darkblue", 0x00008B}, {L"darkcyan", 0x008B8B}, {L"darkgoldenrod", 0xB8860B},
    {L"darkgray", 0xA9A9A9}, {L"darkgreen", 0x006400}, {L"darkgrey", 0xA9A9A9}, {L"darkkhaki", 0xBDB76B},
    {L"darkmagenta", 0x8B008B}, {L"darkolivegreen", 0x556B2F}, {L"darkorange", 0xFF8C00}, {L"darkorchid", 0x9932CC},
    {L"darkred", 0x8B0000}, {L"darksalmon", 0xE9967A}, {L"darkseagreen", 0x8FBC8F}, {L"darkslateblue", 0x483D8B},
    {L"darkslategray", 0x2F4F4F}, {L"darkslategrey", 0x2F4F4F}, {L"darkturquoise", 0x00CED1}, {L"darkviolet", 0x9400D3},
    {L"deeppink", 0xFF1493}, {L"deepskyblue", 0x00BFFF}, {L"dimgray", 0x696969}, {L"dimgrey", 0x696969},
    {L"dodgerblue", 0x1E90FF}, {L"firebrick", 0xB22222}, {L"floralwhite", 0xFFFAF0}, {L"forestgreen", 0x228B22},
    {L"fuchsia", 0xFF00FF}, {L"gainsboro", 0xDCDCDC}, {L"ghostwhite", 0xF8F8FF}, {L"gold", 0xFFD700},
    {L"goldenrod", 0xDAA520}, {L"gray", 0x808080}, {L"green", 0x008000}, {L"greenyellow", 0xADFF2F},
    {L"grey", 0x808080}, {L"honeydew", 0xF0FFF0}, {L"hotpink", 0xFF69B4}, {L"indianred", 0xCD5C5C},
    {L"indigo", 0x4B0082}, {L"ivory", 0xFFFFF0}, {L"khaki", 0xF0E68C}, {L"lavender", 0xE6E6FA},
    {L"lavenderblush", 0xFFF0F5}, {L"lawngreen", 0x7CFC00}, {L"lemonchiffon", 0xFFFACD}, {L"lightblue", 0xADD8E6},
    {L"lightcoral", 0xF08080}, {L"lightcyan", 0xE0FFFF}, {L"lightgoldenrodyellow", 0xFAFAD2}, {L"lightgray", 0xD3D3D3},
    {L"lightgreen", 0x90EE90}, {L"lightgrey", 0xD3D3D3}, {L"lightpink", 0xFFB6C1}, {L"lightsalmon", 0xFFA07A},
    {L"lightseagreen", 0x20B2AA}, {L"lightskyblue", 0x87CEFA}, {L"lightslategray", 0x778899}, {L"lightslategrey", 0x778899},
    {L"lightsteelblue", 0xB0C4DE}, {L"lightyellow", 0xFFFFE0}, {L"lime", 0x00FF00}, {L"limegreen", 0x32CD32},
    {L"linen", 0xFAF0E6}, {L"magenta", 0xFF00FF}, {L"maroon", 0x800000}, {L"mediumaquamarine", 0x66CDAA},
    {L"mediumblue", 0x0000CD}, {L"mediumorchid", 0xBA55D3}, {L"mediumpurple", 0x9370DB}, {L"mediumseagreen", 0x3CB371},
    {L"mediumslateblue", 0x7B68EE}, {L"mediumspringgreen", 0x00FA9A}, {L"mediumturquoise", 0x48D1CC},
    {L"mediumvioletred", 0xC71585}, {L"midnightblue", 0x191970}, {L"mintcream", 0xF5FFFA}, {L"mistyrose", 0xFFE4E1},
    {L"moccasin", 0xFFE4B5}, {L"navajowhite", 0xFFDEAD}, {L"navy", 0x000080}, {L"oldlace", 0xFDF5E6},
    {L"olive", 0x808000}, {L"olivedrab", 0x6B8E23}, {L"orange", 0xFFA500}, {L"orangered", 0xFF4500},
    {L"orchid", 0xDA70D6}, {L"palegoldenrod", 0xEEE8AA}, {L"palegreen", 0x98FB98}, {L"paleturquoise", 0xAFEEEE},
    {L"palevioletred", 0xDB7093}, {L"papayawhip", 0xFFEFD5}, {L"peachpuff", 0xFFDAB9}, {L"peru", 0xCD853F},
    {L"pink", 0xFFC0CB}, {L"plum", 0xDDA0DD}, {L"powderblue", 0xB0E0E6}, {L"purple", 0x800080},
    {L"rebeccapurple", 0x663399}, {L"red", 0xFF0000}, {L"rosybrown", 0xBC8F8F}, {L"royalblue", 0x4169E1},
    {L"saddlebrown", 0x8B4513}, {L"salmon", 0xFA8072}, {L"sandybrown", 0xF4A460}, {L"seagreen", 0x2E8B57},
    {L"seashell", 0xFFF5EE}, {L"sienna", 0xA0522D}, {L"silver", 0xC0C0C0}, {L"skyblue", 0x87CEEB},
    {L"slateblue", 0x6A5ACD}, {L"slategray", 0x708090}, {L"slategrey", 0x708090}, {L"snow", 0xFFFAFA},
    {L"springgreen", 0x00FF7F}, {L"steelblue", 0x4682B4}, {L"tan", 0xD2B48C}, {L"teal", 0x008080},
    {L"thistle", 0xD8BFD8}, {L"tomato", 0xFF6347}, {L"turquoise", 0x40E0D0}, {L"violet", 0xEE82EE},
    {L"wheat", 0xF5DEB3}, {L"white", 0xFFFFFF}, {L"whitesmoke", 0xF5F5F5}, {L"yellow", 0xFFFF00},
    {L"yellowgreen", 0x9ACD32},
};

int HexDigit(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

// Numbers of rgb()/hsl(): "12", "50%", "0.5".
bool CssNumbers(std::wstring_view args, float* out, bool* percent, int max, int* count) {
    *count = 0;
    std::wstring a(args);
    for (wchar_t& c : a) {
        if (c == L',' || c == L'/') c = L' ';
    }
    const wchar_t* p = a.c_str();
    while (*p && *count < max) {
        while (*p == L' ' || *p == L'\t') ++p;
        if (!*p) break;
        wchar_t* end = nullptr;
        const float v = std::wcstof(p, &end);
        if (end == p || !std::isfinite(v)) return false;
        percent[*count] = *end == L'%';
        if (*end == L'%') ++end;
        while (std::iswalpha(*end)) ++end;  // deg
        out[(*count)++] = v;
        p = end;
    }
    return *count >= 3;
}

uint8_t Byte(float v) { return static_cast<uint8_t>(std::clamp(v, 0.f, 255.f) + 0.5f); }

}  // namespace

bool ParseCssColor(std::wstring_view v, uint32_t* argb) {
    v = Trim(v);
    if (v.empty()) return false;
    if (v[0] == L'#') {
        const std::wstring_view h = v.substr(1);
        uint32_t d[8] = {};
        if (h.size() != 3 && h.size() != 4 && h.size() != 6 && h.size() != 8) return false;
        for (size_t i = 0; i < h.size(); ++i) {
            const int x = HexDigit(h[i]);
            if (x < 0) return false;
            d[i] = static_cast<uint32_t>(x);
        }
        uint32_t r, g, b, a = 255;
        if (h.size() <= 4) {
            r = d[0] * 17;
            g = d[1] * 17;
            b = d[2] * 17;
            if (h.size() == 4) a = d[3] * 17;
        } else {
            r = d[0] * 16 + d[1];
            g = d[2] * 16 + d[3];
            b = d[4] * 16 + d[5];
            if (h.size() == 8) a = d[6] * 16 + d[7];
        }
        *argb = (a << 24) | (r << 16) | (g << 8) | b;
        return true;
    }
    const size_t paren = v.find(L'(');
    if (paren != std::wstring_view::npos && v.back() == L')') {
        const std::wstring fn = ToLower(Trim(v.substr(0, paren)));
        float n[4] = {};
        bool pc[4] = {};
        int count = 0;
        if (!CssNumbers(v.substr(paren + 1, v.size() - paren - 2), n, pc, 4, &count)) return false;
        const float alpha = count >= 4 ? (pc[3] ? n[3] / 100.f : n[3]) : 1.f;
        uint8_t r, g, b;
        if (fn == L"rgb" || fn == L"rgba") {
            r = Byte(pc[0] ? n[0] * 2.55f : n[0]);
            g = Byte(pc[1] ? n[1] * 2.55f : n[1]);
            b = Byte(pc[2] ? n[2] * 2.55f : n[2]);
        } else if (fn == L"hsl" || fn == L"hsla") {
            const float hh = std::fmod(std::fmod(n[0], 360.f) + 360.f, 360.f) / 60.f;
            const float s = std::clamp(n[1] / 100.f, 0.f, 1.f), l = std::clamp(n[2] / 100.f, 0.f, 1.f);
            const float c = (1 - std::fabs(2 * l - 1)) * s;
            const float x = c * (1 - std::fabs(std::fmod(hh, 2.f) - 1));
            float rr = 0, gg = 0, bb = 0;
            if (hh < 1) rr = c, gg = x;
            else if (hh < 2) rr = x, gg = c;
            else if (hh < 3) gg = c, bb = x;
            else if (hh < 4) gg = x, bb = c;
            else if (hh < 5) rr = x, bb = c;
            else rr = c, bb = x;
            const float m = l - c / 2;
            r = Byte((rr + m) * 255);
            g = Byte((gg + m) * 255);
            b = Byte((bb + m) * 255);
        } else {
            return false;
        }
        *argb = (static_cast<uint32_t>(Byte(std::clamp(alpha, 0.f, 1.f) * 255)) << 24) | (r << 16) | (g << 8) | b;
        return true;
    }
    const std::wstring name = ToLower(v);
    if (name == L"transparent" || name == L"none") {
        *argb = 0;
        return true;
    }
    for (const NamedColor& c : kColors) {
        if (name == c.name) {
            *argb = 0xFF000000u | c.rgb;
            return true;
        }
    }
    return false;
}

void Style::Apply(std::wstring_view css) {
    size_t i = 0;
    while (i < css.size()) {
        // A property ends at a comma or semicolon outside parentheses.
        size_t e = i;
        int depth = 0;
        while (e < css.size() && !((css[e] == L',' || css[e] == L';') && depth == 0)) {
            if (css[e] == L'(') ++depth;
            else if (css[e] == L')' && depth > 0) --depth;
            ++e;
        }
        const std::wstring_view prop = Trim(css.substr(i, e - i));
        i = e + 1;
        const size_t colon = prop.find(L':');
        if (colon == std::wstring_view::npos) continue;
        const std::wstring name = ToLower(Trim(prop.substr(0, colon)));
        std::wstring_view value = Trim(prop.substr(colon + 1));
        if (const size_t bang = value.find(L'!'); bang != std::wstring_view::npos) value = Trim(value.substr(0, bang));
        uint32_t argb = 0;
        if (name == L"fill" || name == L"background" || name == L"background-color") {
            if (ParseCssColor(value, &argb)) fill = Argb(argb);
        } else if (name == L"stroke") {
            if (ParseCssColor(value, &argb)) stroke = Argb(argb);
        } else if (name == L"color") {
            if (ParseCssColor(value, &argb)) color = Argb(argb);
        } else if (name == L"stroke-width") {
            const float w = std::wcstof(std::wstring(value).c_str(), nullptr);
            if (std::isfinite(w) && w >= 0) stroke_width = std::min(w, 20.f);
        } else if (name == L"stroke-dasharray") {
            dash = value.empty() || value == L"none" || value == L"0" ? 0 : 1;
        } else if (name == L"font-weight") {
            const std::wstring v = ToLower(value);
            bold = v == L"bold" || v == L"bolder" || std::wcstol(v.c_str(), nullptr, 10) >= 600 ? 1 : 0;
        } else if (name == L"font-style") {
            italic = EqualsNoCase(value, L"italic") || EqualsNoCase(value, L"oblique") ? 1 : 0;
        }
    }
}

void Style::Merge(const Style& over) {
    if (over.fill.role != kDgNone) fill = over.fill;
    if (over.stroke.role != kDgNone) stroke = over.stroke;
    if (over.color.role != kDgNone) color = over.color;
    if (over.stroke_width >= 0) stroke_width = over.stroke_width;
    if (over.dash >= 0) dash = over.dash;
    if (over.bold >= 0) bold = over.bold;
    if (over.italic >= 0) italic = over.italic;
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

namespace {
D2D1_POINT_2F Lerp(D2D1_POINT_2F a, D2D1_POINT_2F b, float t) { return D2D1::Point2F(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
D2D1_POINT_2F Mix3(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c, float wa, float wb, float wc, float d) {
    return D2D1::Point2F((a.x * wa + b.x * wb + c.x * wc) / d, (a.y * wa + b.y * wb + c.y * wc) / d);
}
}  // namespace

void AddBasisCurve(SceneBuilder& b, const std::vector<D2D1_POINT_2F>& pts) {
    if (pts.empty()) return;
    b.Begin(pts[0]);
    if (pts.size() == 1) return;
    if (pts.size() == 2) {
        b.LineTo(pts[1]);
        return;
    }
    // d3-shape's curveBasis.
    D2D1_POINT_2F x0 = pts[0], x1 = pts[1];
    auto bezier = [&](D2D1_POINT_2F p) {
        b.CubicTo(Mix3(x0, x1, x1, 2, 1, 0, 3), Mix3(x0, x1, x1, 1, 2, 0, 3), Mix3(x0, x1, p, 1, 4, 1, 6));
    };
    b.LineTo(Mix3(x0, x1, x1, 5, 1, 0, 6));
    for (size_t i = 2; i < pts.size(); ++i) {
        bezier(pts[i]);
        x0 = x1;
        x1 = pts[i];
    }
    bezier(x1);
    b.LineTo(x1);
}

D2D1_POINT_2F ClipRect(D2D1_POINT_2F c, float w, float h, D2D1_POINT_2F p) {
    const float dx = p.x - c.x, dy = p.y - c.y;
    float hw = w / 2, hh = h / 2;
    if (dx == 0 && dy == 0) return c;
    float sx, sy;
    if (std::fabs(dy) * hw > std::fabs(dx) * hh) {
        if (dy < 0) hh = -hh;
        sx = hh * dx / dy;
        sy = hh;
    } else {
        if (dx < 0) hw = -hw;
        sx = hw;
        sy = hw * dy / dx;
    }
    return D2D1::Point2F(c.x + sx, c.y + sy);
}

D2D1_POINT_2F ClipEllipse(D2D1_POINT_2F c, float rx, float ry, D2D1_POINT_2F p) {
    const float dx = p.x - c.x, dy = p.y - c.y;
    if ((dx == 0 && dy == 0) || rx <= 0 || ry <= 0) return c;
    const float t = 1.f / std::sqrt((dx / rx) * (dx / rx) + (dy / ry) * (dy / ry));
    return D2D1::Point2F(c.x + dx * t, c.y + dy * t);
}

D2D1_POINT_2F ClipPolygon(D2D1_POINT_2F c, const std::vector<D2D1_POINT_2F>& poly, D2D1_POINT_2F p) {
    // The crossing of the ray center -> p with the polygon farthest along it
    // (p may be inside: the ray runs on to the outline, as for the others).
    float best = -1;
    const float dx = p.x - c.x, dy = p.y - c.y;
    for (size_t i = 0; i < poly.size(); ++i) {
        const D2D1_POINT_2F a = poly[i], q = poly[(i + 1) % poly.size()];
        const float ex = q.x - a.x, ey = q.y - a.y;
        const float den = dx * ey - dy * ex;
        if (std::fabs(den) < 1e-9f) continue;
        const float t = ((a.x - c.x) * ey - (a.y - c.y) * ex) / den;  // along center -> p
        const float u = ((a.x - c.x) * dy - (a.y - c.y) * dx) / den;  // along the side
        if (t >= 0 && u >= -0.0001f && u <= 1.0001f) best = std::max(best, t);
    }
    if (best < 0) return c;
    return D2D1::Point2F(c.x + dx * best, c.y + dy * best);
}

D2D1_POINT_2F Retreat(D2D1_POINT_2F p, D2D1_POINT_2F from, float d) {
    const float dx = from.x - p.x, dy = from.y - p.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return p;
    const float t = std::min(d / len, 1.f);
    return D2D1::Point2F(p.x + dx * t, p.y + dy * t);
}

float MarkerLength(Marker m) {
    switch (m) {
    case Marker::Arrow: return 8.f;
    case Marker::Circle: return 8.f;
    case Marker::Cross: return 5.f;
    case Marker::OpenArrow: return 16.f;
    case Marker::Diamond:
    case Marker::OpenDiamond: return 18.f;
    case Marker::LineArrow: return 1.f;
    default: return 0.f;
    }
}

void AddMarker(SceneBuilder& b, Marker m, D2D1_POINT_2F tip, D2D1_POINT_2F from, DgColor color, float stroke_width) {
    float dx = tip.x - from.x, dy = tip.y - from.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f || m == Marker::None) return;
    dx /= len;
    dy /= len;
    const float nx = -dy, ny = dx;
    auto at = [&](float back, float side) { return D2D1::Point2F(tip.x - dx * back + nx * side, tip.y - dy * back + ny * side); };
    switch (m) {
    case Marker::Arrow: {
        const float l = 9.f + stroke_width, w = 4.5f + stroke_width * 0.5f;
        b.Polygon({tip, at(l, w), at(l * 0.85f, 0), at(l, -w)}, color, color, 1.f);
        break;
    }
    case Marker::Circle: {
        const D2D1_POINT_2F c = at(4.5f, 0);
        b.Ellipse(D2D1::RectF(c.x - 4.5f, c.y - 4.5f, c.x + 4.5f, c.y + 4.5f), color, color, 1.f);
        break;
    }
    case Marker::Cross: {
        const D2D1_POINT_2F c = at(5.f, 0);
        const float r = 4.5f;
        b.Begin(D2D1::Point2F(c.x - r, c.y - r));
        b.LineTo(D2D1::Point2F(c.x + r, c.y + r));
        b.MoveTo(D2D1::Point2F(c.x - r, c.y + r));
        b.LineTo(D2D1::Point2F(c.x + r, c.y - r));
        b.End(Role(kDgNone), color, 2.f);
        break;
    }
    case Marker::OpenArrow:
        b.Polygon({tip, at(16, 8), at(16, -8)}, Role(kDgPage), color, stroke_width);
        break;
    case Marker::Diamond:
        b.Polygon({tip, at(9, 6), at(18, 0), at(9, -6)}, color, color, stroke_width);
        break;
    case Marker::OpenDiamond:
        b.Polygon({tip, at(9, 6), at(18, 0), at(9, -6)}, Role(kDgPage), color, stroke_width);
        break;
    case Marker::LineArrow:
        b.Begin(at(10, 6));
        b.LineTo(tip);
        b.LineTo(at(10, -6));
        b.End(Role(kDgNone), color, stroke_width);
        break;
    default: break;
    }
}

}  // namespace mermaid

// ---------------------------------------------------------------------------
// Diagram types
// ---------------------------------------------------------------------------

namespace {

using namespace mermaid;

enum class Kind { Unknown, Flowchart };

// The diagram's type from its first keyword; `name` receives the keyword.
Kind DetectKind(const Source& src, std::wstring* name) {
    Cursor c(src.text, src.body);
    c.SkipStatementGap();
    const size_t start = c.pos();
    while (!c.AtEnd() && (IsWordChar(c.Peek()) || c.Peek() == L'-')) c.Advance();
    *name = std::wstring(src.text.substr(start, c.pos() - start));
    if (EqualsNoCase(*name, L"flowchart") || EqualsNoCase(*name, L"graph") || EqualsNoCase(*name, L"flowchart-elk")) {
        return Kind::Flowchart;
    }
    return Kind::Unknown;
}

template <typename Fn>
auto Guarded(std::wstring_view source, std::wstring* error, Fn&& fn) -> decltype(fn()) {
    if (source.size() > kMaxMermaidSource) {
        if (error) *error = Tr(L"The diagram is too large", L"Диаграмма слишком большая");
        return {};
    }
    try {
        return fn();
    } catch (const ParseError& e) {
        if (error) *error = FormatError(source, e);
    } catch (const std::bad_alloc&) {
        if (error) *error = Tr(L"Out of memory", L"Не хватает памяти");
    }
    return {};
}

std::wstring UnsupportedMessage(const std::wstring& name) {
    if (Trim(name).empty()) return Tr(L"No diagram type", L"Не указан тип диаграммы");
    return std::wstring(Tr(L"Unsupported diagram type: ", L"Этот тип диаграмм не поддерживается: ")) + name;
}

}  // namespace

bool CheckMermaid(std::wstring_view source, std::wstring* error) {
    return Guarded(source, error, [&]() -> bool {
        const Source src = Preprocess(source);
        std::wstring name;
        switch (DetectKind(src, &name)) {
        case Kind::Flowchart: Flowchart(src, nullptr); return true;
        default:
            if (error) *error = UnsupportedMessage(name);
            return false;
        }
    });
}

std::shared_ptr<DiagramScene> LayoutMermaid(std::wstring_view source, IDWriteFactory* dw, const std::wstring& family,
                                            float size, std::wstring* error) {
    return Guarded(source, error, [&]() -> std::shared_ptr<DiagramScene> {
        const Source src = Preprocess(source);
        std::wstring name;
        SceneBuilder builder(dw, family, size);
        switch (DetectKind(src, &name)) {
        case Kind::Flowchart: return Flowchart(src, &builder);
        default:
            if (error) *error = UnsupportedMessage(name);
            return nullptr;
        }
    });
}

std::shared_ptr<DiagramScene> MermaidErrorScene(const std::wstring& error, IDWriteFactory* dw,
                                                const std::wstring& family, float size) {
    try {
        SceneBuilder b(dw, family, size);
        RichText t;
        t.text = std::wstring(Tr(L"Mermaid diagram not shown: ", L"Диаграмма Mermaid не показана: ")) + error;
        const SceneBuilder::Measured m = b.Measure(t, 0, false, 0.875f, false);
        if (!m.layout) return nullptr;
        const float pad = 8;
        b.Rect(D2D1::RectF(0.5f, 0.5f, m.w + 2 * pad + 0.5f, m.h + 2 * pad + 0.5f), 4, Role(kDgNoteFill),
               Role(kDgNoteStroke));
        b.Text(m, D2D1::Point2F(pad, pad), Role(kDgNoteText));
        return b.Finish(m.w + 2 * pad + 1, m.h + 2 * pad + 1);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

}  // namespace mdv

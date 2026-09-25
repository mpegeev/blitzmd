// Mermaid C4 diagrams (C4Context, C4Container, C4Component, C4Dynamic,
// C4Deployment): people, systems, containers and components (plain,
// external, database, queue) in rows, boundaries around them, relations
// as straight lines with their labels, in the C4 colors Mermaid uses in
// both themes; laid out in rows as Mermaid's C4 renderer does (four shapes,
// two boundaries to a row).
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

enum class Kind : uint8_t { Person, System, Container, Component, Node, Boundary };
enum class Form : uint8_t { Plain, Db, Queue };

struct Element {
    std::wstring alias;
    Kind kind = Kind::System;
    Form form = Form::Plain;
    bool external = false;
    std::wstring type_name;        // shown: [Person], [Software System], [Container: techn]...
    Text label, techn, descr, type;  // (type: a boundary's)
    int parent = -1;               // its boundary
    std::vector<int> children;     // a boundary's
    uint32_t bg = 0, font = 0, border = 0;  // UpdateElementStyle (0: default)
};

struct Rel {
    int from = -1, to = -1;
    Text label, techn;
    bool both = false;
    uint32_t text_color = 0, line_color = 0;
    float dx = 0, dy = 0;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        const size_t start = c_.pos();
        while (IsWordChar(c_.Peek())) c_.Advance();
        const std::wstring_view head = src_.text.substr(start, c_.pos() - start);
        if (head != L"C4Context" && head != L"C4Container" && head != L"C4Component" && head != L"C4Dynamic" && head != L"C4Deployment") {
            c_.set_pos(start);
            c_.Fail(Tr(L"Expected C4Context, C4Container, C4Component, C4Dynamic or C4Deployment",
                       L"Ожидалось C4Context, C4Container, C4Component, C4Dynamic или C4Deployment"));
        }
        std::vector<std::pair<int, size_t>> open;
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            const size_t at = c_.pos();
            if (c_.Eat(L'}')) {
                if (open.empty()) c_.Fail(Tr(L"'}' without '{'", L"«}» без «{»"));
                open.pop_back();
                continue;
            }
            if (c_.EatKeyword(L"title")) {
                c_.SkipBlanks();
                const std::wstring_view t = c_.LineRest();
                title = Text{std::wstring(t), static_cast<uint32_t>(t.data() - src_.text.data()), static_cast<uint32_t>(t.size())};
                continue;
            }
            if (c_.EatKeyword(L"accTitle") || c_.EatKeyword(L"accDescr")) {
                c_.LineRest();
                continue;
            }
            const size_t k = c_.pos();
            while (IsWordChar(c_.Peek())) c_.Advance();
            const std::wstring name(src_.text.substr(k, c_.pos() - k));
            c_.SkipBlanks();
            if (name.empty() || !c_.Eat(L'(')) {
                c_.set_pos(at);
                c_.Fail(Tr(L"Expected an element, a boundary or a relation", L"Ожидался элемент, граница или связь"));
            }
            std::vector<Text> args;
            std::map<std::wstring, Text> named;
            Args(&args, &named);
            c_.SkipBlanks();
            const bool block = c_.Eat(L'{');
            Statement(name, args, named, open.empty() ? -1 : open.back().first, at, block ? &open : nullptr);
        }
        if (!open.empty()) {
            c_.set_pos(open.back().second);
            c_.Fail(Tr(L"'{' without '}'", L"«{» без «}»"));
        }
    }

    Text title;
    std::vector<Element> elements;
    std::vector<Rel> rels;
    int shapes_in_row = 4, boundaries_in_row = 2;

private:
    // (a, "b c", $descr="d")
    void Args(std::vector<Text>* args, std::map<std::wstring, Text>* named) {
        for (;;) {
            c_.SkipBlanks();
            if (c_.Eat(L')')) return;
            if (!args->empty() || !named->empty()) {
                if (!c_.Eat(L',')) c_.Fail(Tr(L"Expected ',' or ')'", L"Ожидалась «,» или «)»"));
                c_.SkipBlanks();
                if (c_.Eat(L')')) return;
            }
            std::wstring key;
            if (c_.Eat(L'$')) {
                const size_t k = c_.pos();
                while (IsWordChar(c_.Peek())) c_.Advance();
                key = std::wstring(src_.text.substr(k, c_.pos() - k));
                c_.SkipBlanks();
                if (!c_.Eat(L'=')) c_.Fail(Tr(L"Expected '='", L"Ожидалось «=»"));
                c_.SkipBlanks();
            }
            Text value;
            if (c_.Peek() == L'"') {
                c_.Advance();
                const size_t s = c_.pos();
                while (!c_.AtEnd() && c_.Peek() != L'"' && c_.Peek() != L'\n') c_.Advance();
                if (!c_.Eat(L'"')) c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
                value = Here(src_.text.substr(s, c_.pos() - 1 - s));
            } else {
                const size_t s = c_.pos();
                while (!c_.AtEnd() && c_.Peek() != L',' && c_.Peek() != L')' && c_.Peek() != L'\n') c_.Advance();
                if (c_.AtEnd() || c_.Peek() == L'\n') c_.Fail(Tr(L"Expected ')'", L"Ожидалась «)»"));
                value = Here(Trim(src_.text.substr(s, c_.pos() - s)));
            }
            if (!key.empty()) (*named)[key] = value;
            else if (args->size() < 16) args->push_back(value);
        }
    }

    void Statement(const std::wstring& name, const std::vector<Text>& a, const std::map<std::wstring, Text>& named, int parent, size_t at,
                   std::vector<std::pair<int, size_t>>* open) {
        auto arg = [&](size_t i, const wchar_t* key) {
            if (auto it = named.find(key); it != named.end()) return it->second;
            return i < a.size() ? a[i] : Text{};
        };
        // Relations.
        static const wchar_t* kRels[] = {L"Rel", L"BiRel", L"Rel_U", L"Rel_Up", L"Rel_D", L"Rel_Down", L"Rel_L", L"Rel_Left", L"Rel_R", L"Rel_Right", L"Rel_Back", L"RelIndex"};
        if (std::find_if(std::begin(kRels), std::end(kRels), [&](const wchar_t* r) { return name == r; }) != std::end(kRels)) {
            const size_t o = name == L"RelIndex" ? 1 : 0;
            Rel r;
            const bool back = name == L"Rel_Back";
            r.from = Find(arg(back ? o + 1 : o, L"from").raw, at);
            r.to = Find(arg(back ? o : o + 1, L"to").raw, at);
            r.label = arg(o + 2, L"label");
            r.techn = arg(o + 3, L"techn");
            r.both = name == L"BiRel";
            if (rels.size() >= kMaxEdges) Error(at, Tr(L"Too many relations", L"Слишком много связей"));
            rels.push_back(std::move(r));
            return;
        }
        if (name == L"UpdateElementStyle") {
            if (a.empty()) return;
            auto it = ids_.find(a[0].raw);
            if (it == ids_.end()) return;
            Element& e = elements[static_cast<size_t>(it->second)];
            auto color = [&](const wchar_t* key, uint32_t* out) {
                if (auto n = named.find(key); n != named.end()) ParseCssColor(n->second.raw, out);
            };
            color(L"bgColor", &e.bg);
            color(L"fontColor", &e.font);
            color(L"borderColor", &e.border);
            return;
        }
        if (name == L"UpdateRelStyle") {
            if (a.size() < 2) return;
            for (Rel& r : rels) {
                if (r.from < 0 || r.to < 0 || elements[static_cast<size_t>(r.from)].alias != a[0].raw || elements[static_cast<size_t>(r.to)].alias != a[1].raw) continue;
                if (auto n = named.find(L"textColor"); n != named.end()) ParseCssColor(n->second.raw, &r.text_color);
                if (auto n = named.find(L"lineColor"); n != named.end()) ParseCssColor(n->second.raw, &r.line_color);
                if (auto n = named.find(L"offsetX"); n != named.end()) r.dx = static_cast<float>(std::wcstod(n->second.raw.c_str(), nullptr));
                if (auto n = named.find(L"offsetY"); n != named.end()) r.dy = static_cast<float>(std::wcstod(n->second.raw.c_str(), nullptr));
            }
            return;
        }
        if (name == L"UpdateLayoutConfig") {
            if (auto n = named.find(L"c4ShapeInRow"); n != named.end()) shapes_in_row = std::clamp(static_cast<int>(std::wcstol(n->second.raw.c_str(), nullptr, 10)), 1, 20);
            if (auto n = named.find(L"c4BoundaryInRow"); n != named.end()) boundaries_in_row = std::clamp(static_cast<int>(std::wcstol(n->second.raw.c_str(), nullptr, 10)), 1, 20);
            return;
        }
        if (name == L"UpdateBoundaryStyle" || name == L"AddElementTag" || name == L"AddRelTag") return;
        // Elements and boundaries.
        Element e;
        e.parent = parent;
        static const struct {
            const wchar_t* name;
            Kind kind;
            Form form;
            bool external;
            const wchar_t* shown;
        } kShapes[] = {
            {L"Person", Kind::Person, Form::Plain, false, L"Person"},          {L"Person_Ext", Kind::Person, Form::Plain, true, L"Person"},
            {L"System", Kind::System, Form::Plain, false, L"Software System"}, {L"System_Ext", Kind::System, Form::Plain, true, L"Software System"},
            {L"SystemDb", Kind::System, Form::Db, false, L"Software System"},  {L"SystemDb_Ext", Kind::System, Form::Db, true, L"Software System"},
            {L"SystemQueue", Kind::System, Form::Queue, false, L"Software System"}, {L"SystemQueue_Ext", Kind::System, Form::Queue, true, L"Software System"},
            {L"Container", Kind::Container, Form::Plain, false, L"Container"}, {L"Container_Ext", Kind::Container, Form::Plain, true, L"Container"},
            {L"ContainerDb", Kind::Container, Form::Db, false, L"Container"},  {L"ContainerDb_Ext", Kind::Container, Form::Db, true, L"Container"},
            {L"ContainerQueue", Kind::Container, Form::Queue, false, L"Container"}, {L"ContainerQueue_Ext", Kind::Container, Form::Queue, true, L"Container"},
            {L"Component", Kind::Component, Form::Plain, false, L"Component"}, {L"Component_Ext", Kind::Component, Form::Plain, true, L"Component"},
            {L"ComponentDb", Kind::Component, Form::Db, false, L"Component"},  {L"ComponentDb_Ext", Kind::Component, Form::Db, true, L"Component"},
            {L"ComponentQueue", Kind::Component, Form::Queue, false, L"Component"}, {L"ComponentQueue_Ext", Kind::Component, Form::Queue, true, L"Component"},
        };
        bool found = false;
        for (const auto& s : kShapes) {
            if (name != s.name) continue;
            e.kind = s.kind;
            e.form = s.form;
            e.external = s.external;
            e.type_name = s.shown;
            e.alias = arg(0, L"alias").raw;
            e.label = arg(1, L"label");
            const bool has_techn = s.kind == Kind::Container || s.kind == Kind::Component;
            if (has_techn) {
                e.techn = arg(2, L"techn");
                e.descr = arg(3, L"descr");
            } else {
                e.descr = arg(2, L"descr");
            }
            found = true;
        }
        static const struct {
            const wchar_t* name;
            const wchar_t* type;
        } kBoundaries[] = {{L"Boundary", L""}, {L"Enterprise_Boundary", L"ENTERPRISE"}, {L"System_Boundary", L"SYSTEM"},
                           {L"Container_Boundary", L"CONTAINER"}, {L"Deployment_Node", L""}, {L"Node", L""}, {L"Node_L", L""}, {L"Node_R", L""}};
        for (const auto& bd : kBoundaries) {
            if (name != bd.name) continue;
            e.kind = name.starts_with(L"Node") || name == L"Deployment_Node" ? Kind::Node : Kind::Boundary;
            e.alias = arg(0, L"alias").raw;
            e.label = arg(1, L"label");
            e.type = arg(2, L"type");
            if (e.type.raw.empty()) e.type.raw = bd.type;
            if (e.kind == Kind::Node) e.descr = arg(3, L"descr");
            found = true;
        }
        if (!found) Error(at, Tr(L"Unknown C4 statement: ", L"Неизвестная команда C4: ") + name);
        if (elements.size() >= kMaxNodes) Error(at, Tr(L"Too many elements", L"Слишком много элементов"));
        const int index = static_cast<int>(elements.size());
        if (!e.alias.empty()) ids_[e.alias] = index;
        const bool boundary = e.kind == Kind::Boundary || e.kind == Kind::Node;
        elements.push_back(std::move(e));
        if (parent >= 0) elements[static_cast<size_t>(parent)].children.push_back(index);
        else roots.push_back(index);
        if (open) {
            if (!boundary) Error(at, Tr(L"Only boundaries hold elements", L"Элементы бывают только внутри границ"));
            if (static_cast<int>(open->size()) >= kMaxNesting) Error(at, Tr(L"Boundaries nested too deep", L"Слишком глубокая вложенность границ"));
            open->push_back({index, at});
        }
    }

public:
    std::vector<int> roots;

private:
    int Find(const std::wstring& alias, size_t at) {
        auto it = ids_.find(alias);
        if (it == ids_.end()) Error(at, Tr(L"Unknown element: ", L"Неизвестный элемент: ") + alias);
        return it->second;
    }

    [[noreturn]] void Error(size_t at, std::wstring message) {
        c_.set_pos(at);
        c_.Fail(std::move(message));
    }

    Text Here(std::wstring_view s) {
        if (s.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return Text{std::wstring(s), static_cast<uint32_t>(s.data() - src_.text.data()), static_cast<uint32_t>(s.size())};
    }

    const Source& src_;
    Cursor c_;
    std::unordered_map<std::wstring, int> ids_;
};

// ---------------------------------------------------------------------------
// Layout and drawing
// ---------------------------------------------------------------------------

using M = SceneBuilder::Measured;

constexpr float kShapeW = 216, kMargin = 50, kPad = 12, kHead = 48, kTitleBand = 50;

struct Colors {
    uint32_t bg, border, font;
};

Colors ColorsOf(const Element& e) {
    switch (e.kind) {
    case Kind::Person: return e.external ? Colors{0xFF686868, 0xFF8A8A8A, 0xFFFFFFFF} : Colors{0xFF08427B, 0xFF073B6F, 0xFFFFFFFF};
    case Kind::System: return e.external ? Colors{0xFF999999, 0xFF8A8A8A, 0xFFFFFFFF} : Colors{0xFF1168BD, 0xFF3C7FC0, 0xFFFFFFFF};
    case Kind::Container: return e.external ? Colors{0xFFB3B3B3, 0xFFA6A6A6, 0xFFFFFFFF} : Colors{0xFF438DD5, 0xFF3C7FC0, 0xFFFFFFFF};
    case Kind::Component: return e.external ? Colors{0xFFCCCCCC, 0xFFBFBFBF, 0xFF000000} : Colors{0xFF85BBF0, 0xFF78A8D8, 0xFF000000};
    default: return {0, 0xFF444444, 0};
    }
}

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b, const Source& src) : p_(p), b_(b), src_(src), n_(p.elements.size()) {}

    std::shared_ptr<DiagramScene> Build() {
        rect_.assign(n_, D2D1::RectF(0, 0, 0, 0));
        texts_.resize(n_);
        for (size_t i = 0; i < n_; ++i) MeasureElement(i);
        const Text title = !p_.title.raw.empty() ? p_.title : Text{src_.title, src_.title_pos, src_.title_len};
        float top = 0;
        if (!title.raw.empty()) {
            const M m = b_.Measure(ParseLabel(title.raw, false), 0, true, 1.f);
            b_.Text(m, D2D1::Point2F(0, 0), Role(kDgText), title.pos, title.len);
            top = m.h + 20;
        }
        // Boundaries first (under everything), then shapes, then relations.
        Lay(p_.roots, 0, top);
        for (size_t i = 0; i < n_; ++i) {
            if (IsBoundary(i)) DrawBoundary(i);
        }
        for (size_t i = 0; i < n_; ++i) {
            if (!IsBoundary(i)) DrawShape(i);
        }
        for (const Rel& r : p_.rels) DrawRel(r);
        return b_.FinishFitted(10);
    }

private:
    bool IsBoundary(size_t i) const { return p_.elements[i].kind == Kind::Boundary || p_.elements[i].kind == Kind::Node; }

    struct Texts {
        M label, type, descr;
        float h = 0;
    };

    void MeasureElement(size_t i) {
        const Element& e = p_.elements[i];
        Texts& t = texts_[i];
        const float unit = b_.size();
        const float wrap = kShapeW - 2 * kPad;
        if (IsBoundary(i)) {
            t.label = b_.Measure(ParseLabel(e.label.raw, false), 0, true, 1.f);
            if (!e.type.raw.empty()) {
                RichText ty;
                ty.text = L"[" + e.type.raw + L"]";
                t.type = b_.Measure(ty, 0, false, 14 / unit);
            }
            if (!e.descr.raw.empty()) t.descr = b_.Measure(ParseLabel(e.descr.raw, false), 0, false, 12 / unit);
            return;
        }
        t.label = b_.Measure(ParseLabel(e.label.raw, false), wrap, true, 14 / unit);
        RichText ty;
        ty.text = L"[" + e.type_name + (e.techn.raw.empty() ? L"" : L": " + e.techn.raw) + L"]";
        t.type = b_.Measure(ty, wrap, false, 11 / unit);
        if (!e.descr.raw.empty()) t.descr = b_.Measure(ParseLabel(e.descr.raw, false), wrap, true, 11 / unit);
        t.h = kPad + t.label.h + 4 + t.type.h + (t.descr.layout ? 6 + t.descr.h : 0.f) + kPad;
        t.h = std::max(t.h, 60.f);
    }

    // Shapes in rows, then boundaries in rows below them; returns the size.
    D2D1_SIZE_F Lay(const std::vector<int>& items, float x0, float y0) {
        std::vector<int> shapes, bounds;
        for (int i : items) (IsBoundary(static_cast<size_t>(i)) ? bounds : shapes).push_back(i);
        float y = y0, width = 0;
        for (size_t k = 0; k < shapes.size(); k += static_cast<size_t>(p_.shapes_in_row)) {
            const size_t end = std::min(shapes.size(), k + static_cast<size_t>(p_.shapes_in_row));
            float row_h = 0;
            for (size_t j = k; j < end; ++j) row_h = std::max(row_h, Height(static_cast<size_t>(shapes[j])));
            float x = x0;
            for (size_t j = k; j < end; ++j) {
                const size_t i = static_cast<size_t>(shapes[j]);
                rect_[i] = D2D1::RectF(x, y, x + kShapeW, y + Height(i));
                x += kShapeW + kMargin;
            }
            width = std::max(width, x - kMargin - x0);
            y += row_h + kMargin;
        }
        for (size_t k = 0; k < bounds.size(); k += static_cast<size_t>(p_.boundaries_in_row)) {
            const size_t end = std::min(bounds.size(), k + static_cast<size_t>(p_.boundaries_in_row));
            float x = x0, row_h = 0;
            for (size_t j = k; j < end; ++j) {
                const size_t i = static_cast<size_t>(bounds[j]);
                const Texts& t = texts_[i];
                const float band = t.label.h + (t.type.layout ? t.type.h : 0.f) + (t.descr.layout ? t.descr.h : 0.f) + 16;
                const D2D1_SIZE_F inner = Lay(p_.elements[i].children, x + kMargin, y + band + 10);
                const float w = std::max({inner.width + 2 * kMargin, t.label.w + 2 * kPad, t.type.w + 2 * kPad, 2 * kMargin});
                const float h = band + 10 + inner.height + kMargin / 2;
                rect_[i] = D2D1::RectF(x, y, x + w, y + h);
                // (Children centered in a box wider than they are.)
                const float spare = w - inner.width - 2 * kMargin;
                if (spare > 0) Shift(p_.elements[i].children, spare / 2);
                x += w + kMargin;
                row_h = std::max(row_h, h);
            }
            width = std::max(width, x - kMargin - x0);
            y += row_h + kMargin;
        }
        return D2D1::SizeF(width, std::max(y - kMargin - y0, 0.f));
    }

    void Shift(const std::vector<int>& items, float dx) {
        for (int i : items) {
            D2D1_RECT_F& r = rect_[static_cast<size_t>(i)];
            r = D2D1::RectF(r.left + dx, r.top, r.right + dx, r.bottom);
            Shift(p_.elements[static_cast<size_t>(i)].children, dx);
        }
    }

    float Height(size_t i) const { return texts_[i].h + (p_.elements[i].kind == Kind::Person ? kHead * 1.5f : 0.f); }

    void DrawBoundary(size_t i) {
        const Element& e = p_.elements[i];
        const D2D1_RECT_F r = rect_[i];
        const Texts& t = texts_[i];
        const uint32_t border = e.border ? e.border : 0xFF444444;
        if (e.kind == Kind::Node) b_.Rect(r, 6, e.bg ? Argb(e.bg) : Role(kDgNone), Argb(border), 1.f);
        else b_.Rect(r, 0, e.bg ? Argb(e.bg) : Role(kDgNone), Argb(border), 1.f, 1);
        const float cx = (r.left + r.right) / 2;
        float y = r.top + 6;
        b_.Text(t.label, D2D1::Point2F(cx - t.label.w / 2, y), Role(kDgText), e.label.pos, e.label.len);
        y += t.label.h;
        if (t.type.layout) {
            b_.Text(t.type, D2D1::Point2F(cx - t.type.w / 2, y), Role(kDgMuted), e.type.pos, e.type.len);
            y += t.type.h;
        }
        if (t.descr.layout) b_.Text(t.descr, D2D1::Point2F(cx - t.descr.w / 2, y), Role(kDgMuted), e.descr.pos, e.descr.len);
    }

    void DrawShape(size_t i) {
        const Element& e = p_.elements[i];
        const D2D1_RECT_F full = rect_[i];
        const Colors c = ColorsOf(e);
        const DgColor bg = Argb(e.bg ? e.bg : c.bg), border = Argb(e.border ? e.border : c.border), font = Argb(e.font ? e.font : c.font);
        D2D1_RECT_F r = full;
        if (e.kind == Kind::Person) {
            // A head above the body.
            const float cx = (r.left + r.right) / 2;
            b_.Ellipse(D2D1::RectF(cx - kHead, r.top, cx + kHead, r.top + 2 * kHead), bg, border, 1.f);
            r.top += kHead * 1.5f;
            b_.Rect(r, 40, bg, border, 1.f);
        } else if (e.form == Form::Db) {
            const float ry = 10;
            b_.Rect(D2D1::RectF(r.left, r.top + ry, r.right, r.bottom - ry), 0, bg, Role(kDgNone));
            b_.Ellipse(D2D1::RectF(r.left, r.bottom - 2 * ry, r.right, r.bottom), bg, border, 1.f);
            b_.Rect(D2D1::RectF(r.left + 0.5f, r.bottom - 2 * ry, r.right - 0.5f, r.bottom - ry), 0, bg, Role(kDgNone));
            b_.Begin(D2D1::Point2F(r.left, r.top + ry));
            b_.LineTo(D2D1::Point2F(r.left, r.bottom - ry));
            b_.MoveTo(D2D1::Point2F(r.right, r.top + ry));
            b_.LineTo(D2D1::Point2F(r.right, r.bottom - ry));
            b_.End(Role(kDgNone), border, 1.f);
            b_.Ellipse(D2D1::RectF(r.left, r.top, r.right, r.top + 2 * ry), bg, border, 1.f);
            r.top += ry;
        } else if (e.form == Form::Queue) {
            const float rx = 10;
            b_.Rect(D2D1::RectF(r.left + rx, r.top, r.right - rx, r.bottom), 0, bg, Role(kDgNone));
            b_.Ellipse(D2D1::RectF(r.left, r.top, r.left + 2 * rx, r.bottom), bg, border, 1.f);
            b_.Rect(D2D1::RectF(r.left + rx, r.top + 0.5f, r.left + 2 * rx, r.bottom - 0.5f), 0, bg, Role(kDgNone));
            b_.Begin(D2D1::Point2F(r.left + rx, r.top));
            b_.LineTo(D2D1::Point2F(r.right - rx, r.top));
            b_.MoveTo(D2D1::Point2F(r.left + rx, r.bottom));
            b_.LineTo(D2D1::Point2F(r.right - rx, r.bottom));
            b_.End(Role(kDgNone), border, 1.f);
            b_.Ellipse(D2D1::RectF(r.right - 2 * rx, r.top, r.right, r.bottom), bg, border, 1.f);
        } else {
            b_.Rect(r, 10, bg, border, 1.f);
        }
        const Texts& t = texts_[i];
        const float cx = (r.left + r.right) / 2;
        float y = r.top + (r.bottom - r.top - (t.h - 2 * kPad)) / 2;
        b_.Text(t.label, D2D1::Point2F(cx - t.label.w / 2, y), font, e.label.pos, e.label.len);
        y += t.label.h + 4;
        b_.Text(t.type, D2D1::Point2F(cx - t.type.w / 2, y), font, e.techn.pos, e.techn.len);
        y += t.type.h + 6;
        if (t.descr.layout) b_.Text(t.descr, D2D1::Point2F(cx - t.descr.w / 2, y), font, e.descr.pos, e.descr.len);
    }

    // The point where a line from the element's middle toward `p` leaves it.
    D2D1_POINT_2F Border(size_t i, D2D1_POINT_2F toward) const {
        const D2D1_RECT_F& r = rect_[i];
        return ClipRect(D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2), r.right - r.left, r.bottom - r.top, toward);
    }

    void DrawRel(const Rel& rel) {
        if (rel.from < 0 || rel.to < 0 || rel.from == rel.to) return;
        const size_t a = static_cast<size_t>(rel.from), z = static_cast<size_t>(rel.to);
        const D2D1_RECT_F& ra = rect_[a];
        const D2D1_RECT_F& rz = rect_[z];
        const D2D1_POINT_2F ca = D2D1::Point2F((ra.left + ra.right) / 2, (ra.top + ra.bottom) / 2), cz = D2D1::Point2F((rz.left + rz.right) / 2, (rz.top + rz.bottom) / 2);
        D2D1_POINT_2F p0 = Border(a, cz), p1 = Border(z, ca);
        const DgColor line = rel.line_color ? Argb(rel.line_color) : Argb(0xFF444444);
        const D2D1_POINT_2F tip = p1, start = p0;
        p1 = Retreat(p1, p0, MarkerLength(Marker::Arrow));
        if (rel.both) p0 = Retreat(p0, p1, MarkerLength(Marker::Arrow));
        b_.Begin(p0);
        b_.LineTo(p1);
        b_.End(Role(kDgNone), line, 1.f);
        AddMarker(b_, Marker::Arrow, tip, start, line, 1.f);
        if (rel.both) AddMarker(b_, Marker::Arrow, start, tip, line, 1.f);
        // The label and technology at the middle.
        const float unit = b_.size();
        const D2D1_POINT_2F mid = D2D1::Point2F((start.x + tip.x) / 2 + rel.dx, (start.y + tip.y) / 2 + rel.dy);
        const DgColor ink = rel.text_color ? Argb(rel.text_color) : Role(kDgText);
        float y = mid.y;
        if (!rel.label.raw.empty()) {
            const M m = b_.Measure(ParseLabel(rel.label.raw, false), 0, false, 12 / unit);
            y -= m.h / 2;
            b_.Text(m, D2D1::Point2F(mid.x - m.w / 2, y), ink, rel.label.pos, rel.label.len);
            y += m.h;
        }
        if (!rel.techn.raw.empty()) {
            RichText t;
            t.text = L"[" + rel.techn.raw + L"]";
            const M m = b_.Measure(t, 0, false, 11 / unit);
            b_.Text(m, D2D1::Point2F(mid.x - m.w / 2, y), ink, rel.techn.pos, rel.techn.len);
        }
    }

    const Parser& p_;
    SceneBuilder& b_;
    const Source& src_;
    size_t n_;
    std::vector<D2D1_RECT_F> rect_;
    std::vector<Texts> texts_;
};

}  // namespace

std::shared_ptr<DiagramScene> C4Diagram(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder, src).Build();
}

}  // namespace mdv::mermaid

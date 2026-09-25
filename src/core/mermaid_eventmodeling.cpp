// Mermaid event models (eventmodeling): time frames of UI, commands, read
// models, events and processors placed left to right in swimlanes (by kind,
// or a lane of their own for a namespace), each frame's box with its name
// and data, arrows from the frames that lead to it; laid out as Mermaid's
// event modeling renderer does (its measures of the boxes' text and all).
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <map>
#include <optional>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

enum class T { Keyword, Id, Fid, Inline, Block, Eof };

struct Token {
    T kind = T::Eof;
    std::wstring word;
    Text text;
};

bool Letter(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
bool Digit(wchar_t c) { return c >= L'0' && c <= L'9'; }
bool WordChar(wchar_t c) { return Letter(c) || Digit(c) || c == L'_'; }

// Mermaid's lexer: keywords (unless a longer name), then its terminals.
class Lexer {
public:
    Lexer(std::wstring_view s, size_t pos) : s_(s), p_(pos) {}

    std::vector<Token> Run() {
        std::vector<Token> out;
        for (;;) {
            Token t = Next();
            if (out.size() > 100000) TooLarge();
            const bool end = t.kind == T::Eof;
            out.push_back(std::move(t));
            if (end) return out;
        }
    }

private:
    bool Starts(std::wstring_view w) const { return s_.substr(p_).starts_with(w); }
    Token Make(T kind, size_t len, std::wstring word = {}) {
        if (len > kMaxLabel) mermaid::Fail(p_, Tr(L"Text too long", L"Слишком длинный текст"));
        Token t{kind, std::move(word), Text{std::wstring(s_.substr(p_, len)), static_cast<uint32_t>(p_), static_cast<uint32_t>(len)}};
        p_ += len;
        return t;
    }
    size_t IdLength() const {
        if (p_ >= s_.size() || !(Letter(s_[p_]) || s_[p_] == L'_')) return 0;
        size_t e = p_ + 1;
        while (e < s_.size() && WordChar(s_[e])) ++e;
        return e - p_;
    }

    Token Next() {
        for (;;) {
            while (p_ < s_.size() && std::iswspace(s_[p_])) ++p_;
            if (p_ >= s_.size()) return Token{T::Eof, {}, Text{L"", static_cast<uint32_t>(p_), 0}};
            if (Starts(L"%%")) {
                if (Starts(L"%%{")) {
                    const size_t e = s_.find(L"}%%", p_ + 3);
                    if (e != std::wstring_view::npos) {
                        p_ = e + 3;
                        continue;
                    }
                }
                while (p_ < s_.size() && s_[p_] != L'\n' && s_[p_] != L'\r') ++p_;
                continue;
            }
            if (Starts(L"/*")) {
                const size_t e = s_.find(L"*/", p_ + 2);
                if (e != std::wstring_view::npos) {
                    p_ = e + 2;
                    continue;
                }
            }
            if (Starts(L"//")) {
                while (p_ < s_.size() && s_[p_] != L'\n' && s_[p_] != L'\r') ++p_;
                continue;
            }
            break;
        }
        // Keywords, longest first; a longer name wins.
        static const wchar_t* kWords[] = {L"eventmodeling", L"resetframe", L"timeframe", L"processor", L"readmodel", L"command", L"entity", L"event",
                                          L"figma", L"given", L"jsobj", L"json", L"html", L"salt", L"text", L"then", L"when", L"data", L"note",
                                          L"->>", L"cmd", L"evt", L"gwt", L"pcr", L"rmo", L"uri", L"[[", L"]]", L"md", L"rf", L"tf", L"ui", L"`", L"."};
        for (const wchar_t* w : kWords) {
            if (!Starts(w)) continue;
            const size_t n = std::wcslen(w);
            if (Letter(w[0]) && IdLength() > n) break;
            if (std::wstring_view(w) == L"eventmodeling" && p_ + n < s_.size() && !std::iswspace(s_[p_ + n]) && !s_.substr(p_ + n).starts_with(L"%%")) break;
            return Make(T::Keyword, n, w);
        }
        if (const size_t n = IdLength()) return Make(T::Id, n);
        if (Digit(s_[p_])) {
            size_t e = p_;
            while (e < s_.size() && e - p_ < 3 && Digit(s_[e])) ++e;
            return Make(T::Fid, e - p_);
        }
        // Inline data: {...}, "..." or '...' to the last of it on the line.
        const wchar_t c = s_[p_];
        if (c == L'{' || c == L'"' || c == L'\'') {
            size_t eol = p_;
            while (eol < s_.size() && s_[eol] != L'\n' && s_[eol] != L'\r') ++eol;
            const wchar_t close = c == L'{' ? L'}' : c;
            const size_t last = s_.substr(p_ + 1, eol - p_ - 1).rfind(close);
            if (last != std::wstring_view::npos) return Make(T::Inline, last + 2);
        }
        // A data block: { on its line, } on its own (then a line end, or not
        // followed by more).
        if (c == L'{') {
            size_t k = p_ + 1;
            while (k < s_.size() && (s_[k] == L' ' || s_[k] == L'\t')) ++k;
            if (k < s_.size() && (s_[k] == L'\n' || (s_[k] == L'\r' && k + 1 < s_.size() && s_[k + 1] == L'\n'))) {
                for (size_t e = s_.find(L'\n', k); e != std::wstring_view::npos; e = s_.find(L'\n', e + 1)) {
                    if (e + 1 < s_.size() && s_[e + 1] == L'}' && (e + 2 >= s_.size() || std::iswspace(s_[e + 2]))) return Make(T::Block, e + 2 - p_);
                }
            }
        }
        mermaid::Fail(p_, Tr(L"Unexpected character", L"Непонятный символ"));
    }

    std::wstring_view s_;
    size_t p_;
};

struct Frame {
    Text name;          // its number
    bool reset = false;
    std::wstring type;  // ui, cmd, evt...
    Text entity;        // Namespace.Name
    std::vector<std::wstring> sources;
    std::wstring data_ref;
    std::wstring inline_data;
    bool has_inline = false;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src) {}

    void Parse() {
        toks_ = Lexer(src_.text, src_.body).Run();
        if (!IsKey(L"eventmodeling")) Fail(toks_[i_].text.pos, Tr(L"Expected \"eventmodeling\"", L"Ожидалось «eventmodeling»"));
        ++i_;
        while (toks_[i_].kind != T::Eof) {
            if (IsKey(L"tf") || IsKey(L"timeframe") || IsKey(L"rf") || IsKey(L"resetframe")) {
                Frame f;
                f.reset = IsKey(L"rf") || IsKey(L"resetframe");
                ++i_;
                f.name = Take(T::Fid, Tr(L"Expected a frame number", L"Ожидался номер кадра"));
                f.type = Type();
                f.entity = QualifiedName();
                while (IsKey(L"->>")) {
                    ++i_;
                    f.sources.push_back(Take(T::Fid, Tr(L"Expected a frame number", L"Ожидался номер кадра")).raw);
                }
                if (IsKey(L"[[")) {
                    ++i_;
                    f.data_ref = Take(T::Id, Tr(L"Expected a data name", L"Ожидалось имя данных")).raw;
                    Expect(L"]]");
                }
                if (IsKey(L"`") || toks_[i_].kind == T::Inline) {
                    DataType();
                    f.inline_data = Take(T::Inline, Tr(L"Expected {data}", L"Ожидались {данные}")).raw;
                    f.has_inline = true;
                }
                if (frames.size() >= kMaxNodes) TooLarge();
                frames.push_back(std::move(f));
            } else if (IsKey(L"entity")) {
                ++i_;
                QualifiedName();
            } else if (IsKey(L"data")) {
                ++i_;
                const std::wstring name = Take(T::Id, Tr(L"Expected a data name", L"Ожидалось имя данных")).raw;
                DataType();
                const std::wstring block = Take(T::Block, Tr(L"Expected a { block }", L"Ожидался { блок }")).raw;
                data.push_back({name, block});
            } else if (IsKey(L"note")) {
                ++i_;
                Take(T::Fid, Tr(L"Expected a frame number", L"Ожидался номер кадра"));
                DataType();
                Take(T::Block, Tr(L"Expected a { block }", L"Ожидался { блок }"));
            } else if (IsKey(L"gwt")) {
                ++i_;
                Take(T::Fid, Tr(L"Expected a frame number", L"Ожидался номер кадра"));
                Expect(L"given");
                Statements();
                if (IsKey(L"when")) {
                    ++i_;
                    Statements();
                }
                Expect(L"then");
                Statements();
            } else {
                Fail(toks_[i_].text.pos, Tr(L"Expected tf, rf, entity, data, note or gwt", L"Ожидалось tf, rf, entity, data, note или gwt"));
            }
        }
    }

    std::vector<Frame> frames;
    std::vector<std::pair<std::wstring, std::wstring>> data;  // name, block

private:
    bool IsKey(const wchar_t* w) const { return toks_[i_].kind == T::Keyword && toks_[i_].word == w; }
    void Expect(const wchar_t* w) {
        if (!IsKey(w)) Fail(toks_[i_].text.pos, Tr(L"Expected \"", L"Ожидалось «") + std::wstring(w) + Tr(L"\"", L"»"));
        ++i_;
    }
    Text Take(T kind, std::wstring message) {
        if (toks_[i_].kind != kind) Fail(toks_[i_].text.pos, std::move(message));
        return toks_[i_++].text;
    }
    std::wstring Type() {
        static const wchar_t* kTypes[] = {L"rmo", L"readmodel", L"ui", L"cmd", L"command", L"evt", L"event", L"pcr", L"processor"};
        for (const wchar_t* t : kTypes) {
            if (IsKey(t)) {
                ++i_;
                return t;
            }
        }
        Fail(toks_[i_].text.pos, Tr(L"Expected ui, cmd, evt, rmo or pcr", L"Ожидалось ui, cmd, evt, rmo или pcr"));
    }
    Text QualifiedName() {
        Text t = Take(T::Id, Tr(L"Expected a name", L"Ожидалось имя"));
        while (IsKey(L".")) {
            ++i_;
            const Text part = Take(T::Id, Tr(L"Expected a name", L"Ожидалось имя"));
            t.raw += L"." + part.raw;
            t.len = part.pos + part.len - t.pos;
        }
        return t;
    }
    // `json` and the like before data.
    void DataType() {
        if (!IsKey(L"`")) return;
        ++i_;
        static const wchar_t* kTypes[] = {L"json", L"jsobj", L"figma", L"salt", L"uri", L"md", L"html", L"text"};
        bool ok = false;
        for (const wchar_t* t : kTypes) ok = ok || IsKey(t);
        if (!ok) Fail(toks_[i_].text.pos, Tr(L"Expected a data type", L"Ожидался тип данных"));
        ++i_;
        Expect(L"`");
    }
    bool IsType() const {
        static const wchar_t* kTypes[] = {L"rmo", L"readmodel", L"ui", L"cmd", L"command", L"evt", L"event", L"pcr", L"processor"};
        return std::any_of(std::begin(kTypes), std::end(kTypes), [&](const wchar_t* t) { return IsKey(t); });
    }
    // given/when/then: kinds and names, at least one.
    void Statements() {
        do {
            Type();
            Take(T::Id, Tr(L"Expected a name", L"Ожидалось имя"));
        } while (IsType());
    }

    const Source& src_;
    std::vector<Token> toks_;
    size_t i_ = 0;
};

using M = SceneBuilder::Measured;

constexpr float kLaneMin = 70, kLanePad = 15, kLaneGap = 10, kBoxPad = 10, kOverlap = 90;
constexpr float kBoxMinW = 80, kBoxMaxW = 450, kBoxMinH = 80, kBoxMaxH = 750, kContentX = 250, kTextMax = 430;
constexpr float kFont = 16, kLineH = 19;

struct Lane {
    int index = 0;
    std::wstring label;
    float r = 0, y = 0, height = kLaneMin, max_height = kLaneMin;
};

struct Box {
    size_t frame = 0;
    int lane = 0;  // its index
    float x = 0, r = 0, w = 0, h = 0;
    std::vector<std::wstring> name;  // the name's lines
    std::wstring code;               // the data shown
    bool has_code = false;
};

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b) : p_(p), b_(b) {}

    std::shared_ptr<DiagramScene> Build() {
        std::map<int, Lane> lanes;
        std::vector<Box> boxes;
        std::vector<std::pair<size_t, size_t>> relations;  // source box, target box
        std::optional<int> previous;
        float max_r = 0;
        for (size_t k = 0; k < p_.frames.size(); ++k) {
            const Frame& f = p_.frames[k];
            Box box;
            box.frame = k;
            // Mermaid's measure of the box's text: its HTML as text, lines split at <br/>.
            const auto [text_w, text_h] = Content(f, &box);
            const float w = std::max(kBoxMinW, std::min(kBoxMaxW, text_w + 20)) + 2 * kBoxPad;
            const float h = std::max(kBoxMinH, std::min(kBoxMaxH, text_h + 20)) + 2 * kBoxPad;
            const auto [index, label] = LaneOf(f, lanes);
            box.lane = index;
            auto [it, fresh] = lanes.try_emplace(index);
            Lane& lane = it->second;
            if (fresh) {
                lane.index = index;
                lane.label = label;
            }
            const Box* last = boxes.empty() ? nullptr : &boxes.back();
            float x;
            const auto prev = previous ? lanes.find(*previous) : lanes.end();
            if (prev == lanes.end()) x = kContentX;
            else if (prev->second.index == lane.index && lane.r != 0) x = lane.r + kBoxPad;
            else if (!last) x = kContentX;
            else x = last->r - kOverlap + kBoxPad;
            const float r = x + w + kBoxPad;
            float most = r;
            for (const auto& [i, l] : lanes) {
                if (!(fresh && i == index)) most = std::max(most, l.r);
            }
            max_r = most;
            lane.r = x + w;
            lane.max_height = std::max(lane.max_height, h);
            lane.height = std::max(kLaneMin, lane.max_height) + 2 * kLanePad;
            box.x = x;
            box.r = r;
            box.w = w;
            box.h = h;
            boxes.push_back(std::move(box));
            previous = index;
            float y = 0;
            for (auto& [i, l] : lanes) {
                l.y = y;
                y += l.height + kLaneGap;
            }
            // Its arrows: from the frames it names, else from the last box
            // in another lane (none for the first frame or a reset).
            if (f.reset || (k == 0 && f.sources.empty())) continue;
            const size_t target = FindBoxIn(boxes, f.name.raw);
            if (!f.sources.empty()) {
                for (size_t j = 0; j < p_.frames.size(); ++j) {
                    if (std::find(f.sources.begin(), f.sources.end(), p_.frames[j].name.raw) == f.sources.end()) continue;
                    const size_t source = FindBoxIn(boxes, p_.frames[j].name.raw);
                    if (source < boxes.size()) relations.push_back({source, target});
                    if (relations.size() > kMaxEdges * 10) TooLarge();
                }
            } else {
                for (size_t j = k; j-- > 0;) {
                    if (boxes[j].lane != boxes[target].lane) {
                        relations.push_back({j, target});
                        break;
                    }
                }
            }
        }
        // Lanes, boxes, arrows.
        for (const auto& [i, lane] : lanes) {
            b_.Rect(D2D1::RectF(0, lane.y, max_r + kLanePad, lane.y + lane.height), 3, Role(static_cast<uint8_t>(kDgEventModel + 10)),
                    Role(static_cast<uint8_t>(kDgEventModel + 11)));
            RichText t;
            t.text = lane.label;
            const M m = b_.Measure(t, 0, true, kFont / b_.size());
            b_.Text(m, D2D1::Point2F(30, lane.y + 30 - 1.17f * kFont), Role(kDgText));
        }
        for (const Box& box : boxes) DrawBox(box, lanes.at(box.lane).y + kLanePad);
        for (const auto& [s, t] : relations) {
            const Box &a = boxes[s], &z = boxes[t];
            const float ay = lanes.at(a.lane).y + kLanePad, zy = lanes.at(z.lane).y + kLanePad;
            const bool up = ay > zy;
            const D2D1_POINT_2F from = D2D1::Point2F(a.x + a.w * 2 / 3, up ? ay : ay + a.h), to = D2D1::Point2F(z.x + z.w / 3, up ? zy + z.h : zy);
            const float dx = to.x - from.x, dy = to.y - from.y, d = std::sqrt(dx * dx + dy * dy);
            if (!(d > 0)) continue;
            b_.Begin(from);
            b_.LineTo(to);
            b_.End(Role(kDgNone), Role(kDgLine), 1);
            // Mermaid's arrowhead: 10 long, 7 wide, its tip at the end.
            const float ux = dx / d, uy = dy / d;
            b_.Polygon({to, D2D1::Point2F(to.x - ux * 10 - uy * 3.5f, to.y - uy * 10 + ux * 3.5f), D2D1::Point2F(to.x - ux * 10 + uy * 3.5f, to.y - uy * 10 - ux * 3.5f)},
                       Role(kDgLine), Role(kDgNone));
        }
        return b_.FinishFitted(30);
    }

private:
    size_t FindBoxIn(const std::vector<Box>& boxes, const std::wstring& name) const {
        for (size_t i = 0; i < boxes.size(); ++i) {
            if (p_.frames[boxes[i].frame].name.raw == name) return i;
        }
        return boxes.size();
    }

    // Mermaid's calculateSwimlaneProps: UI and processors from 0, commands
    // and read models from 100, events from 200; a namespace gets the next
    // lane of its kind (each time: Mermaid finds none it made before).
    std::pair<int, std::wstring> LaneOf(const Frame& f, const std::map<int, Lane>& lanes) const {
        const std::wstring& id = f.entity.raw;
        std::wstring ns;
        if (std::count(id.begin(), id.end(), L'.') == 1) ns = id.substr(0, id.find(L'.'));
        int base;
        std::wstring label, prefix;
        if (f.type == L"ui" || f.type == L"pcr" || f.type == L"processor") {
            base = 0;
            label = L"UI/Automation";
            prefix = L"UI/A: ";
        } else if (f.type == L"rmo" || f.type == L"readmodel" || f.type == L"cmd" || f.type == L"command") {
            base = 100;
            label = L"Command/Read Model";
            prefix = L"C/RM: ";
        } else {
            base = 200;
            label = L"Events";
            prefix = L"Stream: ";
        }
        if (ns.empty()) return {base, label};
        int next = base;
        for (const auto& [i, l] : lanes) {
            if (i > base && i < base + 100) next = std::max(next, i);
        }
        return {next + 1, prefix + ns};
    }

    // The name, the data (inline or a data entity's block), and Mermaid's
    // measure of them.
    std::pair<float, float> Content(const Frame& f, Box* box) {
        const std::wstring& id = f.entity.raw;
        const std::wstring name = std::count(id.begin(), id.end(), L'.') == 1 ? id.substr(id.find(L'.') + 1) : id;
        box->name = Wrap(name);
        std::wstring html = L"<b>" + Join(box->name, L"<br/>") + L"</b>";
        std::optional<std::wstring> code;
        if (f.has_inline) {
            std::wstring v = f.inline_data;
            const size_t open = v.find(L'{');
            v = open == std::wstring::npos ? v : v.substr(open + 1);
            const size_t close = v.rfind(L'}');
            v = close == std::wstring::npos || close == 0 ? std::wstring() : v.substr(0, close - 1);
            code = v;
        }
        if (!f.data_ref.empty()) {
            for (const auto& [n, block] : p_.data) {
                if (n != f.data_ref) continue;
                std::wstring v = block;
                const size_t open = v.find(L"{\n");
                v = open == std::wstring::npos ? v : v.substr(open + 2);
                const size_t close = v.rfind(L'}');
                v = close == std::wstring::npos || close == 0 ? std::wstring() : v.substr(0, close - 1);
                code = v;
                break;
            }
        }
        std::vector<std::wstring> lines = Lines(html);
        if (code) {
            // (HTML collapses its line ends; spaces are kept.)
            std::wstring flat;
            for (const wchar_t c : *code) flat.push_back(c == L'\n' || c == L'\r' ? L' ' : c);
            box->code = Join(Wrap(flat), L"\n");
            box->has_code = true;
            std::wstring shown = Join(Wrap(flat), L"<br/>");
            for (size_t k = shown.find(L' '); k != std::wstring::npos; k = shown.find(L' ', k + 6)) shown.replace(k, 1, L"&nbsp;");
            if (!f.data_ref.empty()) shown += L"<br/>";
            html += L"<br/><br/><code style=\"text-align: left; display: block;max-width:430px\">" + shown + L"</code>";
            lines = Lines(html);
        }
        float w = 0;
        for (const std::wstring& l : lines) w = std::max(w, Width(l));
        return {code ? w / 3 : w, static_cast<float>(lines.size()) * kLineH};
    }

    static std::vector<std::wstring> Lines(const std::wstring& html) {
        std::vector<std::wstring> out;
        size_t s = 0;
        for (size_t k = html.find(L"<br/>"); k != std::wstring::npos; k = html.find(L"<br/>", s)) {
            out.push_back(html.substr(s, k - s));
            s = k + 5;
        }
        out.push_back(html.substr(s));
        return out;
    }
    static std::wstring Join(const std::vector<std::wstring>& parts, const wchar_t* with) {
        std::wstring out;
        for (size_t i = 0; i < parts.size(); ++i) out += (i ? with : L"") + parts[i];
        return out;
    }

    float Width(const std::wstring& s) {
        if (s.empty()) return 0;
        RichText t;
        t.text = s;
        return b_.Measure(t, 0, true, kFont / b_.size()).w;
    }

    // Mermaid's wrapLabel: words into lines of at most 430 (bold 16px),
    // words too long broken.
    std::vector<std::wstring> Wrap(const std::wstring& text) {
        if (const auto it = wrapped_.find(text); it != wrapped_.end()) return it->second;
        std::vector<std::wstring> lines;
        std::wstring line;
        auto place = [&](const std::wstring& word) {
            const float w = Width(word);
            if (w <= kTextMax) {
                line = word;
                return;
            }
            // (In pieces of about a line's worth of characters.)
            const size_t per = std::max<size_t>(1, static_cast<size_t>(static_cast<float>(word.size()) * kTextMax / w));
            size_t k = 0;
            for (; k + per < word.size(); k += per) lines.push_back(word.substr(k, per));
            line = word.substr(k);
        };
        size_t s = 0;
        for (bool first = true;; first = false) {
            size_t e = text.find(L' ', s);
            if (e == std::wstring::npos) e = text.size();
            const std::wstring word = text.substr(s, e - s);
            if (first) {
                place(word);
            } else if (Width(line + L" " + word) <= kTextMax) {
                line += L" " + word;
            } else {
                lines.push_back(line);
                place(word);
            }
            if (e >= text.size() || lines.size() > 200) break;
            s = e + 1;
        }
        lines.push_back(line);
        wrapped_[text] = lines;
        return lines;
    }

    void DrawBox(const Box& box, float y) {
        const Frame& f = p_.frames[box.frame];
        int kind;
        if (f.type == L"ui") kind = 0;
        else if (f.type == L"pcr" || f.type == L"processor") kind = 1;
        else if (f.type == L"rmo" || f.type == L"readmodel") kind = 2;
        else if (f.type == L"cmd" || f.type == L"command") kind = 3;
        else kind = 4;
        b_.Rect(D2D1::RectF(box.x, y, box.x + box.w, y + box.h), 3, Role(static_cast<uint8_t>(kDgEventModel + kind * 2)),
                Role(static_cast<uint8_t>(kDgEventModel + kind * 2 + 1)));
        // Its text: the name in bold, centred; the data below, left; both
        // in the middle of the box.
        RichText name;
        name.text = Join(box.name, L"\n");
        name.spans.push_back({0, static_cast<uint32_t>(name.text.size()), true, false});
        M n = b_.Measure(name, 0, true, kFont / b_.size());
        LineHeight(n, kFont * 1.15f);
        M c;
        float code_h = 0;
        if (box.has_code && !box.code.empty()) {
            RichText code;
            code.text = box.code;
            RichText::Span s{0, static_cast<uint32_t>(code.text.size())};
            s.mono = true;
            code.spans.push_back(s);
            c = b_.Measure(code, 0, false, 13.f / b_.size(), false);
            LineHeight(c, 13 * 1.15f);
            code_h = c.h;
        }
        const float inner_x = box.x + kBoxPad, inner_w = box.w - 2 * kBoxPad;
        const float total = n.h + (box.has_code ? kFont * 1.15f + code_h : 0);
        float top = y + kBoxPad + (box.h - 2 * kBoxPad - total) / 2;
        const uint32_t pos = f.entity.pos, len = f.entity.len;
        b_.Text(n, D2D1::Point2F(inner_x + (inner_w - n.w) / 2, top), Role(kDgText), pos, len);
        if (c.layout) {
            top += n.h + kFont * 1.15f;
            b_.Text(c, D2D1::Point2F(inner_x, top), Role(kDgText));
        }
    }

    static void LineHeight(M& m, float lh) {
        if (!m.layout) return;
        m.layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lh, lh * 0.8f);
        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(m.layout->GetMetrics(&tm))) m.h = tm.height;
    }

    const Parser& p_;
    SceneBuilder& b_;
    std::map<std::wstring, std::vector<std::wstring>> wrapped_;
};

}  // namespace

std::shared_ptr<DiagramScene> EventModel(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder).Build();
}

}  // namespace mdv::mermaid

// Mermaid user journeys: sections of tasks in a row, each task with its
// actors and a face showing how it felt (its score, 1 to 5), a legend of
// the actors, the timeline arrow below.
#include <algorithm>
#include <cmath>
#include <cwchar>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

struct Task {
    Text text;
    double score = 3;
    std::vector<int> actors;
    int section = -1;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"journey")) c_.Fail(Tr(L"Expected \"journey\"", L"Ожидалось «journey»"));
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
        // Actors in order, as Mermaid lists (and colors) them.
        std::vector<int> order(actors.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return actors[static_cast<size_t>(a)].raw < actors[static_cast<size_t>(b)].raw; });
        std::vector<int> rank(actors.size());
        std::vector<Text> sorted;
        for (size_t i = 0; i < order.size(); ++i) {
            rank[static_cast<size_t>(order[i])] = static_cast<int>(i);
            sorted.push_back(actors[static_cast<size_t>(order[i])]);
        }
        actors = std::move(sorted);
        for (Task& t : tasks) {
            for (int& a : t.actors) a = rank[static_cast<size_t>(a)];
        }
    }

    Text title;
    std::vector<Text> sections;
    std::vector<Task> tasks;
    std::vector<Text> actors;  // where each is first named

private:
    void Statement() {
        if (c_.EatKeyword(L"title")) {
            title = Rest();
            return;
        }
        if (c_.EatKeyword(L"section")) {
            if (sections.size() >= kMaxNodes) c_.Fail(Tr(L"Too many sections", L"Слишком много разделов"));
            sections.push_back(Rest());
            return;
        }
        if (c_.EatKeyword(L"accDescr")) {
            c_.SkipBlanks();
            if (c_.Eat(L'{')) {
                while (!c_.AtEnd() && c_.Peek() != L'}') c_.Advance();
                c_.Eat(L'}');
            } else {
                c_.LineRest();
            }
            return;
        }
        if (c_.EatKeyword(L"accTitle")) {
            c_.LineRest();
            return;
        }
        // Task text: score[: actor, actor]
        Task t;
        const size_t start = c_.pos();
        while (!c_.AtStatementEnd() && c_.Peek() != L':') c_.Advance();
        const std::wstring_view text = Trim(src_.text.substr(start, c_.pos() - start));
        if (text.empty() || !c_.Eat(L':')) c_.Fail(Tr(L"Expected a task (Task: score: actors)", L"Ожидалась задача (Задача: оценка: участники)"));
        if (text.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        t.text.raw = std::wstring(text);
        t.text.pos = static_cast<uint32_t>(text.data() - src_.text.data());
        t.text.len = static_cast<uint32_t>(text.size());
        c_.SkipBlanks();
        const size_t num = c_.pos();
        while (!c_.AtStatementEnd() && c_.Peek() != L':') c_.Advance();
        // A number (as JavaScript's Number reads it: none is 0).
        const std::wstring score(Trim(src_.text.substr(num, c_.pos() - num)));
        wchar_t* end = nullptr;
        const double v = score.empty() ? 0.0 : std::wcstod(score.c_str(), &end);
        if (!score.empty() && (!end || *end || !std::isfinite(v))) c_.Fail(Tr(L"Expected a score", L"Ожидалась оценка"));
        t.score = std::clamp(v, 0.0, 5.0);
        if (c_.Eat(L':')) {
            for (const std::wstring_view a : Split(c_.LineRest())) {
                auto it = std::find_if(actors.begin(), actors.end(), [&](const Text& x) { return x.raw == a; });
                if (it == actors.end()) {
                    if (actors.size() >= 64) c_.Fail(Tr(L"Too many actors", L"Слишком много участников"));
                    actors.push_back(Text{std::wstring(a), static_cast<uint32_t>(a.data() - src_.text.data()), static_cast<uint32_t>(a.size())});
                    it = actors.end() - 1;
                }
                t.actors.push_back(static_cast<int>(it - actors.begin()));
            }
        }
        t.section = static_cast<int>(sections.size()) - 1;
        if (tasks.size() >= kMaxEdges) c_.Fail(Tr(L"Too many tasks", L"Слишком много задач"));
        tasks.push_back(std::move(t));
    }

    static std::vector<std::wstring_view> Split(std::wstring_view s) {
        std::vector<std::wstring_view> out;
        size_t start = 0;
        while (start <= s.size()) {
            size_t e = s.find(L',', start);
            if (e == std::wstring_view::npos) e = s.size();
            const std::wstring_view part = Trim(s.substr(start, e - start));
            if (!part.empty()) out.push_back(part);
            start = e + 1;
        }
        return out;
    }

    Text Rest() {
        c_.SkipBlanks();
        const std::wstring_view r = c_.LineRest();
        Text t;
        t.raw = std::wstring(r);
        t.pos = static_cast<uint32_t>(r.data() - src_.text.data());
        t.len = static_cast<uint32_t>(r.size());
        if (t.raw.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return t;
    }

    const Source& src_;
    Cursor c_;
};

constexpr float kMargin = 20;
constexpr float kTaskW = 150, kTaskH = 50, kTaskGap = 20;
constexpr float kFaceR = 15, kScoreStep = 30;
// Mermaid's journey.actorColours.
constexpr uint32_t kActorColors[] = {0xFF8FBC8F, 0xFF7CFC00, 0xFF00FFFF, 0xFF20B2AA, 0xFFB0E0E6, 0xFFFFFFE0};

// A face: a smile for good scores, a frown for bad, a straight mouth between.
void AddFace(SceneBuilder& b, D2D1_POINT_2F c, double score) {
    const DgColor edge = Argb(0xFF999999);
    b.Ellipse(D2D1::RectF(c.x - kFaceR, c.y - kFaceR, c.x + kFaceR, c.y + kFaceR), Argb(0xFFFFF8DC), edge, 2.f);
    for (const float dx : {-5.f, 5.f}) {
        b.Ellipse(D2D1::RectF(c.x + dx - 1.5f, c.y - 6.5f, c.x + dx + 1.5f, c.y - 3.5f), Argb(0xFF666666), Argb(0xFF666666));
    }
    if (score > 3) {
        b.Begin(D2D1::Point2F(c.x - 7, c.y + 2));
        b.CubicTo(D2D1::Point2F(c.x - 5, c.y + 8), D2D1::Point2F(c.x + 5, c.y + 8), D2D1::Point2F(c.x + 7, c.y + 2));
    } else if (score < 3) {
        b.Begin(D2D1::Point2F(c.x - 7, c.y + 8));
        b.CubicTo(D2D1::Point2F(c.x - 5, c.y + 2), D2D1::Point2F(c.x + 5, c.y + 2), D2D1::Point2F(c.x + 7, c.y + 8));
    } else {
        b.Begin(D2D1::Point2F(c.x - 6, c.y + 6));
        b.LineTo(D2D1::Point2F(c.x + 6, c.y + 6));
    }
    b.End(Role(kDgNone), Argb(0xFF666666), 1.5f);
}

std::shared_ptr<DiagramScene> Build(const Parser& p, SceneBuilder& b, const Source& src) {
    using M = SceneBuilder::Measured;
    const Text& own = p.title;
    const std::wstring& title_text = own.raw.empty() ? src.title : own.raw;
    M title;
    if (!title_text.empty()) title = b.Measure(ParseLabel(title_text, false), 0, true, 1.5f);
    // The actors: a colored dot and the name each, on the left.
    std::vector<M> names;
    float legend_w = 0;
    for (const Text& a : p.actors) {
        names.push_back(b.Measure(ParseLabel(a.raw, false), 0, false, 1.f, false));
        legend_w = std::max(legend_w, names.back().w + 26);
    }
    const float x0 = kMargin + (legend_w > 0 ? legend_w + 20 : 0.f);
    const float top = kMargin + (title.layout ? title.h + 16 : 0.f);
    // Tasks side by side, as wide as their text wraps into.
    std::vector<M> texts;
    std::vector<float> xs;
    float x = x0;
    for (const Task& t : p.tasks) {
        texts.push_back(b.Measure(ParseLabel(t.text.raw, false), kTaskW - 16, false, 0.875f));
        xs.push_back(x);
        x += kTaskW + kTaskGap;
    }
    const float right = p.tasks.empty() ? x0 + kTaskW : x - kTaskGap;
    const float section_y = top, task_y = top + kTaskH + 10;
    const float face_top = task_y + kTaskH + 40;             // score 5
    const float face_bottom = face_top + 4 * kScoreStep;      // score 1
    const float arrow_y = face_bottom + kFaceR + 30;
    const float width = std::max(right, x0 + title.w) + kMargin;
    const float legend_bottom = top + 10 + static_cast<float>(names.size()) * 28;  // (below its last row)
    const float height = std::max(arrow_y + 12, legend_bottom) + kMargin;
    if (title.layout) {
        b.Text(title, D2D1::Point2F(x0, kMargin), Role(kDgText), own.raw.empty() ? src.title_pos : own.pos,
               own.raw.empty() ? src.title_len : own.len);
    }
    // The legend.
    for (size_t i = 0; i < names.size(); ++i) {
        const float y = top + 10 + static_cast<float>(i) * 28;
        const DgColor c = Argb(kActorColors[i % std::size(kActorColors)]);
        b.Ellipse(D2D1::RectF(kMargin, y - 7, kMargin + 14, y + 7), c, Argb(0xFF000000), 1.f);
        b.Text(names[i], D2D1::Point2F(kMargin + 22, y - names[i].h / 2), Role(kDgText), p.actors[i].pos, p.actors[i].len);
    }
    // Sections over their tasks, in Mermaid's fillType colors in turn (seven
    // of them, counting the sections with tasks).
    std::vector<int> color(p.sections.size(), 0);
    for (int s = -1, n = 0; const Task& t : p.tasks) {
        if (t.section >= 0 && t.section != s) color[static_cast<size_t>(t.section)] = n++ % 7;
        s = t.section;
    }
    for (size_t k = 0; k < p.tasks.size();) {
        const int s = p.tasks[k].section;
        size_t e = k;
        while (e < p.tasks.size() && p.tasks[e].section == s) ++e;
        if (s >= 0) {
            const DgColor fill = Role(static_cast<uint8_t>(kDgFillType + color[static_cast<size_t>(s)]));
            const D2D1_RECT_F r = D2D1::RectF(xs[k], section_y, xs[e - 1] + kTaskW, section_y + kTaskH);
            b.Rect(r, 3, fill, Role(kDgNodeStroke), 1.f);
            const Text& st = p.sections[static_cast<size_t>(s)];
            const M m = b.Measure(ParseLabel(st.raw, false), r.right - r.left - 16, true, 0.875f);
            b.Text(m, D2D1::Point2F((r.left + r.right) / 2 - m.w / 2, (r.top + r.bottom) / 2 - m.h / 2), Role(kDgText), st.pos, st.len);
        }
        k = e;
    }
    // Tasks: the box, its actors' dots, a dashed line down to its face.
    for (size_t k = 0; k < p.tasks.size(); ++k) {
        const Task& t = p.tasks[k];
        const float cx = xs[k] + kTaskW / 2;
        const float face_y = face_top + static_cast<float>(5 - std::max(1.0, t.score)) * kScoreStep;
        b.Begin(D2D1::Point2F(cx, task_y + kTaskH));
        b.LineTo(D2D1::Point2F(cx, face_y - kFaceR));
        b.End(Role(kDgNone), Role(kDgMuted), 1.f, 1);
        const DgColor fill = t.section >= 0 ? Role(static_cast<uint8_t>(kDgFillType + color[static_cast<size_t>(t.section)])) : Role(kDgNodeFill);
        b.Rect(D2D1::RectF(xs[k], task_y, xs[k] + kTaskW, task_y + kTaskH), 3, fill, Role(kDgNodeStroke), 1.f);
        const M& m = texts[k];
        b.Text(m, D2D1::Point2F(cx - m.w / 2, task_y + (kTaskH - m.h) / 2), Role(kDgText), t.text.pos, t.text.len);
        for (size_t a = 0; a < t.actors.size(); ++a) {
            const float ax = xs[k] + 8 + static_cast<float>(a) * 10;
            const DgColor c = Argb(kActorColors[static_cast<size_t>(t.actors[a]) % std::size(kActorColors)]);
            b.Ellipse(D2D1::RectF(ax - 4, task_y - 4, ax + 4, task_y + 4), c, Argb(0xFF000000), 1.f);
        }
        AddFace(b, D2D1::Point2F(cx, face_y), t.score);
    }
    // The timeline.
    b.Begin(D2D1::Point2F(x0, arrow_y));
    b.LineTo(D2D1::Point2F(right - 10, arrow_y));
    b.End(Role(kDgNone), Role(kDgLine), 4.f);
    AddMarker(b, Marker::Arrow, D2D1::Point2F(right, arrow_y), D2D1::Point2F(x0, arrow_y), Role(kDgLine), 2.f);
    return b.Finish(width, height);
}

}  // namespace

std::shared_ptr<DiagramScene> Journey(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Build(p, *builder, src);
}

}  // namespace mdv::mermaid

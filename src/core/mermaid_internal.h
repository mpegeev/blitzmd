// Shared parts of the Mermaid diagram types: reading the source, CSS styles,
// edge geometry and markers.
#pragma once

#include <d2d1.h>
#include <dwrite.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/diagram.h"
#include "core/graphlayout.h"
#include "core/util.h"

namespace mdv::mermaid {

// Parse errors, at a position of the source.
struct ParseError {
    size_t pos = 0;
    std::wstring message;
};
[[noreturn]] inline void Fail(size_t pos, std::wstring message) { throw ParseError{pos, std::move(message)}; }
// An error about the whole diagram, not one of its lines.
constexpr size_t kNoLine = static_cast<size_t>(-1);
[[noreturn]] inline void TooLarge() {
    Fail(kNoLine, Tr(L"The diagram is too large", L"Диаграмма слишком большая"));
}

// Size limits (hostile input).
constexpr size_t kMaxNodes = 2000;
constexpr size_t kMaxEdges = 500;    // Mermaid's own limit (maxEdges)
constexpr size_t kMaxLabel = 4000;   // characters of one label
constexpr int kMaxNesting = 24;

// The source with its front matter (--- title: ... ---) and %%{...}%%
// directives set aside; positions refer to the whole source.
struct Source {
    std::wstring_view text;
    size_t body = 0;          // where the diagram starts
    std::wstring title;       // front matter title
    uint32_t title_pos = 0, title_len = 0;
    std::wstring display_mode;  // front matter displayMode (Gantt charts), at any depth
};
Source Preprocess(std::wstring_view text);

// Reads statements: blanks, line ends (newline or ';'), %% comments.
class Cursor {
public:
    Cursor(std::wstring_view s, size_t pos) : s_(s), pos_(pos) {}
    size_t pos() const { return pos_; }
    void set_pos(size_t p) { pos_ = p; }
    bool AtEnd() const { return pos_ >= s_.size(); }
    wchar_t Peek(size_t k = 0) const { return pos_ + k < s_.size() ? s_[pos_ + k] : L'\0'; }
    std::wstring_view Rest() const { return s_.substr(std::min(pos_, s_.size())); }
    void Advance(size_t n = 1) { pos_ = std::min(pos_ + n, s_.size()); }
    void SkipBlanks();                  // spaces and tabs
    // Blank lines, separators and comments before the next statement.
    void SkipStatementGap();
    bool AtStatementEnd() const;        // newline, ';', '%%' or the end
    void SkipToLineEnd();               // to (not past) the next newline
    std::wstring_view LineRest();       // up to the statement end, trimmed
    std::wstring_view WholeLine();      // up to the newline (';' and all), trimmed
    bool Eat(wchar_t c);
    bool EatStr(std::wstring_view str);
    // A keyword not followed by a word character. Flowchart statements are
    // case-sensitive in Mermaid ("End" is a node), sequence diagrams not.
    bool EatKeyword(std::wstring_view word, bool match_case = false);
    [[noreturn]] void Fail(std::wstring message) const { mermaid::Fail(pos_, std::move(message)); }

private:
    std::wstring_view s_;
    size_t pos_;
};

bool IsWordChar(wchar_t c);

// "Line 3: message".
std::wstring FormatError(std::wstring_view source, const ParseError& e);

// CSS colors (#rgb, #rrggbb, #rrggbbaa, rgb(), rgba(), hsl(), names).
bool ParseCssColor(std::wstring_view value, uint32_t* argb);

// Style properties from style / classDef / linkStyle statements.
struct Style {
    DgColor fill, stroke, color;        // role kDgNone = not set
    float stroke_width = -1;
    int dash = -1;                      // 0 solid, 1 dashed
    int bold = -1, italic = -1;
    void Apply(std::wstring_view css);  // "fill:#f9f,stroke:#333,stroke-width:4px"
    void Merge(const Style& over);      // `over` wins where set
};

// Edge ends.
enum class Marker : uint8_t {
    None,
    Arrow,           // filled triangle
    Circle,          // filled circle
    Cross,           // x
    OpenArrow,       // class diagrams: hollow triangle (inheritance)
    Diamond,         // filled diamond (composition)
    OpenDiamond,     // hollow diamond (aggregation)
    LineArrow,       // open "V" (dependency)
    Lollipop,        // class diagrams: hollow circle (a provided interface)
};
float MarkerLength(Marker m);
// Draws a marker whose tip is at `tip`, pointing away from `from`.
void AddMarker(SceneBuilder& b, Marker m, D2D1_POINT_2F tip, D2D1_POINT_2F from, DgColor color, float stroke_width);

// A smooth curve through a polyline (d3's "basis" curve, Mermaid's default
// for edges): Begin() ... for SceneBuilder, the caller ends the path.
void AddBasisCurve(SceneBuilder& b, const std::vector<D2D1_POINT_2F>& pts);

// Where the ray from a shape's center through `outside` leaves it (`outside`
// may lie inside too).
D2D1_POINT_2F ClipRect(D2D1_POINT_2F center, float w, float h, D2D1_POINT_2F outside);
D2D1_POINT_2F ClipEllipse(D2D1_POINT_2F center, float rx, float ry, D2D1_POINT_2F outside);
D2D1_POINT_2F ClipPolygon(D2D1_POINT_2F center, const std::vector<D2D1_POINT_2F>& poly, D2D1_POINT_2F outside);

// Moves `p` towards `from` by `d`.
D2D1_POINT_2F Retreat(D2D1_POINT_2F p, D2D1_POINT_2F from, float d);

// Drops bends where a clipped end already is (a bend on a box's border the
// end was clipped to): the ends' markers need a direction.
void DropDoubledEnds(std::vector<D2D1_POINT_2F>* pts);

// Diagram types.
std::shared_ptr<DiagramScene> Flowchart(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> Sequence(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> ClassDiagram(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> StateDiagram(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> ErDiagram(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> PieChart(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> Journey(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> Gantt(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> Timeline(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> Mindmap(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> QuadrantChart(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> XyChart(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> GitGraph(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> Kanban(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> Packet(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> RadarChart(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> Treemap(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> RequirementDiagram(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> SankeyDiagram(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> BlockDiagram(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> C4Diagram(const Source& src, SceneBuilder* builder);
std::shared_ptr<DiagramScene> ArchitectureDiagram(const Source& src, SceneBuilder* builder);

}  // namespace mdv::mermaid

// Diagrams (Mermaid) as scenes: shapes, paths and text laid out once at a
// reference size and drawn scaled into any rectangle, in the colors of
// GitHub's Mermaid themes (light or dark, chosen at draw time).
#pragma once

#include <d2d1.h>
#include <dwrite.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/util.h"

namespace mdv {

// Colors: a role of the theme, or an explicit ARGB color (style/classDef).
enum DiagramRole : uint8_t {
    kDgNone,            // not painted
    kDgArgb,            // explicit color
    kDgText,
    kDgLine,
    kDgNodeFill,
    kDgNodeStroke,
    kDgClusterFill,
    kDgClusterStroke,
    kDgLabelBg,         // behind edge labels
    kDgNoteFill,
    kDgNoteStroke,
    kDgNoteText,
    kDgActorFill,
    kDgActorStroke,
    kDgActivationFill,
    kDgActivationStroke,
    kDgBoxFill,         // sequence loop/alt boxes: their title tab
    kDgPage,            // the page behind the diagram
    kDgMuted,           // secondary text
    kDgTint,            // an explicit background color, toned down in the dark theme
    kDgCategory,        // + n: categorical colors (pie slices), blended over the page
    kDgCategorySolid = kDgCategory + 12,  // + n: the same, not blended (the pie's legend)
    kDgFillType = kDgCategorySolid + 12,  // + n: Mermaid's fillType0..7 (journey sections)
    kDgScale = kDgFillType + 8,        // + n: Mermaid's cScale0..11 (timeline, mindmap sections)
    kDgScaleText = kDgScale + 12,      // + n: text on them
    kDgScaleInv = kDgScaleText + 12,   // + n: their inverse (the line under a box)
    kDgGit = kDgScaleInv + 12,         // + n: git0..7 (the mindmap's root)
    kDgGitText = kDgGit + 8,           // + n: text on them
    kDgPlot = kDgGitText + 8,          // + n: xychart plot colors (10)
    kDgQuadrant = kDgPlot + 10,        // + n: quadrant chart fills (4)
    kDgQuadrantBorder = kDgQuadrant + 4,
    kDgScaleBright,                    // + n: cScale at 120% brightness (timeline events)
    kDgRoleCount = kDgScaleBright + 12,
};

struct DgColor {
    uint8_t role = kDgNone;
    uint32_t argb = 0;
};
inline DgColor Role(uint8_t role) { return DgColor{role, 0}; }
inline DgColor Argb(uint32_t argb) { return DgColor{kDgArgb, argb}; }
inline DgColor Tint(uint32_t argb) { return DgColor{kDgTint, argb}; }

// Text of a label with its bold/italic spans (<b>, <i>, **markdown**...).
struct RichText {
    struct Span {
        uint32_t start = 0, length = 0;
        bool bold = false, italic = false;
        bool underline = false;  // class diagrams: static members
    };
    std::wstring text;  // '\n' between lines
    std::vector<Span> spans;
};

// Mermaid label text: <br> line breaks, simple HTML (<b> <strong> <i> <em>),
// entities (&amp; #quot; #9829;), and in markdown strings (`...`) **bold**
// and *italic* with the text wrapped as Mermaid does.
RichText ParseLabel(std::wstring_view raw, bool markdown);

struct DiagramScene {
    enum class Op : uint8_t { Move, Line, Cubic, Close };
    struct Shape {
        enum class Kind : uint8_t { Rect, Ellipse, Path };
        Kind kind = Kind::Rect;
        D2D1_RECT_F box{};              // Path: its bounds
        float rx = 0, ry = 0;           // Rect: rounded corners
        uint32_t first = 0, count = 0;  // Path: its ops (points follow them)
        uint32_t first_point = 0;
        DgColor fill, stroke;
        float stroke_width = 1;
        uint8_t dash = 0;               // 0 solid, 1 dashed, 2 dotted
    };
    struct Text {
        ComPtr<IDWriteTextLayout> layout;
        D2D1_POINT_2F origin{};         // top left of the layout (of its box, turned)
        float w = 0, h = 0;             // (the box's, turned)
        DgColor color;
        bool up = false;                // turned to read upwards (a vertical axis's title)
        uint32_t src_start = 0, src_length = 0;  // its characters in the source
    };
    struct Item {
        bool text = false;
        uint32_t index = 0;
    };

    float width = 0, height = 0;
    bool error = false;                 // a note in place of a diagram: not zoomable
    std::vector<Shape> shapes;
    std::vector<Text> texts;
    std::vector<Item> items;            // drawing order
    std::vector<Op> ops;
    std::vector<D2D1_POINT_2F> points;
    // Path geometries, built on first draw (for the factory they belong to).
    mutable ComPtr<ID2D1Factory> geo_factory;
    mutable std::vector<ComPtr<ID2D1PathGeometry>> geos;
};

// Builds a scene: shapes in drawing order, and text measured with
// DirectWrite in the document's font at the reference size.
class SceneBuilder {
public:
    SceneBuilder(IDWriteFactory* dw, const std::wstring& family, float size);
    DiagramScene& scene() { return *scene_; }
    std::shared_ptr<DiagramScene> Finish(float width, float height);
    float size() const { return size_; }

    void Rect(const D2D1_RECT_F& r, float radius, DgColor fill, DgColor stroke, float stroke_width = 1,
              uint8_t dash = 0);
    void Ellipse(const D2D1_RECT_F& r, DgColor fill, DgColor stroke, float stroke_width = 1, uint8_t dash = 0);
    // Paths: Begin, then points (MoveTo starts another part); End adds it
    // with its paint.
    void Begin(D2D1_POINT_2F p);
    void MoveTo(D2D1_POINT_2F p);
    void LineTo(D2D1_POINT_2F p);
    void CubicTo(D2D1_POINT_2F c1, D2D1_POINT_2F c2, D2D1_POINT_2F p);
    void Close();
    void End(DgColor fill, DgColor stroke, float stroke_width = 1, uint8_t dash = 0);
    void Polygon(const std::vector<D2D1_POINT_2F>& pts, DgColor fill, DgColor stroke, float stroke_width = 1,
                 uint8_t dash = 0);

    struct Measured {
        ComPtr<IDWriteTextLayout> layout;
        float w = 0, h = 0;
    };
    // `wrap` > 0: lines wrap at that width. `scale` multiplies the size.
    Measured Measure(const RichText& t, float wrap = 0, bool bold = false, float scale = 1.f, bool center = true);
    // Adds measured text with its top left at `origin`.
    void Text(Measured m, D2D1_POINT_2F origin, DgColor color, uint32_t src_start = 0, uint32_t src_length = 0);
    // The same turned to read upwards, `origin` the top left of its box
    // (m.h wide, m.w high).
    void TextUp(Measured m, D2D1_POINT_2F origin, DgColor color, uint32_t src_start = 0, uint32_t src_length = 0);
    // Height of one line of text at `scale`.
    float LineHeight(float scale = 1.f) const { return size_ * 1.5f * scale; }

private:
    IDWriteTextFormat* Format(bool bold, float scale);
    ComPtr<IDWriteFactory> dw_;
    std::wstring family_;
    float size_;
    std::shared_ptr<DiagramScene> scene_;
    uint32_t path_first_ = 0, path_first_point_ = 0;
    struct FormatKey {
        bool bold;
        float scale;
        ComPtr<IDWriteTextFormat> format;
    };
    std::vector<FormatKey> formats_;
};

// A range of source characters to highlight: search hits, the current one,
// the selection.
struct DiagramHighlight {
    uint32_t start = 0, end = 0;
    uint8_t kind = 0;  // 0 search hit, 1 current hit, 2 selection
};

// Draws `scene` scaled into `dest`; parts outside the target are skipped.
void DrawDiagram(ID2D1RenderTarget* rt, const DiagramScene& scene, const D2D1_RECT_F& dest, bool dark,
                 const std::vector<DiagramHighlight>* highlights, ID2D1Brush* const highlight_brushes[3]);

// Color of a theme role (for tests and the full-window view's backdrop).
uint32_t DiagramRoleArgb(uint8_t role, bool dark);

}  // namespace mdv

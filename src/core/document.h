// Document model produced by the parser and consumed by layout/rendering.
//
// A document is a tree of blocks. Container blocks (quotes, lists, tables,
// alerts, <div>s, ...) hold child blocks; leaf blocks (paragraphs, headings,
// code blocks, summaries) hold UTF-16 text plus style runs covering it. Inline
// images are U+FFFC characters referencing Document::images; flag emoji are
// images drawn over their own characters.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mdv {

// Inline style flags (StyleRun::flags, Block::base_flags).
enum StyleFlags : uint32_t {
    kBold = 1u << 0,
    kItalic = 1u << 1,
    kStrike = 1u << 2,
    kUnderline = 1u << 3,
    kCode = 1u << 4,       // `inline code`
    kKbd = 1u << 5,        // <kbd>
    kMark = 1u << 6,       // <mark>
    kSmall = 1u << 7,      // <small>
    kSup = 1u << 8,        // <sup>, footnote references
    kSub = 1u << 9,        // <sub>
    kMath = 1u << 10,      // $math$
    kMathDisplay = 1u << 11,
    kMuted = 1u << 12,     // secondary text color
    kBig = 1u << 13,       // <big>
    kStyleBitCount = 14,
};

enum class Align : uint8_t { Default, Left, Center, Right };

enum class BlockType : uint8_t {
    Document,
    Paragraph,        // leaf
    Heading,          // leaf, level 1..6
    CodeBlock,        // leaf, runs carry syntax token kinds
    Summary,          // leaf, the clickable line of <details>
    Quote,
    List,
    ListItem,
    Table,
    TableRow,
    TableCell,
    Rule,             // thematic break / <hr>
    Alert,            // GitHub alert: > [!NOTE] ...
    Details,          // <details>, first child may be a Summary
    Div,              // generic HTML container (<div>, <center>, <dd>, ...)
    FootnoteSection,
    FootnoteDef,
};

inline bool IsLeafType(BlockType t) {
    return t == BlockType::Paragraph || t == BlockType::Heading || t == BlockType::CodeBlock ||
           t == BlockType::Summary;
}

enum class AlertKind : uint8_t { None, Note, Tip, Important, Warning, Caution };

// A run of identically styled text. Runs of a leaf cover its text completely
// and are sorted by `start`.
struct StyleRun {
    uint32_t start = 0;
    uint32_t length = 0;
    uint32_t flags = 0;
    int32_t link = -1;   // index into Document::links
    uint8_t token = 0;   // syntax TokenKind (code blocks only)
};

// An inline object: an image occupying one U+FFFC character of the leaf
// text, or a flag emoji covering its original characters (length > 1).
struct InlineObject {
    uint32_t pos = 0;
    uint32_t length = 1;  // text units covered
    int32_t image = -1;   // index into Document::images
};

struct Link {
    std::wstring url;    // entity-decoded destination
    std::wstring title;
};

struct ImageRef {
    std::wstring src;        // destination as written (entity-decoded)
    std::wstring src_light;  // <picture><source media="(prefers-color-scheme: light)">
    std::wstring src_dark;   // <picture><source media="(prefers-color-scheme: dark)">
    std::wstring alt;
    std::wstring title;
    float width = 0;         // from HTML attributes, CSS px (0 = auto)
    float height = 0;
    bool width_percent = false;
    bool height_percent = false;
    uint8_t theme_only = 0;  // 0 = always, 1 = light theme only, 2 = dark theme only
    bool flag = false;       // flag emoji: `src` is its region code in the embedded set
};

struct Block {
    BlockType type = BlockType::Paragraph;
    Align align = Align::Default;
    uint8_t level = 0;          // heading level
    uint8_t valign = 0;         // table rows/cells: 0 middle, 1 top, 2 bottom (HTML valign)
    bool anonymous = false;     // paragraph implied by inline content (no margins)
    bool open = true;           // <details open>
    bool tight = false;         // tight list
    bool ordered = false;       // ordered list
    bool header = false;        // table header row / cell
    bool from_html = false;     // created by an HTML tag
    int8_t task = -1;           // list item: -1 = not a task, 0 = [ ], 1 = [x]
    AlertKind alert = AlertKind::None;
    uint16_t html_tag = 0;      // Tag that created this block (if from_html)
    int32_t number = 0;         // list start, list item / footnote number
    int32_t colspan = 1;
    int32_t ref_count = 0;      // footnote definition: number of references
    uint32_t base_flags = 0;    // style flags applied to the whole leaf
    float indent = 0;           // extra left indent, in em
    std::wstring lang;          // code block language

    Block* parent = nullptr;
    std::vector<Block*> children;

    // ---- leaf content -----------------------------------------------------
    std::wstring text;
    std::vector<StyleRun> runs;
    std::vector<InlineObject> objects;
    uint32_t text_start = 0;    // offset in the global document text
    int32_t leaf_index = -1;    // index into Document::leaves

    // ---- layout results (document coordinates, DIPs) -----------------------
    float x = 0, y = 0, w = 0, h = 0;
    float content_w = 0;        // scrollable width for wide code blocks/tables
    float scroll_x = 0;         // horizontal scroll offset of wide blocks
    float marker_y = 0;         // list items: baseline of the marker
    bool visible = true;        // false inside collapsed <details>
    bool rtl = false;           // lists: right-to-left (markers on the right)

    bool IsLeaf() const { return IsLeafType(type); }
    uint32_t text_end() const { return text_start + static_cast<uint32_t>(text.size()); }
};

struct Anchor {
    Block* block = nullptr;
    uint32_t pos = 0;           // position inside the block's text (leaves)
};

struct Document {
    Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    Block* NewBlock(BlockType type);

    Block* root = nullptr;
    std::vector<std::unique_ptr<Block>> storage;
    std::vector<Block*> leaves;               // text-bearing blocks in document order
    std::vector<Link> links;
    std::vector<ImageRef> images;
    std::unordered_map<std::wstring, Anchor> anchors;
    std::wstring path;                        // file the document was loaded from
    std::wstring base_dir;                    // for resolving relative links/images
    uint32_t text_length = 0;                 // length of the global text

    // Global text model: all leaf texts separated by one virtual '\n'.
    std::wstring GlobalText() const;
    // Plain text of global range [a, b) for the clipboard: CRLF line breaks,
    // tabs between cells of a table row, images as their alt text.
    std::wstring PlainText(uint32_t a, uint32_t b) const;
    // Finds the leaf containing the global offset (nullptr if none).
    Block* LeafAt(uint32_t global_pos) const;
    // Resolves "#fragment" links. Accepts raw, lowercase or percent-encoded ids.
    const Anchor* FindAnchor(std::wstring_view fragment) const;
};

}  // namespace mdv

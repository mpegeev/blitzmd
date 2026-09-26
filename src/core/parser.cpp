// Markdown -> Document. md4c does the GFM parsing; this file turns its
// callbacks into the block tree and interprets the raw HTML subset GitHub
// allows (alignment, <details>, <kbd>, <sup>, <img width>, HTML tables...).
//
// Raw HTML and Markdown share a single stack of open blocks, so an HTML
// container opened in one HTML block (e.g. <div align="center">) wraps the
// Markdown blocks that follow until its closing tag, as in a browser.

#include "core/parser.h"

#include <algorithm>
#include <exception>
#include <unordered_map>
#include <unordered_set>

#include "core/emoji.h"
#include "core/flags.h"
#include "core/frontmatter.h"
#include "core/highlight.h"
#include "core/html.h"
#include "core/mermaid.h"
#include "core/util.h"
#include "md4c.h"

namespace mdv {
namespace {

constexpr wchar_t kObjectChar = 0xFFFC;
constexpr wchar_t kLineBreak = 0x2028;
constexpr size_t kMaxNesting = 128;  // block tree depth
constexpr int kMaxFootnoteNesting = 4;

std::wstring DecodeAttr(const MD_ATTRIBUTE& a) {
    std::wstring out;
    if (!a.text || a.size == 0) return out;
    for (unsigned i = 0; a.substr_offsets[i] < a.size; ++i) {
        const MD_OFFSET off = a.substr_offsets[i];
        const MD_OFFSET end = a.substr_offsets[i + 1];
        const std::string_view part(a.text + off, end - off);
        switch (a.substr_types[i]) {
        case MD_TEXT_ENTITY:
            if (!AppendEntity(out, part)) AppendUtf8(out, part);
            break;
        case MD_TEXT_NULLCHAR:
            out.push_back(0xFFFD);
            break;
        default:
            AppendUtf8(out, part);
            break;
        }
    }
    return out;
}

Align AlignFromMd(MD_ALIGN a) {
    switch (a) {
    case MD_ALIGN_LEFT: return Align::Left;
    case MD_ALIGN_CENTER: return Align::Center;
    case MD_ALIGN_RIGHT: return Align::Right;
    default: return Align::Default;
    }
}

Align AlignFromAttr(const HtmlToken& tok) {
    const std::wstring* a = tok.Attr(L"align");
    if (!a) return Align::Default;
    if (EqualsNoCase(*a, L"center") || EqualsNoCase(*a, L"middle")) return Align::Center;
    if (EqualsNoCase(*a, L"right")) return Align::Right;
    if (EqualsNoCase(*a, L"left") || EqualsNoCase(*a, L"justify")) return Align::Left;
    return Align::Default;
}

// HTML valign on table rows and cells: 0 = middle (default), 1 = top, 2 = bottom.
uint8_t VAlignFromAttr(const HtmlToken& tok) {
    const std::wstring* a = tok.Attr(L"valign");
    if (!a) return 0;
    if (EqualsNoCase(*a, L"top")) return 1;
    if (EqualsNoCase(*a, L"bottom")) return 2;
    return 0;
}

AlertKind AlertFromName(std::wstring_view s) {
    if (EqualsNoCase(s, L"note")) return AlertKind::Note;
    if (EqualsNoCase(s, L"tip")) return AlertKind::Tip;
    if (EqualsNoCase(s, L"important")) return AlertKind::Important;
    if (EqualsNoCase(s, L"warning")) return AlertKind::Warning;
    if (EqualsNoCase(s, L"caution")) return AlertKind::Caution;
    return AlertKind::Note;
}

// Tags whose whole content is dropped (GitHub strips them).
bool IsHiddenTag(Tag t) {
    switch (t) {
    case Tag::Script: case Tag::Style: case Tag::Iframe: case Tag::Noembed: case Tag::Noframes:
    case Tag::Template: case Tag::Head: case Tag::Title: case Tag::Svg: case Tag::Math:
    case Tag::Object: case Tag::Select: case Tag::Canvas: case Tag::Noscript: case Tag::Button:
    case Tag::Rp: case Tag::Map:
        return true;
    default:
        return false;
    }
}

uint32_t InlineFlagFor(Tag t) {
    switch (t) {
    case Tag::B: case Tag::Strong: return kBold;
    case Tag::I: case Tag::Em: case Tag::Cite: case Tag::Dfn: case Tag::Var: return kItalic;
    case Tag::U: case Tag::Ins: return kUnderline;
    case Tag::S: case Tag::Strike: case Tag::Del: return kStrike;
    case Tag::Code: case Tag::Tt: case Tag::Samp: return kCode;
    case Tag::Kbd: return kKbd;
    case Tag::Mark: return kMark;
    case Tag::Small: return kSmall;
    case Tag::Big: return kBig;
    case Tag::Sub: return kSub;
    case Tag::Sup: return kSup;
    default: return 0;
    }
}

bool IsContainerTag(Tag t) {
    switch (t) {
    case Tag::Div: case Tag::Center: case Tag::Section: case Tag::Article: case Tag::Aside:
    case Tag::Header: case Tag::Footer: case Tag::Nav: case Tag::Main: case Tag::Figure:
    case Tag::Hgroup: case Tag::Fieldset: case Tag::Form: case Tag::Address: case Tag::Dl:
    case Tag::Dd: case Tag::Marquee:
        return true;
    default:
        return false;
    }
}

bool IsHeadingTag(Tag t) { return t >= Tag::H1 && t <= Tag::H6; }
bool IsListTag(Tag t) { return t == Tag::Ul || t == Tag::Ol || t == Tag::Menu || t == Tag::Dir; }

// First URL of a srcset attribute ("a.png 1x, b.png 2x" -> "a.png").
std::wstring FirstSrcsetUrl(std::wstring_view s) {
    s = Trim(s);
    size_t e = 0;
    while (e < s.size() && !IsSpace(s[e]) && s[e] != L',') ++e;
    return std::wstring(s.substr(0, e));
}

// Handles GitHub's "#gh-dark-mode-only" / "#gh-light-mode-only" image hints.
void ApplyThemeFragment(ImageRef& img) {
    constexpr std::wstring_view kDark = L"#gh-dark-mode-only";
    constexpr std::wstring_view kLight = L"#gh-light-mode-only";
    auto ends_with = [](const std::wstring& s, std::wstring_view suffix) {
        return s.size() >= suffix.size() &&
               s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    if (ends_with(img.src, kDark)) {
        img.src.resize(img.src.size() - kDark.size());
        img.theme_only = 2;
    } else if (ends_with(img.src, kLight)) {
        img.src.resize(img.src.size() - kLight.size());
        img.theme_only = 1;
    }
}

bool IsShortcodeChar(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'+' ||
           c == L'-';
}

using FootnoteBodies = std::unordered_map<std::wstring, std::string>;

bool IsBlankLine(std::string_view l) {
    for (char c : l) {
        if (c != ' ' && c != '\t' && c != '\r') return false;
    }
    return true;
}

bool IsIndented4(std::string_view l) {
    int col = 0;
    for (char c : l) {
        if (c == ' ') ++col;
        else if (c == '\t') col += 4 - col % 4;
        else break;
        if (col >= 4) return true;
    }
    return false;
}

std::string_view Dedent4(std::string_view l) {
    size_t i = 0;
    int col = 0;
    while (i < l.size() && col < 4) {
        if (l[i] == ' ') ++col;
        else if (l[i] == '\t') col += 4 - col % 4;
        else break;
        ++i;
    }
    return l.substr(i);
}

// md4c limits footnote definitions to one paragraph. GitHub (cmark-gfm)
// also includes following blocks indented by 4 spaces. This pre-pass moves
// such continuation blocks out of the source into `bodies` (keyed by label);
// they are parsed separately and appended to the footnote. Returns an empty
// string if nothing had to be changed.
std::string ExtractFootnoteBodies(std::string_view src, FootnoteBodies& bodies) {
    if (src.find("[^") == std::string_view::npos) return {};
    std::vector<std::string_view> lines;
    for (size_t p = 0; p <= src.size();) {
        size_t e = src.find('\n', p);
        if (e == std::string_view::npos) e = src.size();
        lines.push_back(src.substr(p, e - p));
        p = e + 1;
    }
    std::string out;
    bool changed = false;
    char fence = 0;
    size_t fence_len = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        size_t ind = 0;
        while (ind < line.size() && ind < 4 && line[ind] == ' ') ++ind;
        const std::string_view body = line.substr(ind);
        auto emit = [&](std::string_view l) {
            out.append(l);
            out.push_back('\n');
        };
        if (ind < 4 && (body.rfind("```", 0) == 0 || body.rfind("~~~", 0) == 0)) {
            size_t n = 0;
            while (n < body.size() && body[n] == body[0]) ++n;
            if (!fence) {
                fence = body[0];
                fence_len = n;
            } else if (body[0] == fence && n >= fence_len && IsBlankLine(body.substr(n))) {
                fence = 0;
            }
            emit(line);
            continue;
        }
        if (fence || ind >= 4 || body.rfind("[^", 0) != 0) {
            emit(line);
            continue;
        }
        const size_t close = body.find("]:");
        if (close == std::string_view::npos || close <= 2 ||
            body.substr(2, close - 2).find_first_of(" \t[]") != std::string_view::npos) {
            emit(line);
            continue;
        }
        const std::wstring label = Utf8ToWide(body.substr(2, close - 2));
        emit(line);
        // The first paragraph (with lazy continuation lines) stays in place;
        // it ends at a blank line or at the next definition.
        auto starts_def = [](std::string_view l) {
            size_t n = 0;
            while (n < l.size() && n < 4 && l[n] == ' ') ++n;
            return n < 4 && l.substr(n).rfind("[^", 0) == 0 && l.find("]:", n) != std::string_view::npos;
        };
        size_t j = i + 1;
        while (j < lines.size() && !IsBlankLine(lines[j]) && !starts_def(lines[j])) emit(lines[j++]);
        if (j < lines.size() && starts_def(lines[j])) {
            i = j - 1;
            continue;
        }
        // Then: blank lines followed by 4-space-indented blocks.
        std::string extra;
        size_t k = j;
        for (;;) {
            size_t m = k;
            while (m < lines.size() && IsBlankLine(lines[m])) ++m;
            if (m == k || m >= lines.size() || !IsIndented4(lines[m])) break;
            extra.push_back('\n');
            while (m < lines.size() && (IsBlankLine(lines[m]) || IsIndented4(lines[m]))) {
                extra.append(Dedent4(lines[m]));
                extra.push_back('\n');
                ++m;
            }
            k = m;
        }
        if (!extra.empty()) {
            bodies[label] = std::move(extra);
            changed = true;
            out.push_back('\n');
            i = k - 1;
        } else {
            i = j - 1;
        }
    }
    if (!changed) return {};
    return out;
}

class Builder {
public:
    Builder(Document& doc, const ParseOptions& opt, const FootnoteBodies* bodies)
        : doc_(doc), opt_(opt), footnote_bodies_(bodies) {
        stack_.push_back(doc.root);
    }

    // md4c is C: nothing may unwind through it (undefined with /EHsc, and
    // its buffers would leak). A callback that throws (out of memory) stops
    // the parse instead; ParseMarkdown rethrows once md_parse has returned.
    template <class F>
    static int Guarded(void* u, F&& f) {
        Builder* b = static_cast<Builder*>(u);
        try {
            return f(*b);
        } catch (...) {
            b->error_ = std::current_exception();
            return -1;
        }
    }
    static int OnEnterBlock(MD_BLOCKTYPE t, void* d, void* u) {
        return Guarded(u, [&](Builder& b) { return b.EnterBlock(t, d); });
    }
    static int OnLeaveBlock(MD_BLOCKTYPE t, void* d, void* u) {
        return Guarded(u, [&](Builder& b) { return b.LeaveBlock(t, d); });
    }
    static int OnEnterSpan(MD_SPANTYPE t, void* d, void* u) {
        return Guarded(u, [&](Builder& b) { return b.EnterSpan(t, d); });
    }
    static int OnLeaveSpan(MD_SPANTYPE t, void* d, void* u) {
        return Guarded(u, [&](Builder& b) { return b.LeaveSpan(t, d); });
    }
    static int OnText(MD_TEXTTYPE t, const MD_CHAR* s, MD_SIZE n, void* u) {
        return Guarded(u, [&](Builder& b) { return b.Text(t, std::string_view(s, n)); });
    }
    const std::exception_ptr& error() const { return error_; }

    void Finish();

private:
    // ---- block stack ------------------------------------------------------
    Block* Top() const { return stack_.back(); }
    Block* Push(BlockType type);
    void Pop();
    void PopTo(Block* b);
    void PopAbove(Block* b);
    void CloseLeaf();
    Block* EnsureLeaf();
    void FinalizeLeaf(Block* b);
    void FinalizeCode(Block* b);
    void AddFootnoteBackrefs(Block* def);

    // ---- inline content -----------------------------------------------------
    void PushStyle(uint32_t flags);
    void PopStyle(uint32_t flags);
    void ResetInline();
    void AppendToLeaf(Block* leaf, std::wstring_view s, uint32_t flags, int32_t link);
    void Append(std::wstring_view s, uint32_t extra_flags = 0);
    void AppendNormal(std::wstring_view s);
    void FlushText();  // both of these:
    void FlushInlineHtml();
    void FlushPendingText();
    void AppendMath(const std::wstring& tex, bool display);
    void AddMath(Block* leaf, const std::wstring& tex, std::wstring source, bool display);
    void AppendImage(ImageRef img);
    void RegisterAnchor(const std::wstring& id);
    void BindPendingAnchors(Block* b);

    // ---- markdown callbacks -------------------------------------------------
    int EnterBlock(MD_BLOCKTYPE type, void* detail);
    int LeaveBlock(MD_BLOCKTYPE type, void* detail);
    int EnterSpan(MD_SPANTYPE type, void* detail);
    int LeaveSpan(MD_SPANTYPE type, void* detail);
    int Text(MD_TEXTTYPE type, std::string_view s);

    // ---- raw HTML -------------------------------------------------------------
    void ProcessHtml(std::string_view utf8, bool block);
    void HtmlText(const std::wstring& text);
    void HtmlStartTag(const HtmlToken& tok);
    void HtmlEndTag(const HtmlToken& tok);
    void HtmlImage(const HtmlToken& tok);
    Block* PushHtml(BlockType type, const HtmlToken& tok);
    Block* FindOpenHtml(Tag tag, Tag stop = Tag::Unknown) const;
    bool PopHtml(Tag tag);

    void AppendFootnoteBody(Block* def);

    Document& doc_;
    const ParseOptions& opt_;
    const FootnoteBodies* footnote_bodies_ = nullptr;
    std::exception_ptr error_;  // thrown by a callback (see Guarded)
    std::vector<Block*> stack_;
    std::vector<Block*> md_open_;

    uint32_t mask_ = 0;
    uint16_t counts_[kStyleBitCount] = {};
    std::vector<int32_t> links_;
    std::vector<bool> a_has_href_;
    std::wstring pending_text_;  // buffered MD_TEXT_NORMAL (for emoji shortcodes)
    // Buffered inline MD_TEXT_HTML: md4c hands a tag spanning lines over in
    // pieces ("<a", "\n", "href=...>"), parsed together once complete.
    std::string inline_html_;

    int image_depth_ = 0;
    ImageRef pending_image_;
    std::wstring image_alt_;

    int math_depth_ = 0;
    std::wstring math_buf_;       // TeX source of the current $math$ span

    bool in_html_block_ = false;
    bool html_block_mode_ = false;  // processing an HTML block (vs inline HTML)
    std::string html_buf_;
    int hidden_depth_ = 0;
    bool html_thead_ = false;
    bool in_thead_ = false;
    bool in_picture_ = false;
    std::wstring picture_dark_, picture_light_;

    std::vector<std::wstring> pending_anchors_;
    std::vector<std::wstring> leaf_anchors_;  // anchors bound to the open leaf
    std::unordered_map<std::wstring, int> slug_counts_;
};

// ============================================================================
// Block stack
// ============================================================================

Block* Builder::Push(BlockType type) {
    CloseLeaf();
    Block* parent = Top();
    // Layout and rendering recurse over the tree: beyond a sane depth, blocks
    // still open and close in order but are attached at the deepest level.
    if (stack_.size() > kMaxNesting) parent = stack_[kMaxNesting - 1];
    Block* b = doc_.NewBlock(type);
    b->parent = parent;
    parent->children.push_back(b);
    stack_.push_back(b);
    BindPendingAnchors(b);
    return b;
}

void Builder::Pop() {
    if (stack_.size() <= 1) return;
    Block* b = stack_.back();
    if (b->IsLeaf()) FlushText();
    stack_.pop_back();
    switch (b->type) {
    case BlockType::Paragraph:
    case BlockType::Heading:
    case BlockType::Summary:
        FinalizeLeaf(b);
        break;
    case BlockType::CodeBlock:
        FinalizeCode(b);
        break;
    case BlockType::FootnoteDef:
        AddFootnoteBackrefs(b);
        break;
    case BlockType::Details:
        if (b->children.empty() || b->children.front()->type != BlockType::Summary) {
            Block* s = doc_.NewBlock(BlockType::Summary);
            s->parent = b;
            s->from_html = true;
            AppendToLeaf(s, L"Details", 0, -1);
            b->children.insert(b->children.begin(), s);
        }
        break;
    default:
        break;
    }
}

void Builder::PopTo(Block* b) {
    if (std::find(stack_.begin(), stack_.end(), b) == stack_.end()) return;
    while (stack_.size() > 1) {
        Block* top = stack_.back();
        Pop();
        if (top == b) break;
    }
}

void Builder::PopAbove(Block* b) {
    if (std::find(stack_.begin(), stack_.end(), b) == stack_.end()) return;
    while (stack_.size() > 1 && Top() != b) Pop();
}

void Builder::CloseLeaf() {
    if (stack_.size() > 1 && Top()->IsLeaf()) Pop();
}

Block* Builder::EnsureLeaf() {
    Block* top = Top();
    if (top->IsLeaf()) return top;
    if (top->type == BlockType::Table || top->type == BlockType::TableRow) return nullptr;
    Block* p = Push(BlockType::Paragraph);
    p->anonymous = true;
    return p;
}

void Builder::FinalizeLeaf(Block* b) {
    // Drop trailing whitespace, and one trailing line break (a trailing <br>
    // does not create an empty line in HTML).
    auto trim_end = [b](wchar_t c) {
        if (b->text.empty() || b->text.back() != c) return false;
        if (b->runs.back().flags & kCode) return false;  // spaces in `code` are significant
        b->text.pop_back();
        StyleRun& r = b->runs.back();
        if (--r.length == 0) b->runs.pop_back();
        return true;
    };
    while (trim_end(L' ')) {}
    if (b->text.size() > 1) {
        if (trim_end(kLineBreak)) {
            while (trim_end(L' ')) {}
        }
    }
    if (b->text.size() == 1 && b->text[0] == kLineBreak) b->text[0] = 0x200B;  // lone <br>

    const bool empty = b->text.empty();
    if (empty && b->type == BlockType::Paragraph) {
        // Remove empty paragraphs (e.g. whitespace between HTML tags); any
        // anchors bound to them move to the next block.
        Block* parent = b->parent;
        if (parent && !parent->children.empty() && parent->children.back() == b) {
            parent->children.pop_back();
        }
        for (auto& id : leaf_anchors_) {
            doc_.anchors.erase(id);
            pending_anchors_.push_back(std::move(id));
        }
        leaf_anchors_.clear();
        return;
    }
    leaf_anchors_.clear();

    // A paragraph holding only $$display math$$ is centered, like on GitHub.
    if (b->type == BlockType::Paragraph && b->align == Align::Default) {
        bool any_display = false, only_display = true;
        for (const StyleRun& r : b->runs) {
            bool blank = true;
            for (uint32_t k = r.start; k < r.start + r.length; ++k) {
                if (!IsSpace(b->text[k]) && b->text[k] != kLineBreak) blank = false;
            }
            if (blank) continue;
            if (r.flags & kMathDisplay) any_display = true;
            else only_display = false;
        }
        if (any_display && only_display) b->align = Align::Center;
    }

    if (b->type == BlockType::Heading) {
        std::wstring plain;
        plain.reserve(b->text.size());
        for (wchar_t c : b->text) {
            if (c != kObjectChar) plain.push_back(c == kLineBreak ? L' ' : c);
        }
        std::wstring slug = MakeSlug(plain);
        int& n = slug_counts_[slug];
        if (n > 0) slug += L"-" + std::to_wstring(n);
        ++n;
        doc_.anchors.try_emplace(slug, Anchor{b, 0});
    }
}

void Builder::FinalizeCode(Block* b) {
    if (!b->text.empty() && b->text.back() == L'\n') b->text.pop_back();
    if (!b->from_html && opt_.math && EqualsNoCase(b->lang, L"math")) {
        // ```math blocks are display equations on GitHub.
        const std::wstring tex = std::move(b->text);
        b->text.clear();
        b->runs.clear();
        b->type = BlockType::Paragraph;
        b->align = Align::Center;
        if (Trim(tex).empty()) {
            AppendToLeaf(b, L"​", 0, -1);
            return;
        }
        AddMath(b, tex, tex, true);
        return;
    }
    if (!b->from_html && opt_.mermaid && EqualsNoCase(b->lang, L"mermaid")) {
        // A diagram drawn over its source (copy and search see the source);
        // one mdview can't draw stays code with a note saying why.
        std::wstring error;
        if (CheckMermaid(b->text, &error)) {
            std::wstring source = std::move(b->text);
            b->text.clear();
            b->runs.clear();
            b->type = BlockType::Paragraph;
            b->align = Align::Center;
            InlineObject obj;
            obj.length = static_cast<uint32_t>(source.size());
            obj.diagram = static_cast<int32_t>(doc_.diagrams.size());
            doc_.diagrams.push_back(DiagramRef{source});
            b->objects.push_back(obj);
            for (wchar_t& c : source) {
                if (c == L'\n') c = kLineBreak;
                else if (c == L'\r') c = L' ';
            }
            AppendToLeaf(b, source, 0, -1);
            return;
        }
        b->note = std::wstring(Tr(L"Mermaid diagram not shown: ", L"Диаграмма Mermaid не показана: ")) + error;
    }
    if (b->from_html) {
        // <pre>: keep inline styles, just fix the run coverage after trimming.
        while (!b->runs.empty() && b->runs.back().start >= b->text.size()) b->runs.pop_back();
        if (!b->runs.empty()) {
            StyleRun& r = b->runs.back();
            r.length = static_cast<uint32_t>(b->text.size()) - r.start;
        }
        if (b->lang.empty()) return;
    }
    b->runs.clear();
    const uint32_t n = static_cast<uint32_t>(b->text.size());
    std::vector<HighlightSpan> spans;
    if (opt_.highlight_code && n > 0 && n < (8u << 20)) HighlightCode(b->text, b->lang, spans);
    uint32_t pos = 0;
    for (const HighlightSpan& s : spans) {
        if (s.start < pos || s.start + s.length > n || s.length == 0) continue;
        if (s.start > pos) b->runs.push_back({pos, s.start - pos, 0, -1, 0});
        b->runs.push_back({s.start, s.length, 0, -1, static_cast<uint8_t>(s.kind)});
        pos = s.start + s.length;
    }
    if (pos < n) b->runs.push_back({pos, n - pos, 0, -1, 0});
}

void Builder::AddFootnoteBackrefs(Block* def) {
    Block* target = nullptr;
    if (!def->children.empty() && def->children.back()->type == BlockType::Paragraph) {
        target = def->children.back();
    } else {
        target = doc_.NewBlock(BlockType::Paragraph);
        target->anonymous = true;
        target->parent = def;
        def->children.push_back(target);
    }
    const int refs = std::max(1, def->ref_count);
    for (int r = 1; r <= refs; ++r) {
        Link link;
        link.url = L"#fnref-" + std::to_wstring(def->number) + L"-" + std::to_wstring(r);
        link.title = L"Back to reference " + std::to_wstring(r);
        doc_.links.push_back(std::move(link));
        const int32_t li = static_cast<int32_t>(doc_.links.size() - 1);
        AppendToLeaf(target, L" ", 0, -1);
        AppendToLeaf(target, L"↩︎", 0, li);
        if (r > 1) AppendToLeaf(target, std::to_wstring(r), kSup, li);
    }
}

// ============================================================================
// Inline content
// ============================================================================

void Builder::PushStyle(uint32_t flags) {
    for (uint32_t bit = 0; bit < kStyleBitCount; ++bit) {
        if (flags & (1u << bit)) {
            ++counts_[bit];
            mask_ |= 1u << bit;
        }
    }
}

void Builder::PopStyle(uint32_t flags) {
    for (uint32_t bit = 0; bit < kStyleBitCount; ++bit) {
        if ((flags & (1u << bit)) && counts_[bit] > 0 && --counts_[bit] == 0) {
            mask_ &= ~(1u << bit);
        }
    }
}

void Builder::ResetInline() {
    mask_ = 0;
    std::fill(std::begin(counts_), std::end(counts_), uint16_t{0});
    links_.clear();
    a_has_href_.clear();
    hidden_depth_ = 0;
    math_depth_ = 0;
    math_buf_.clear();
}

void Builder::AppendToLeaf(Block* leaf, std::wstring_view s, uint32_t flags, int32_t link) {
    if (s.empty()) return;
    const uint32_t start = static_cast<uint32_t>(leaf->text.size());
    leaf->text.append(s);
    const uint32_t len = static_cast<uint32_t>(s.size());
    if (!leaf->runs.empty()) {
        StyleRun& r = leaf->runs.back();
        if (r.flags == flags && r.link == link && r.token == 0 && r.start + r.length == start) {
            r.length += len;
            return;
        }
    }
    leaf->runs.push_back({start, len, flags, link, 0});
}

void Builder::Append(std::wstring_view s, uint32_t extra_flags) {
    if (s.empty() || hidden_depth_ > 0) return;
    if (image_depth_ > 0) {
        image_alt_.append(s);
        return;
    }
    Block* leaf = EnsureLeaf();
    if (!leaf) return;
    AppendToLeaf(leaf, s, mask_ | extra_flags, links_.empty() ? -1 : links_.back());
}

void Builder::AppendNormal(std::wstring_view text) {
    // Collapse whitespace like a browser (white-space: normal): runs of
    // spaces/tabs/newlines become one space, also across text chunks.
    std::wstring collapsed;
    collapsed.reserve(text.size());
    bool space = false;
    for (wchar_t c : text) {
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\f') {
            space = true;
            continue;
        }
        if (space) collapsed.push_back(L' ');
        space = false;
        collapsed.push_back(c);
    }
    if (space) collapsed.push_back(L' ');
    if (!collapsed.empty() && collapsed.front() == L' ' && image_depth_ == 0) {
        const Block* top = Top();
        if (top->IsLeaf()) {
            const bool prev_space =
                top->text.empty() || top->text.back() == kLineBreak ||
                (top->text.back() == L' ' && !top->runs.empty() && !(top->runs.back().flags & kCode));
            if (prev_space) collapsed.erase(0, 1);
        }
    }
    std::wstring_view w = collapsed;
    if (!opt_.emoji_shortcodes || (mask_ & (kCode | kMath))) {
        Append(w);
        return;
    }
    size_t last = 0, i = 0;
    while ((i = w.find(L':', i)) != std::wstring_view::npos) {
        size_t j = i + 1;
        while (j < w.size() && j - i <= 41 && IsShortcodeChar(w[j])) ++j;
        if (j < w.size() && w[j] == L':' && j > i + 1) {
            char name[48];
            const size_t len = j - i - 1;
            for (size_t k = 0; k < len; ++k) name[k] = static_cast<char>(w[i + 1 + k]);
            const std::wstring_view emoji = LookupEmojiShortcode(std::string_view(name, len));
            if (!emoji.empty()) {
                Append(w.substr(last, i - last));
                Append(emoji);
                last = i = j + 1;
                continue;
            }
        }
        ++i;
    }
    Append(w.substr(last));
}

// A formula becomes an inline object over its source, so copying and
// searching see the TeX.
void Builder::AppendMath(const std::wstring& source_tex, bool display) {
    if (hidden_depth_ > 0) return;
    std::wstring delimiter = display ? L"$$" : L"$";
    std::wstring tex = source_tex;
    if (tex.size() >= 2 && tex.front() == L'`' && tex.back() == L'`') {
        // GitHub's $`...`$ form: the backticks are delimiters.
        tex = tex.substr(1, tex.size() - 2);
        delimiter += L'`';
    }
    const std::wstring closing(delimiter.rbegin(), delimiter.rend());
    Block* leaf = EnsureLeaf();
    if (!leaf) return;
    AddMath(leaf, tex, delimiter + tex + closing, display);
}

void Builder::AddMath(Block* leaf, const std::wstring& tex, std::wstring source, bool display) {
    // Lines of a ```math block stay lines when copied; they don't break the
    // formula's line (it is one inline object).
    for (wchar_t& c : source) {
        if (c == L'\n') c = kLineBreak;
        else if (c == L'\r' || c == L'\t') c = L' ';
    }
    InlineObject obj;
    obj.pos = static_cast<uint32_t>(leaf->text.size());
    obj.length = static_cast<uint32_t>(source.size());
    obj.math = static_cast<int32_t>(doc_.maths.size());
    doc_.maths.push_back(MathRef{tex, display});
    leaf->objects.push_back(obj);
    AppendToLeaf(leaf, source, mask_ | kMath | (display ? kMathDisplay : 0), links_.empty() ? -1 : links_.back());
}

void Builder::FlushText() {
    FlushInlineHtml();
    FlushPendingText();
}

void Builder::FlushPendingText() {
    if (pending_text_.empty()) return;
    std::wstring t;
    t.swap(pending_text_);
    AppendNormal(t);
}

void Builder::FlushInlineHtml() {
    if (inline_html_.empty()) return;
    std::string html;
    html.swap(inline_html_);  // (ProcessHtml may flush text again)
    ProcessHtml(html, false);
}

void Builder::AppendImage(ImageRef img) {
    if (hidden_depth_ > 0) return;
    if (image_depth_ > 0) {  // image inside image alt text: only its alt counts
        image_alt_.append(img.alt);
        return;
    }
    Block* leaf = EnsureLeaf();
    if (!leaf) return;
    doc_.images.push_back(std::move(img));
    InlineObject obj;
    obj.pos = static_cast<uint32_t>(leaf->text.size());
    obj.image = static_cast<int32_t>(doc_.images.size() - 1);
    leaf->objects.push_back(obj);
    const wchar_t ch = kObjectChar;
    AppendToLeaf(leaf, std::wstring_view(&ch, 1), mask_, links_.empty() ? -1 : links_.back());
}

void Builder::RegisterAnchor(const std::wstring& id) {
    if (id.empty()) return;
    Block* top = Top();
    if (top->IsLeaf()) {
        if (doc_.anchors.try_emplace(id, Anchor{top, static_cast<uint32_t>(top->text.size())}).second) {
            leaf_anchors_.push_back(id);
        }
    } else {
        pending_anchors_.push_back(id);
    }
}

void Builder::BindPendingAnchors(Block* b) {
    for (auto& id : pending_anchors_) {
        if (doc_.anchors.try_emplace(id, Anchor{b, 0}).second && b->IsLeaf()) {
            leaf_anchors_.push_back(id);
        }
    }
    pending_anchors_.clear();
}

// ============================================================================
// Markdown callbacks
// ============================================================================

int Builder::EnterBlock(MD_BLOCKTYPE type, void* detail) {
    FlushText();
    if (type == MD_BLOCK_DOC) {
        md_open_.push_back(nullptr);
        return 0;
    }
    CloseLeaf();
    ResetInline();
    Block* b = nullptr;
    switch (type) {
    case MD_BLOCK_QUOTE:
        b = Push(BlockType::Quote);
        break;
    case MD_BLOCK_UL: {
        const auto* d = static_cast<const MD_BLOCK_UL_DETAIL*>(detail);
        b = Push(BlockType::List);
        b->tight = d->is_tight != 0;
        break;
    }
    case MD_BLOCK_OL: {
        const auto* d = static_cast<const MD_BLOCK_OL_DETAIL*>(detail);
        b = Push(BlockType::List);
        b->ordered = true;
        b->number = static_cast<int32_t>(std::min<unsigned>(d->start, 999999999u));
        b->tight = d->is_tight != 0;
        break;
    }
    case MD_BLOCK_LI: {
        const auto* d = static_cast<const MD_BLOCK_LI_DETAIL*>(detail);
        Block* list = Top();
        const int index = static_cast<int>(list->children.size());
        b = Push(BlockType::ListItem);
        if (d->is_task) b->task = (d->task_mark == 'x' || d->task_mark == 'X') ? 1 : 0;
        b->number = list->number + index;
        b->tight = list->tight;
        break;
    }
    case MD_BLOCK_HR:
        b = Push(BlockType::Rule);
        break;
    case MD_BLOCK_H: {
        const auto* d = static_cast<const MD_BLOCK_H_DETAIL*>(detail);
        b = Push(BlockType::Heading);
        b->level = static_cast<uint8_t>(std::clamp<unsigned>(d->level, 1, 6));
        break;
    }
    case MD_BLOCK_CODE: {
        const auto* d = static_cast<const MD_BLOCK_CODE_DETAIL*>(detail);
        b = Push(BlockType::CodeBlock);
        b->lang = DecodeAttr(d->lang);
        break;
    }
    case MD_BLOCK_HTML:
        in_html_block_ = true;
        html_buf_.clear();
        break;
    case MD_BLOCK_P:
        b = Push(BlockType::Paragraph);
        break;
    case MD_BLOCK_TABLE:
        b = Push(BlockType::Table);
        break;
    case MD_BLOCK_THEAD:
        in_thead_ = true;
        break;
    case MD_BLOCK_TBODY:
        in_thead_ = false;
        break;
    case MD_BLOCK_TR:
        b = Push(BlockType::TableRow);
        b->header = in_thead_;
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        const auto* d = static_cast<const MD_BLOCK_TD_DETAIL*>(detail);
        b = Push(BlockType::TableCell);
        b->header = type == MD_BLOCK_TH;
        b->align = AlignFromMd(d->align);
        break;
    }
    case MD_BLOCK_FOOTNOTE_DEF_SECTION:
        b = Push(BlockType::FootnoteSection);
        break;
    case MD_BLOCK_FOOTNOTE_DEF: {
        const auto* d = static_cast<const MD_BLOCK_FOOTNOTE_DEF_DETAIL*>(detail);
        b = Push(BlockType::FootnoteDef);
        b->number = static_cast<int32_t>(d->id);
        b->ref_count = static_cast<int32_t>(d->ref_count);
        b->lang = DecodeAttr(d->label);  // label, to find a multi-block body
        doc_.anchors.try_emplace(L"fn-" + std::to_wstring(d->id), Anchor{b, 0});
        break;
    }
    case MD_BLOCK_ADMONITION: {
        const auto* d = static_cast<const MD_BLOCK_ADMONITION_DETAIL*>(detail);
        b = Push(BlockType::Alert);
        b->alert = AlertFromName(DecodeAttr(d->type));
        break;
    }
    default:
        break;
    }
    md_open_.push_back(b);
    return 0;
}

int Builder::LeaveBlock(MD_BLOCKTYPE type, void* /*detail*/) {
    FlushText();
    if (md_open_.empty()) return 0;
    Block* b = md_open_.back();
    md_open_.pop_back();
    if (type == MD_BLOCK_HTML) {
        in_html_block_ = false;
        ProcessHtml(html_buf_, true);
        html_buf_.clear();
        CloseLeaf();
        ResetInline();
        in_picture_ = false;
        return 0;
    }
    if (b && type == MD_BLOCK_FOOTNOTE_DEF) AppendFootnoteBody(b);
    if (b) PopTo(b);
    return 0;
}

void Builder::AppendFootnoteBody(Block* def) {
    if (!footnote_bodies_) return;
    const auto it = footnote_bodies_->find(def->lang);
    if (it == footnote_bodies_->end()) return;
    FlushText();
    PopAbove(def);
    // The first paragraph becomes a real <p> (spacing) now that more follows.
    if (!def->children.empty() && def->children.front()->type == BlockType::Paragraph) {
        def->children.front()->anonymous = false;
    }
    ParseOptions sub_opt = opt_;
    ++sub_opt.footnote_nesting;
    std::unique_ptr<Document> sub = ParseMarkdown(it->second, doc_.path, sub_opt);
    const int32_t link_off = static_cast<int32_t>(doc_.links.size());
    const int32_t image_off = static_cast<int32_t>(doc_.images.size());
    const int32_t math_off = static_cast<int32_t>(doc_.maths.size());
    const int32_t diagram_off = static_cast<int32_t>(doc_.diagrams.size());
    for (Link& l : sub->links) doc_.links.push_back(std::move(l));
    for (ImageRef& im : sub->images) doc_.images.push_back(std::move(im));
    for (MathRef& m : sub->maths) doc_.maths.push_back(std::move(m));
    for (DiagramRef& d : sub->diagrams) doc_.diagrams.push_back(std::move(d));
    for (auto& up : sub->storage) {
        Block* b = up.get();
        for (StyleRun& r : b->runs) {
            if (r.link >= 0) r.link += link_off;
        }
        for (InlineObject& o : b->objects) {
            if (o.image >= 0) o.image += image_off;
            if (o.math >= 0) o.math += math_off;
            if (o.diagram >= 0) o.diagram += diagram_off;
        }
        b->leaf_index = -1;
    }
    // Only the adopted subtrees survive; the rest (root, nested footnote
    // section) is freed with `sub`, so nothing may keep pointing into it.
    std::unordered_set<const Block*> kept;
    std::vector<Block*> walk;
    for (Block* c : sub->root->children) {
        if (c->type == BlockType::FootnoteSection) continue;  // nested refs don't resolve
        c->parent = def;
        def->children.push_back(c);
        walk.push_back(c);
    }
    while (!walk.empty()) {
        Block* b = walk.back();
        walk.pop_back();
        kept.insert(b);
        walk.insert(walk.end(), b->children.begin(), b->children.end());
    }
    for (const auto& [id, anchor] : sub->anchors) {
        // Footnote anchors of the body are numbered on their own: never import.
        if (id.rfind(L"fn-", 0) == 0 || id.rfind(L"fnref-", 0) == 0) continue;
        if (kept.count(anchor.block)) doc_.anchors.try_emplace(id, anchor);
    }
    for (auto& up : sub->storage) {
        if (kept.count(up.get())) doc_.storage.push_back(std::move(up));
    }
}

int Builder::EnterSpan(MD_SPANTYPE type, void* detail) {
    FlushText();
    if (image_depth_ > 0) {
        if (type == MD_SPAN_IMG) ++image_depth_;
        return 0;
    }
    switch (type) {
    case MD_SPAN_EM: PushStyle(kItalic); break;
    case MD_SPAN_STRONG: PushStyle(kBold); break;
    case MD_SPAN_DEL: PushStyle(kStrike); break;
    case MD_SPAN_U: PushStyle(kUnderline); break;
    case MD_SPAN_INS: PushStyle(kUnderline); break;
    case MD_SPAN_CODE: PushStyle(kCode); break;
    case MD_SPAN_MARK: PushStyle(kMark); break;
    case MD_SPAN_SUPERSCRIPT: PushStyle(kSup); break;
    case MD_SPAN_SUBSCRIPT: PushStyle(kSub); break;
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        math_depth_++;
        math_buf_.clear();
        break;
    case MD_SPAN_A: {
        const auto* d = static_cast<const MD_SPAN_A_DETAIL*>(detail);
        Link link;
        link.url = DecodeAttr(d->href);
        link.title = DecodeAttr(d->title);
        doc_.links.push_back(std::move(link));
        links_.push_back(static_cast<int32_t>(doc_.links.size() - 1));
        break;
    }
    case MD_SPAN_IMG: {
        const auto* d = static_cast<const MD_SPAN_IMG_DETAIL*>(detail);
        image_depth_ = 1;
        pending_image_ = ImageRef{};
        pending_image_.src = DecodeAttr(d->src);
        pending_image_.title = DecodeAttr(d->title);
        image_alt_.clear();
        break;
    }
    case MD_SPAN_FOOTNOTE_REF: {
        const auto* d = static_cast<const MD_SPAN_FOOTNOTE_REF_DETAIL*>(detail);
        Link link;
        link.url = L"#fn-" + std::to_wstring(d->id);
        link.title = L"Footnote " + std::to_wstring(d->id);
        doc_.links.push_back(std::move(link));
        links_.push_back(static_cast<int32_t>(doc_.links.size() - 1));
        EnsureLeaf();
        RegisterAnchor(L"fnref-" + std::to_wstring(d->id) + L"-" + std::to_wstring(d->ref_id));
        Append(std::to_wstring(d->id), kSup);
        links_.pop_back();
        break;
    }
    default:
        break;
    }
    return 0;
}

int Builder::LeaveSpan(MD_SPANTYPE type, void* /*detail*/) {
    FlushText();
    if (image_depth_ > 0) {
        if (type == MD_SPAN_IMG && --image_depth_ == 0) {
            pending_image_.alt = image_alt_;
            ApplyThemeFragment(pending_image_);
            AppendImage(std::move(pending_image_));
            pending_image_ = ImageRef{};
        }
        return 0;
    }
    switch (type) {
    case MD_SPAN_EM: PopStyle(kItalic); break;
    case MD_SPAN_STRONG: PopStyle(kBold); break;
    case MD_SPAN_DEL: PopStyle(kStrike); break;
    case MD_SPAN_U: PopStyle(kUnderline); break;
    case MD_SPAN_INS: PopStyle(kUnderline); break;
    case MD_SPAN_CODE: PopStyle(kCode); break;
    case MD_SPAN_MARK: PopStyle(kMark); break;
    case MD_SPAN_SUPERSCRIPT: PopStyle(kSup); break;
    case MD_SPAN_SUBSCRIPT: PopStyle(kSub); break;
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        if (math_depth_ > 0 && --math_depth_ == 0) {
            const bool display = type == MD_SPAN_LATEXMATH_DISPLAY;
            AppendMath(math_buf_, display);
            math_buf_.clear();
        }
        break;
    case MD_SPAN_A:
        if (!links_.empty()) links_.pop_back();
        break;
    default:
        break;
    }
    return 0;
}

int Builder::Text(MD_TEXTTYPE type, std::string_view s) {
    if (in_html_block_) {
        html_buf_.append(s);
        return 0;
    }
    if (type != MD_TEXT_HTML) FlushInlineHtml();  // what came before goes first
    if (math_depth_ > 0 && image_depth_ == 0) {
        if (type == MD_TEXT_SOFTBR || type == MD_TEXT_BR) math_buf_.push_back(L' ');
        else if (type == MD_TEXT_ENTITY) {
            if (!AppendEntity(math_buf_, s)) AppendUtf8(math_buf_, s);
        } else AppendUtf8(math_buf_, s);
        return 0;
    }
    if ((type == MD_TEXT_NORMAL || type == MD_TEXT_SOFTBR) && image_depth_ == 0) {
        Block* top = Top();
        if (!(top->type == BlockType::CodeBlock && !top->from_html)) {
            if (type == MD_TEXT_SOFTBR) pending_text_.push_back(L' ');
            else AppendUtf8(pending_text_, s);
            return 0;
        }
    }
    FlushPendingText();  // (inline HTML: flushed above, or growing)

    if (image_depth_ > 0) {
        switch (type) {
        case MD_TEXT_ENTITY:
            if (!AppendEntity(image_alt_, s)) AppendUtf8(image_alt_, s);
            break;
        case MD_TEXT_NULLCHAR: image_alt_.push_back(0xFFFD); break;
        case MD_TEXT_BR:
        case MD_TEXT_SOFTBR: image_alt_.push_back(L' '); break;
        case MD_TEXT_HTML: break;
        default: AppendUtf8(image_alt_, s); break;
        }
        return 0;
    }

    Block* top = Top();
    if (top->type == BlockType::CodeBlock && !top->from_html) {
        if (type == MD_TEXT_NULLCHAR) top->text.push_back(0xFFFD);
        else AppendUtf8(top->text, s);
        return 0;
    }

    switch (type) {
    case MD_TEXT_NULLCHAR:
        Append(L"�");
        break;
    case MD_TEXT_BR:
        Append(std::wstring_view(&kLineBreak, 1));
        break;
    case MD_TEXT_SOFTBR:
        Append(L" ");
        break;
    case MD_TEXT_ENTITY: {
        std::wstring w;
        if (!AppendEntity(w, s)) AppendUtf8(w, s);
        Append(w);
        break;
    }
    case MD_TEXT_HTML:
        inline_html_.append(s);  // parsed by the next other event (FlushInlineHtml)
        break;
    case MD_TEXT_LATEXMATH:
        AppendUtf8(math_buf_, s);
        break;
    default: {  // MD_TEXT_CODE
        std::wstring w;
        AppendUtf8(w, s);
        // GFM splits table cells before inline parsing: "\|" inside a code
        // span in a table cell is an escaped cell delimiter, i.e. "|".
        const Block* cell = top->IsLeaf() ? top->parent : top;
        if (type == MD_TEXT_CODE && cell && cell->type == BlockType::TableCell && !cell->from_html) {
            for (size_t p = 0; (p = w.find(L"\\|", p)) != std::wstring::npos;) w.erase(p, 1);
        }
        Append(w);
        break;
    }
    }
    return 0;
}

void Builder::Finish() {
    FlushText();
    while (stack_.size() > 1) Pop();
    // Assign global text offsets.
    uint32_t cursor = 0;
    std::vector<Block*> todo{doc_.root};
    while (!todo.empty()) {
        Block* b = todo.back();
        todo.pop_back();
        if (b->IsLeaf()) {
            b->leaf_index = static_cast<int32_t>(doc_.leaves.size());
            b->text_start = cursor;
            cursor += static_cast<uint32_t>(b->text.size()) + 1;
            doc_.leaves.push_back(b);
        }
        for (auto it = b->children.rbegin(); it != b->children.rend(); ++it) todo.push_back(*it);
    }
    doc_.text_length = cursor;
    for (auto& id : pending_anchors_) doc_.anchors.try_emplace(id, Anchor{doc_.root, 0});
    pending_anchors_.clear();
}

// ============================================================================
// Raw HTML
// ============================================================================

void Builder::ProcessHtml(std::string_view utf8, bool block) {
    const std::wstring w = Utf8ToWide(utf8);
    const bool saved = html_block_mode_;
    html_block_mode_ = block;
    HtmlTokenizer tz(w);
    HtmlToken tok;
    while (tz.Next(tok)) {
        switch (tok.kind) {
        case HtmlToken::Text:
            if (hidden_depth_ == 0 && !IsHiddenTag(tok.tag)) HtmlText(tok.text);
            break;
        case HtmlToken::StartTag:
            HtmlStartTag(tok);
            break;
        case HtmlToken::EndTag:
            HtmlEndTag(tok);
            break;
        default:
            break;
        }
    }
    html_block_mode_ = saved;
}

void Builder::HtmlText(const std::wstring& text) {
    if (text.empty()) return;
    Block* top = Top();
    if (top->type == BlockType::CodeBlock && top->from_html) {
        // <pre>: whitespace is significant; a newline right after <pre> is dropped.
        std::wstring_view t = text;
        if (top->text.empty() && !t.empty() && t.front() == L'\n') t.remove_prefix(1);
        if (t.empty()) return;
        std::wstring clean;
        clean.reserve(t.size());
        for (wchar_t c : t) {
            if (c != L'\r') clean.push_back(c);
        }
        Append(clean);
        return;
    }
    if (!html_block_mode_) {
        // Inline raw HTML text inside a Markdown paragraph (rare: CDATA).
        Append(text);
        return;
    }
    // Collapse whitespace like a browser.
    std::wstring c;
    c.reserve(text.size());
    bool space = false;
    for (wchar_t ch : text) {
        if (IsSpace(ch)) {
            space = true;
            continue;
        }
        if (space) c.push_back(L' ');
        space = false;
        c.push_back(ch);
    }
    if (space) c.push_back(L' ');
    if (c.empty()) return;
    if (!top->IsLeaf()) {
        if (c == L" ") return;  // inter-element whitespace
        if (image_depth_ == 0 && hidden_depth_ == 0 && c.front() == L' ') c.erase(0, 1);
    } else if (!top->text.empty() && c.front() == L' ') {
        const wchar_t last = top->text.back();
        if (last == L' ' || last == kLineBreak) c.erase(0, 1);
    } else if (top->text.empty() && c.front() == L' ') {
        c.erase(0, 1);
    }
    if (!c.empty()) AppendNormal(c);
}

Block* Builder::PushHtml(BlockType type, const HtmlToken& tok) {
    CloseLeaf();
    const BlockType top = Top()->type;
    if (type != BlockType::TableRow && type != BlockType::TableCell &&
        (top == BlockType::Table || top == BlockType::TableRow)) {
        // Content directly in <table>/<tr> (browsers move it out of the
        // table): an implicit cell lays it out like any other.
        auto implicit = [this](BlockType t, Tag tag) {
            Block* c = Push(t);
            c->from_html = true;
            c->html_tag = static_cast<uint16_t>(tag);
            c->header = t == BlockType::TableRow && html_thead_;
        };
        if (top == BlockType::Table) implicit(BlockType::TableRow, Tag::Tr);
        implicit(BlockType::TableCell, Tag::Td);
    }
    Block* b = Push(type);
    b->from_html = true;
    b->html_tag = static_cast<uint16_t>(tok.tag);
    b->align = AlignFromAttr(tok);
    if (const std::wstring* id = tok.Attr(L"id")) {
        // A paragraph that stays empty is dropped: its anchor then moves on.
        if (doc_.anchors.try_emplace(*id, Anchor{b, 0}).second && b->IsLeaf()) leaf_anchors_.push_back(*id);
    }
    return b;
}

Block* Builder::FindOpenHtml(Tag tag, Tag stop) const {
    for (size_t i = stack_.size(); i-- > 1;) {
        Block* b = stack_[i];
        if (b->anonymous) continue;         // implied paragraphs are transparent
        if (!b->from_html) return nullptr;  // never cross Markdown-owned blocks
        const Tag t = static_cast<Tag>(b->html_tag);
        if (t == tag) return b;
        if (stop != Tag::Unknown && t == stop) return nullptr;
    }
    return nullptr;
}

bool Builder::PopHtml(Tag tag) {
    for (size_t i = stack_.size(); i-- > 1;) {
        Block* b = stack_[i];
        if (b->anonymous) continue;
        if (!b->from_html) return false;
        const Tag t = static_cast<Tag>(b->html_tag);
        const bool match = t == tag || (IsHeadingTag(tag) && IsHeadingTag(t)) ||
                           (IsListTag(tag) && IsListTag(t)) ||
                           ((tag == Tag::Td || tag == Tag::Th) && (t == Tag::Td || t == Tag::Th));
        if (match) {
            PopTo(b);
            return true;
        }
    }
    return false;
}

void Builder::HtmlImage(const HtmlToken& tok) {
    ImageRef img;
    if (const auto* v = tok.Attr(L"src")) img.src = std::wstring(Trim(*v));
    if (const auto* v = tok.Attr(L"alt")) img.alt = *v;
    if (const auto* v = tok.Attr(L"title")) img.title = *v;
    if (const auto* v = tok.Attr(L"width")) ParseLength(*v, img.width, img.width_percent);
    if (const auto* v = tok.Attr(L"height")) ParseLength(*v, img.height, img.height_percent);
    if (in_picture_) {
        img.src_dark = picture_dark_;
        img.src_light = picture_light_;
    }
    ApplyThemeFragment(img);
    if (const auto* v = tok.Attr(L"align")) {
        if (EqualsNoCase(*v, L"right") || EqualsNoCase(*v, L"center")) {
            if (Block* leaf = EnsureLeaf(); leaf && leaf->text.empty() && leaf->anonymous) {
                leaf->align = EqualsNoCase(*v, L"right") ? Align::Right : Align::Center;
            }
        }
    }
    AppendImage(std::move(img));
}

void Builder::HtmlStartTag(const HtmlToken& tok) {
    const Tag t = tok.tag;
    if (hidden_depth_ > 0) {
        if (IsHiddenTag(t) && !tok.self_closing) ++hidden_depth_;
        return;
    }
    if (IsHiddenTag(t)) {
        if (!tok.self_closing) ++hidden_depth_;
        return;
    }
    if (const uint32_t flag = InlineFlagFor(t)) {
        if (t == Tag::Code && Top()->type == BlockType::CodeBlock && Top()->from_html) {
            // <pre><code class="language-x">: language hint only.
            if (const auto* cls = tok.Attr(L"class")) {
                std::wstring_view c = *cls;
                for (std::wstring_view prefix : {L"language-", L"lang-"}) {
                    const size_t p = c.find(prefix);
                    if (p != std::wstring_view::npos) {
                        size_t e = p + prefix.size();
                        while (e < c.size() && !IsSpace(c[e])) ++e;
                        Top()->lang = std::wstring(c.substr(p + prefix.size(), e - p - prefix.size()));
                        break;
                    }
                }
            }
            return;
        }
        if (!tok.self_closing) PushStyle(flag);
        if (const auto* id = tok.Attr(L"id")) RegisterAnchor(*id);
        return;
    }
    switch (t) {
    case Tag::A: {
        if (const auto* id = tok.Attr(L"id")) RegisterAnchor(*id);
        if (const auto* name = tok.Attr(L"name")) RegisterAnchor(*name);
        const std::wstring* href = tok.Attr(L"href");
        if (tok.self_closing) return;
        if (href) {
            Link link;
            link.url = std::wstring(Trim(*href));
            if (const auto* title = tok.Attr(L"title")) link.title = *title;
            doc_.links.push_back(std::move(link));
            links_.push_back(static_cast<int32_t>(doc_.links.size() - 1));
        }
        a_has_href_.push_back(href != nullptr);
        return;
    }
    case Tag::Br:
        Append(std::wstring_view(&kLineBreak, 1));
        return;
    case Tag::Wbr:
        Append(L"​");
        return;
    case Tag::Img:
        HtmlImage(tok);
        return;
    case Tag::Picture:
        in_picture_ = true;
        picture_dark_.clear();
        picture_light_.clear();
        return;
    case Tag::Source:
        if (in_picture_) {
            const std::wstring* media = tok.Attr(L"media");
            const std::wstring* srcset = tok.Attr(L"srcset");
            if (media && srcset) {
                const std::wstring m = ToLower(*media);
                if (m.find(L"dark") != std::wstring::npos) picture_dark_ = FirstSrcsetUrl(*srcset);
                else if (m.find(L"light") != std::wstring::npos) picture_light_ = FirstSrcsetUrl(*srcset);
            }
        }
        return;
    case Tag::Q:
        Append(L"“");
        return;
    case Tag::Input: {
        const std::wstring* type = tok.Attr(L"type");
        if (type && EqualsNoCase(*type, L"checkbox")) {
            Append(tok.HasAttr(L"checked") ? L"☑ " : L"☐ ");
        }
        return;
    }
    case Tag::Video:
    case Tag::Audio: {
        const std::wstring* src = tok.Attr(L"src");
        if (src && !src->empty()) {
            Link link;
            link.url = std::wstring(Trim(*src));
            doc_.links.push_back(std::move(link));
            links_.push_back(static_cast<int32_t>(doc_.links.size() - 1));
            Append(t == Tag::Video ? L"▶ Video" : L"▶ Audio");
            links_.pop_back();
        }
        if (!tok.self_closing) ++hidden_depth_;  // hide fallback content
        return;
    }
    default:
        break;
    }

    if (!html_block_mode_) {
        // Block-level tags inside a Markdown paragraph don't change structure.
        if (const auto* id = tok.Attr(L"id")) RegisterAnchor(*id);
        return;
    }

    switch (t) {
    case Tag::P:
    case Tag::Figcaption:
    case Tag::Legend:
    case Tag::Caption: {
        Block* b = PushHtml(BlockType::Paragraph, tok);
        if (t == Tag::Caption) b->align = Align::Center;
        if (t == Tag::Legend) b->base_flags = kBold;
        break;
    }
    case Tag::H1: case Tag::H2: case Tag::H3: case Tag::H4: case Tag::H5: case Tag::H6: {
        Block* b = PushHtml(BlockType::Heading, tok);
        b->level = static_cast<uint8_t>(static_cast<int>(t) - static_cast<int>(Tag::H1) + 1);
        break;
    }
    case Tag::Blockquote:
        PushHtml(BlockType::Quote, tok);
        break;
    case Tag::Pre: {
        Block* b = PushHtml(BlockType::CodeBlock, tok);
        if (const auto* lang = tok.Attr(L"lang")) b->lang = *lang;
        break;
    }
    case Tag::Ul: case Tag::Ol: case Tag::Menu: case Tag::Dir: {
        Block* b = PushHtml(BlockType::List, tok);
        b->ordered = t == Tag::Ol;
        b->tight = true;
        b->number = 1;
        // (Bounded like Markdown lists: item numbers add to it.)
        if (const auto* start = tok.Attr(L"start")) b->number = std::clamp(_wtoi(start->c_str()), -999999999, 999999999);
        break;
    }
    case Tag::Li: {
        // Close a previous <li> of the same list (no nested list in between).
        for (size_t i = stack_.size(); i-- > 1;) {
            Block* b = stack_[i];
            if (b->anonymous) continue;
            if (!b->from_html || b->type == BlockType::List) break;
            if (b->type == BlockType::ListItem) {
                PopTo(b);
                break;
            }
        }
        CloseLeaf();
        if (Top()->type != BlockType::List) {
            HtmlToken ul;
            ul.tag = Tag::Ul;
            Block* list = PushHtml(BlockType::List, ul);
            list->tight = true;
            list->number = 1;
        }
        Block* list = Top();
        const int index = static_cast<int>(list->children.size());
        Block* b = PushHtml(BlockType::ListItem, tok);
        b->number = list->number + index;
        if (const auto* v = tok.Attr(L"value")) b->number = std::clamp(_wtoi(v->c_str()), -999999999, 999999999);
        b->tight = true;
        break;
    }
    case Tag::Dt:
    case Tag::Dd: {
        // A new <dt>/<dd> closes the previous one of the same <dl>.
        for (size_t i = stack_.size(); i-- > 1;) {
            Block* b = stack_[i];
            if (b->anonymous) continue;
            if (!b->from_html) break;
            const Tag open = static_cast<Tag>(b->html_tag);
            if (open == Tag::Dl) break;
            if (open == Tag::Dt || open == Tag::Dd) {
                PopTo(b);
                break;
            }
        }
        if (t == Tag::Dt) {
            Block* b = PushHtml(BlockType::Paragraph, tok);
            b->base_flags = kBold | kItalic;
        } else {
            Block* b = PushHtml(BlockType::Div, tok);
            b->indent = 1.0f;
        }
        break;
    }
    case Tag::Table:
        PushHtml(BlockType::Table, tok);
        html_thead_ = false;
        break;
    case Tag::Thead:
        html_thead_ = true;
        break;
    case Tag::Tbody:
    case Tag::Tfoot:
        html_thead_ = false;
        break;
    case Tag::Tr: {
        Block* table = FindOpenHtml(Tag::Table);
        if (!table) break;
        PopAbove(table);
        Block* b = PushHtml(BlockType::TableRow, tok);
        b->header = html_thead_;
        b->valign = VAlignFromAttr(tok);
        break;
    }
    case Tag::Td:
    case Tag::Th: {
        Block* table = FindOpenHtml(Tag::Table);
        if (!table) {
            PushHtml(BlockType::Div, tok);
            break;
        }
        Block* row = FindOpenHtml(Tag::Tr, Tag::Table);
        if (row) {
            PopAbove(row);
        } else {
            PopAbove(table);
            HtmlToken tr;
            tr.tag = Tag::Tr;
            row = PushHtml(BlockType::TableRow, tr);
            row->header = html_thead_;
        }
        Block* b = PushHtml(BlockType::TableCell, tok);
        b->header = t == Tag::Th;
        b->valign = tok.Attr(L"valign") ? VAlignFromAttr(tok) : row->valign;
        if (const auto* cs = tok.Attr(L"colspan")) b->colspan = std::clamp(_wtoi(cs->c_str()), 1, 64);
        break;
    }
    case Tag::Details: {
        Block* b = PushHtml(BlockType::Details, tok);
        b->open = tok.HasAttr(L"open");
        break;
    }
    case Tag::Summary:
        PushHtml(BlockType::Summary, tok);
        break;
    case Tag::Hr:
        CloseLeaf();
        PushHtml(BlockType::Rule, tok);
        Pop();
        break;
    case Tag::Center: {
        Block* b = PushHtml(BlockType::Div, tok);
        b->align = Align::Center;
        break;
    }
    default:
        if (IsContainerTag(t)) {
            PushHtml(BlockType::Div, tok);
        } else if (const auto* id = tok.Attr(L"id")) {
            RegisterAnchor(*id);
        }
        break;
    }
}

void Builder::HtmlEndTag(const HtmlToken& tok) {
    const Tag t = tok.tag;
    if (hidden_depth_ > 0) {
        if (IsHiddenTag(t) || t == Tag::Video || t == Tag::Audio) --hidden_depth_;
        return;
    }
    if (const uint32_t flag = InlineFlagFor(t)) {
        if (!(t == Tag::Code && Top()->type == BlockType::CodeBlock && Top()->from_html)) PopStyle(flag);
        return;
    }
    switch (t) {
    case Tag::A:
        if (!a_has_href_.empty()) {
            if (a_has_href_.back() && !links_.empty()) links_.pop_back();
            a_has_href_.pop_back();
        }
        return;
    case Tag::Q:
        Append(L"”");
        return;
    case Tag::Picture:
        in_picture_ = false;
        return;
    case Tag::Br:
        Append(std::wstring_view(&kLineBreak, 1));
        return;
    default:
        break;
    }
    if (!html_block_mode_) return;
    switch (t) {
    case Tag::P: case Tag::Figcaption: case Tag::Legend: case Tag::Caption: case Tag::Dt:
    case Tag::H1: case Tag::H2: case Tag::H3: case Tag::H4: case Tag::H5: case Tag::H6:
    case Tag::Summary:
        if (Top()->IsLeaf() && Top()->from_html) {
            const Tag open = static_cast<Tag>(Top()->html_tag);
            if (open == t || (IsHeadingTag(open) && IsHeadingTag(t))) Pop();
        }
        break;
    case Tag::Thead:
        html_thead_ = false;
        break;
    default:
        if (t != Tag::Unknown && t != Tag::Tbody && t != Tag::Tfoot) PopHtml(t);
        break;
    }
}

}  // namespace

std::wstring MakeSlug(std::wstring_view text) {
    const std::wstring lower = ToLower(text);
    std::wstring out;
    out.reserve(lower.size());
    for (size_t i = 0; i < lower.size(); ++i) {
        const wchar_t c = lower[i];
        if (c == L' ') {
            out.push_back(L'-');
            continue;
        }
        if (c == L'-' || c == L'_') {
            out.push_back(c);
            continue;
        }
        if (c < 0x80) {
            if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) out.push_back(c);
            continue;
        }
        if (c >= 0xD800 && c <= 0xDFFF) continue;  // astral: emoji etc.
        WORD type1 = 0, type3 = 0;
        GetStringTypeW(CT_CTYPE1, &c, 1, &type1);
        GetStringTypeW(CT_CTYPE3, &c, 1, &type3);
        if ((type1 & (C1_ALPHA | C1_DIGIT)) || (type3 & (C3_NONSPACING | C3_DIACRITIC))) {
            out.push_back(c);
        }
    }
    return out;
}

std::unique_ptr<Document> ParseMarkdown(std::string_view utf8, std::wstring_view path,
                                        const ParseOptions& options) {
    auto doc = std::make_unique<Document>();
    doc->path = std::wstring(path);
    doc->base_dir = DirectoryOf(path);

    // YAML front matter at the very top becomes a table (like on GitHub).
    std::string with_front_matter;
    if (options.footnote_nesting == 0) {
        std::string replacement;
        if (const size_t n = RenderFrontMatter(utf8, replacement)) {
            with_front_matter = replacement;
            with_front_matter.append(utf8.substr(n));
            utf8 = with_front_matter;
        }
    }

    // Multi-block footnote bodies are parsed as sub-documents; bodies nested
    // in bodies recurse, so the depth is bounded (deeper ones stay inline).
    FootnoteBodies bodies;
    std::string rewritten;
    if (options.footnote_nesting < kMaxFootnoteNesting) rewritten = ExtractFootnoteBodies(utf8, bodies);
    if (!rewritten.empty()) utf8 = rewritten;

    Builder builder(*doc, options, bodies.empty() ? nullptr : &bodies);
    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH |
                   MD_FLAG_TASKLISTS | MD_FLAG_ADMONITIONS | MD_FLAG_FOOTNOTES;
    if (options.math) parser.flags |= MD_FLAG_LATEXMATHSPANS;
    parser.enter_block = &Builder::OnEnterBlock;
    parser.leave_block = &Builder::OnLeaveBlock;
    parser.enter_span = &Builder::OnEnterSpan;
    parser.leave_span = &Builder::OnLeaveSpan;
    parser.text = &Builder::OnText;
    const MD_SIZE size = static_cast<MD_SIZE>(std::min<size_t>(utf8.size(), 0x7FFFFFFF));
    md_parse(utf8.data(), size, &parser, &builder);
    if (builder.error()) std::rethrow_exception(builder.error());
    builder.Finish();
    if (options.footnote_nesting == 0) AddFlagImages(*doc);  // footnote bodies are part of *doc by now
    return doc;
}

}  // namespace mdv

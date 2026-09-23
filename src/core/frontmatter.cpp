#include "core/frontmatter.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace mdv {
namespace {

constexpr size_t kMaxLines = 2000;
constexpr int kMaxDepth = 12;

struct Node {
    enum Kind { Scalar, Seq, Map } kind = Scalar;
    std::string text;                                  // Scalar
    std::vector<Node> items;                           // Seq
    std::vector<std::pair<std::string, Node>> fields;  // Map
};

struct Line {
    int indent;
    std::string_view text;  // without indentation and trailing spaces
};

std::string_view TrimRight(std::string_view s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::string_view Trim(std::string_view s) {
    s = TrimRight(s);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    return s;
}

bool IsDelimiter(std::string_view line, bool closing) {
    line = TrimRight(line);
    return line == "---" || (closing && line == "...");
}

// A scalar: quotes removed, trailing comment dropped.
std::string Unquote(std::string_view s) {
    s = Trim(s);
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) {
        const char q = s.front();
        std::string out;
        for (size_t i = 1; i + 1 < s.size(); ++i) {
            const char c = s[i];
            if (q == '\'' && c == '\'' && i + 2 < s.size() && s[i + 1] == '\'') {
                out.push_back('\'');
                ++i;
            } else if (q == '"' && c == '\\' && i + 2 < s.size()) {
                const char e = s[++i];
                out.push_back(e == 'n' ? '\n' : e == 't' ? '\t' : e);
            } else {
                out.push_back(c);
            }
        }
        return out;
    }
    // Plain scalar: " #" starts a comment.
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        if ((s[i] == ' ' || s[i] == '\t') && s[i + 1] == '#') {
            s = Trim(s.substr(0, i));
            break;
        }
    }
    if (!s.empty() && s.front() == '#') return {};
    return std::string(s);
}

// Splits a flow collection body ("a, 'b, c', [d]") at top-level commas.
std::vector<std::string_view> SplitFlow(std::string_view s) {
    std::vector<std::string_view> parts;
    int depth = 0;
    char quote = 0;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '[' || c == '{') {
            ++depth;
        } else if (c == ']' || c == '}') {
            --depth;
        } else if (c == ',' && depth == 0) {
            parts.push_back(Trim(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    const std::string_view last = Trim(s.substr(start));
    if (!last.empty() || !parts.empty()) parts.push_back(last);
    return parts;
}

// Position of the ':' ending a mapping key, or npos.
size_t KeyColon(std::string_view s) {
    char quote = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quote) {
            if (c == quote) quote = 0;
            continue;
        }
        if (i == 0 && (c == '"' || c == '\'')) {
            quote = c;
            continue;
        }
        if (c == ' ' && i + 1 < s.size() && s[i + 1] == '#') return std::string_view::npos;
        if (c == ':' && (i + 1 == s.size() || s[i + 1] == ' ' || s[i + 1] == '\t')) return i;
        if (c == '[' || c == '{') return std::string_view::npos;
    }
    return std::string_view::npos;
}

bool ParseFlow(std::string_view s, Node& out, int depth);

bool ParseInlineValue(std::string_view s, Node& out, int depth) {
    s = Trim(s);
    if (!s.empty() && (s.front() == '[' || s.front() == '{')) return ParseFlow(s, out, depth);
    out.kind = Node::Scalar;
    out.text = Unquote(s);
    return true;
}

bool ParseFlow(std::string_view s, Node& out, int depth) {
    if (depth > kMaxDepth) return false;
    // Strip a trailing comment after the closing bracket.
    const char open = s.front(), close = open == '[' ? ']' : '}';
    const size_t end = s.rfind(close);
    if (end == std::string_view::npos) return false;
    if (!Trim(s.substr(end + 1)).empty() && Trim(s.substr(end + 1)).front() != '#') return false;
    const std::string_view body = s.substr(1, end - 1);
    if (open == '[') {
        out.kind = Node::Seq;
        for (std::string_view part : SplitFlow(body)) {
            Node item;
            if (!ParseInlineValue(part, item, depth + 1)) return false;
            out.items.push_back(std::move(item));
        }
    } else {
        out.kind = Node::Map;
        for (std::string_view part : SplitFlow(body)) {
            const size_t colon = KeyColon(part);
            Node value;
            if (colon == std::string_view::npos) {
                out.fields.emplace_back(Unquote(part), Node{});
                continue;
            }
            if (!ParseInlineValue(part.substr(colon + 1), value, depth + 1)) return false;
            out.fields.emplace_back(Unquote(part.substr(0, colon)), std::move(value));
        }
    }
    return true;
}

class Parser {
public:
    explicit Parser(std::vector<Line> lines) : lines_(std::move(lines)) {}

    bool ParseDocument(Node& root) {
        Skip();
        if (i_ >= lines_.size()) {
            root.kind = Node::Map;  // empty front matter
            return true;
        }
        if (!ParseBlock(lines_[i_].indent, root, 0)) return false;
        Skip();
        return i_ >= lines_.size();
    }

private:
    void Skip() {
        while (i_ < lines_.size() && (lines_[i_].text.empty() || lines_[i_].text.front() == '#')) ++i_;
    }

    static bool IsSeqItem(std::string_view t) { return t == "-" || (t.size() > 1 && t[0] == '-' && t[1] == ' '); }

    bool ParseBlock(int indent, Node& out, int depth) {
        if (depth > kMaxDepth) return false;
        Skip();
        if (i_ >= lines_.size()) return true;
        if (IsSeqItem(lines_[i_].text)) return ParseSeq(indent, out, depth);
        if (KeyColon(lines_[i_].text) != std::string_view::npos) return ParseMap(indent, out, depth);
        // A plain multi-line scalar.
        out.kind = Node::Scalar;
        while (i_ < lines_.size() && lines_[i_].indent >= indent && !lines_[i_].text.empty()) {
            if (!out.text.empty()) out.text.push_back(' ');
            out.text += Unquote(lines_[i_].text);
            ++i_;
        }
        return true;
    }

    // The value after "key:" or "- ": inline, block scalar, or nested block.
    bool ParseValue(std::string_view rest, int parent_indent, Node& out, int depth, bool allow_same_indent_seq) {
        rest = Trim(rest);
        if (!rest.empty() && rest.front() != '#') {
            if (rest.front() == '|' || rest.front() == '>') return ParseBlockScalar(rest.front() == '|', parent_indent, out);
            if (rest.front() == '&' || rest.front() == '*' || rest.front() == '!') return false;  // anchors, tags
            return ParseInlineValue(rest, out, depth);
        }
        Skip();
        if (i_ >= lines_.size()) return true;  // null
        const Line& next = lines_[i_];
        if (next.indent > parent_indent || (allow_same_indent_seq && next.indent == parent_indent && IsSeqItem(next.text))) {
            return ParseBlock(next.indent, out, depth + 1);
        }
        return true;  // null value
    }

    bool ParseBlockScalar(bool literal, int parent_indent, Node& out) {
        out.kind = Node::Scalar;
        int block_indent = -1;
        std::vector<std::string_view> parts;
        while (i_ < lines_.size()) {
            const Line& l = lines_[i_];
            if (!l.text.empty()) {
                if (l.indent <= parent_indent) break;
                if (block_indent < 0) block_indent = l.indent;
            }
            parts.push_back(l.text);
            ++i_;
        }
        while (!parts.empty() && parts.back().empty()) parts.pop_back();
        for (size_t k = 0; k < parts.size(); ++k) {
            if (k) out.text.push_back(literal || parts[k].empty() || parts[k - 1].empty() ? '\n' : ' ');
            out.text += parts[k];
        }
        return true;
    }

    bool ParseMap(int indent, Node& out, int depth) {
        out.kind = Node::Map;
        while (true) {
            Skip();
            if (i_ >= lines_.size()) return true;
            const Line& l = lines_[i_];
            if (l.indent < indent) return true;
            if (l.indent > indent || IsSeqItem(l.text)) return false;
            const size_t colon = KeyColon(l.text);
            if (colon == std::string_view::npos) return false;
            std::string key = Unquote(l.text.substr(0, colon));
            const std::string_view rest = l.text.substr(colon + 1);
            ++i_;
            Node value;
            if (!ParseValue(rest, indent, value, depth, true)) return false;
            out.fields.emplace_back(std::move(key), std::move(value));
        }
    }

    bool ParseSeq(int indent, Node& out, int depth) {
        out.kind = Node::Seq;
        while (true) {
            Skip();
            if (i_ >= lines_.size()) return true;
            Line& l = lines_[i_];
            if (l.indent < indent || !IsSeqItem(l.text)) return l.indent <= indent;
            if (l.indent > indent) return false;
            std::string_view rest = l.text.size() > 1 ? l.text.substr(2) : std::string_view{};
            size_t lead = 0;
            while (lead < rest.size() && rest[lead] == ' ') ++lead;
            rest = rest.substr(lead);
            Node item;
            if (!rest.empty() && KeyColon(rest) != std::string_view::npos && rest.front() != '[' && rest.front() != '{') {
                // "- key: value" starts a mapping indented at the key's column.
                l.indent = indent + 2 + static_cast<int>(lead);
                l.text = rest;
                if (!ParseMap(l.indent, item, depth + 1)) return false;
            } else {
                ++i_;
                if (!ParseValue(rest, indent, item, depth, false)) return false;
            }
            out.items.push_back(std::move(item));
        }
    }

    std::vector<Line> lines_;
    size_t i_ = 0;
};

void AppendEscaped(std::string& out, std::string_view s) {
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\n': out += "<br>"; break;
        default: out.push_back(c);
        }
    }
}

constexpr size_t kInlineListChars = 100;  // longer scalar lists: one item per line

void AppendNode(std::string& out, const Node& n);

// A mapping is a two-column table: the key (bold) and its value, one row
// per key. GitHub lays the keys out as columns instead, which gets wide and
// scrolls; this reads top to bottom.
void AppendMap(std::string& out, const Node& n) {
    if (n.fields.empty()) return;
    out += "<table><tbody>";
    for (const auto& f : n.fields) {
        out += "<tr valign=\"top\"><th align=\"left\">";
        AppendEscaped(out, f.first);
        out += "</th><td>";
        AppendNode(out, f.second);
        out += "</td></tr>";
    }
    out += "</tbody></table>";
}

void AppendNode(std::string& out, const Node& n) {
    switch (n.kind) {
    case Node::Scalar:
        AppendEscaped(out, n.text);
        break;
    case Node::Seq: {
        // Short plain lists (tags...) read best on one line; anything else
        // gets one item per line, mappings as their own small tables.
        bool plain = true;
        size_t chars = 0;
        for (const Node& item : n.items) {
            // (An item with a comma of its own would be ambiguous in a comma list.)
            if (item.kind != Node::Scalar || item.text.find_first_of("\n,") != std::string::npos) plain = false;
            chars += item.text.size() + 2;
        }
        if (plain && chars <= kInlineListChars) {
            for (size_t i = 0; i < n.items.size(); ++i) {
                if (i) out += ", ";
                AppendEscaped(out, n.items[i].text);
            }
            break;
        }
        for (const Node& item : n.items) {
            out += "<div>";
            AppendNode(out, item);
            out += "</div>";
        }
        break;
    }
    case Node::Map:
        AppendMap(out, n);
        break;
    }
}

}  // namespace

size_t RenderFrontMatter(std::string_view md, std::string& replacement) {
    // First line: "---" exactly (a thematic break "- - -" or "----" is not front matter).
    size_t eol = md.find('\n');
    if (eol == std::string_view::npos || !IsDelimiter(md.substr(0, eol), false)) return 0;
    std::vector<Line> lines;
    size_t pos = eol + 1;
    size_t end = 0;
    while (pos < md.size()) {
        eol = md.find('\n', pos);
        const size_t next = eol == std::string_view::npos ? md.size() : eol + 1;
        std::string_view raw = md.substr(pos, next - pos);
        if (!raw.empty() && raw.back() == '\n') raw.remove_suffix(1);
        if (IsDelimiter(raw, true)) {
            end = next;
            break;
        }
        if (lines.size() >= kMaxLines) return 0;
        int indent = 0;
        size_t k = 0;
        while (k < raw.size() && raw[k] == ' ') {
            ++indent;
            ++k;
        }
        if (k < raw.size() && raw[k] == '\t') return 0;  // tabs are not YAML indentation
        lines.push_back(Line{indent, TrimRight(raw.substr(k))});
        pos = next;
    }
    if (end == 0) return 0;  // never closed: not front matter
    // Only YAML that starts like a mapping: "---", "Foo", "---" is a thematic
    // break and a setext heading (GFM spec), and an empty block is two breaks.
    const Line* first = nullptr;
    for (const Line& l : lines) {
        if (!l.text.empty() && l.text.front() != '#') {
            first = &l;
            break;
        }
    }
    if (!first || KeyColon(first->text) == std::string_view::npos) return 0;

    Node root;
    Parser parser(lines);
    if (parser.ParseDocument(root) && root.kind == Node::Map) {
        replacement.clear();
        if (!root.fields.empty()) {
            AppendNode(replacement, root);
            replacement += "\n\n";
        }
        return end;
    }
    // Beyond the supported subset: show the YAML as a code block.
    const std::string_view body = md.substr(md.find('\n') + 1, pos - md.find('\n') - 1);
    size_t fence = 3, run = 0;
    for (char c : body) {
        run = c == '`' ? run + 1 : 0;
        fence = std::max(fence, run + 1);
    }
    replacement.assign(fence, '`');
    replacement += "yaml\n";
    replacement += body;
    if (!body.empty() && body.back() != '\n') replacement += '\n';
    replacement.append(fence, '`');
    replacement += "\n\n";
    return end;
}

}  // namespace mdv

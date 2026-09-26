#include "html_dump.h"

#include <algorithm>
#include <cstdio>

#include "core/html.h"
#include "core/util.h"

namespace mdv::test {
namespace {

void EscapeText(std::string& out, std::wstring_view s) {
    const std::string u = WideToUtf8(s);
    for (char c : u) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
}

// Same escaping as md4c-html's render_url_escaped.
void EscapeUrl(std::string& out, std::wstring_view s) {
    static const char kHex[] = "0123456789ABCDEF";
    const std::string u = WideToUtf8(s);
    for (unsigned char c : u) {
        if (c == '&') {
            out += "&amp;";
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   std::string_view("~-_.+!*(),%#@?=;:/,+$").find(static_cast<char>(c)) !=
                       std::string_view::npos) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
}

struct Dumper {
    const Document& doc;
    std::string out;

    void Inline(const Block* b) {
        // Walk runs; open/close tags by diffing flags with the previous run.
        uint32_t open_flags = 0;
        int32_t open_link = -1;
        auto close_all = [&] {
            if (open_flags & kSup) out += "</sup>";
            if (open_flags & kCode) out += "</code>";
            if (open_flags & kStrike) out += "</del>";
            if (open_flags & kBold) out += "</strong>";
            if (open_flags & kItalic) out += "</em>";
            if (open_link >= 0) out += "</a>";
            open_flags = 0;
            open_link = -1;
        };
        for (const StyleRun& r : b->runs) {
            const uint32_t f = r.flags & (kItalic | kBold | kStrike | kCode | kSup);
            if (f != open_flags || r.link != open_link) {
                close_all();
                if (r.link >= 0) {
                    const Link& l = doc.links[static_cast<size_t>(r.link)];
                    out += "<a href=\"";
                    EscapeUrl(out, l.url);
                    out += "\"";
                    if (!l.title.empty()) {
                        out += " title=\"";
                        EscapeText(out, l.title);
                        out += "\"";
                    }
                    out += ">";
                }
                if (f & kItalic) out += "<em>";
                if (f & kBold) out += "<strong>";
                if (f & kStrike) out += "<del>";
                if (f & kCode) out += "<code>";
                if (f & kSup) out += "<sup>";
                open_flags = f;
                open_link = r.link;
            }
            for (uint32_t i = r.start; i < r.start + r.length; ++i) {
                const wchar_t c = b->text[i];
                if (c == 0xFFFC) {
                    const InlineObject* obj = nullptr;
                    for (const auto& o : b->objects) {
                        if (o.pos == i) obj = &o;
                    }
                    if (obj && obj->image >= 0) {
                        const ImageRef& img = doc.images[static_cast<size_t>(obj->image)];
                        out += "<img src=\"";
                        EscapeUrl(out, img.src);
                        out += "\" alt=\"";
                        EscapeText(out, img.alt);
                        out += "\"";
                        if (!img.title.empty()) {
                            out += " title=\"";
                            EscapeText(out, img.title);
                            out += "\"";
                        }
                        out += " />";
                    }
                } else if (c == 0x2028) {
                    out += "<br />\n";
                } else if (c >= 0xD800 && c <= 0xDBFF && i + 1 < r.start + r.length) {
                    EscapeText(out, std::wstring_view(b->text.data() + i, 2));
                    ++i;
                } else {
                    EscapeText(out, std::wstring_view(&c, 1));
                }
            }
        }
        close_all();
    }

    void BlockOut(const Block* b, bool tight) {
        switch (b->type) {
        case BlockType::Document:
            for (const Block* c : b->children) BlockOut(c, false);
            break;
        case BlockType::Paragraph:
            if (b->anonymous && tight) {
                Inline(b);
            } else {
                out += "<p>";
                Inline(b);
                out += "</p>\n";
            }
            break;
        case BlockType::Heading: {
            char tag[8];
            std::snprintf(tag, sizeof tag, "h%d", b->level);
            out += "<";
            out += tag;
            out += ">";
            Inline(b);
            out += "</";
            out += tag;
            out += ">\n";
            break;
        }
        case BlockType::CodeBlock:
            out += "<pre><code";
            if (!b->lang.empty()) {
                out += " class=\"language-";
                EscapeText(out, b->lang);
                out += "\"";
            }
            out += ">";
            EscapeText(out, b->text);
            if (!b->text.empty()) out += "\n";
            out += "</code></pre>\n";
            break;
        case BlockType::Quote:
            out += "<blockquote>\n";
            for (const Block* c : b->children) BlockOut(c, false);
            out += "</blockquote>\n";
            break;
        case BlockType::List: {
            if (b->ordered) {
                if (b->number != 1) {
                    out += "<ol start=\"" + std::to_string(b->number) + "\">\n";
                } else {
                    out += "<ol>\n";
                }
            } else {
                out += "<ul>\n";
            }
            for (const Block* c : b->children) BlockOut(c, b->tight);
            out += b->ordered ? "</ol>\n" : "</ul>\n";
            break;
        }
        case BlockType::ListItem:
            out += "<li>";
            if (b->task >= 0) {
                out += b->task ? "<input checked=\"\" disabled=\"\" type=\"checkbox\"> "
                               : "<input disabled=\"\" type=\"checkbox\"> ";
            }
            for (const Block* c : b->children) {
                if (!(c->type == BlockType::Paragraph && tight)) out += "\n";
                BlockOut(c, tight);
            }
            out += "</li>\n";
            break;
        case BlockType::Table: {
            out += "<table>\n";
            bool in_head = false, in_body = false;
            for (const Block* row : b->children) {
                if (row->header && !in_head) { out += "<thead>\n"; in_head = true; }
                if (!row->header && !in_body) {
                    if (in_head) out += "</thead>\n";
                    out += "<tbody>\n";
                    in_body = true;
                }
                out += "<tr>\n";
                for (const Block* cell : row->children) {
                    const char* tag = cell->header ? "th" : "td";
                    out += "<";
                    out += tag;
                    switch (cell->align) {
                    case Align::Left: out += " align=\"left\""; break;
                    case Align::Center: out += " align=\"center\""; break;
                    case Align::Right: out += " align=\"right\""; break;
                    default: break;
                    }
                    out += ">";
                    for (const Block* c : cell->children) BlockOut(c, true);
                    out += "</";
                    out += tag;
                    out += ">\n";
                }
                out += "</tr>\n";
            }
            if (in_body) out += "</tbody>\n";
            else if (in_head) out += "</thead>\n";
            out += "</table>\n";
            break;
        }
        case BlockType::Rule:
            out += "<hr />\n";
            break;
        case BlockType::Details:
            out += "<details>\n";
            for (const Block* c : b->children) BlockOut(c, false);
            out += "</details>\n";
            break;
        case BlockType::Summary:
            out += "<summary>";
            Inline(b);
            out += "</summary>\n";
            break;
        default:
            for (const Block* c : b->children) BlockOut(c, false);
            break;
        }
    }
};

bool IsBlockTag(std::wstring_view n) {
    static constexpr std::wstring_view kBlock[] = {
        L"p", L"li", L"ul", L"ol", L"blockquote", L"h1", L"h2", L"h3", L"h4", L"h5", L"h6",
        L"pre", L"table", L"thead", L"tbody", L"tr", L"th", L"td", L"hr", L"div", L"br",
        L"section"};
    for (auto b : kBlock) {
        if (n == b) return true;
    }
    return false;
}

}  // namespace

std::string DumpHtml(const Document& doc) {
    Dumper d{doc, {}};
    d.BlockOut(doc.root, false);
    return d.out;
}

std::string NormalizeHtml(const std::string& html) {
    const std::wstring w = Utf8ToWide(html);
    HtmlTokenizer tz(w);
    struct Tok {
        HtmlToken::Kind kind;
        std::wstring s;
        bool block;
        bool pre;
    };
    std::vector<Tok> toks;
    HtmlToken t;
    int pre = 0;
    // Inline formatting is flattened: each text run is prefixed with the
    // sorted set of formatting elements (and link targets) active on it, so
    // that "<strong>a <em>b</em></strong>" and
    // "<strong>a </strong><em><strong>b</strong></em>" compare equal.
    std::vector<std::wstring> inline_stack;
    auto is_inline_fmt = [](const std::wstring& n) {
        return n == L"em" || n == L"strong" || n == L"del" || n == L"code" || n == L"a" ||
               n == L"sup" || n == L"s";
    };
    auto format_key = [&]() {
        std::vector<std::wstring> keys = inline_stack;
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        std::wstring k;
        for (const auto& s : keys) k += s + L";";
        return k;
    };
    std::wstring last_key;
    while (tz.Next(t)) {
        const bool in_code_block = pre > 0;
        if ((t.kind == HtmlToken::StartTag || t.kind == HtmlToken::EndTag) && !in_code_block &&
            is_inline_fmt(t.name)) {
            std::wstring entry = t.name;
            if (t.name == L"a") {
                if (const auto* h = t.Attr(L"href")) entry += L"[" + *h + L"]";
                if (const auto* ti = t.Attr(L"title")) entry += L"{" + *ti + L"}";
            }
            if (t.kind == HtmlToken::StartTag) {
                inline_stack.push_back(entry);
            } else {
                for (size_t i = inline_stack.size(); i-- > 0;) {
                    if (inline_stack[i].compare(0, t.name.size(), t.name) == 0 &&
                        (inline_stack[i].size() == t.name.size() ||
                         inline_stack[i][t.name.size()] == L'[' ||
                         inline_stack[i][t.name.size()] == L'{')) {
                        inline_stack.erase(inline_stack.begin() + static_cast<ptrdiff_t>(i));
                        break;
                    }
                }
            }
            continue;
        }
        if (t.kind == HtmlToken::Text && !in_code_block && t.text.find_first_not_of(L" \t\r\n") == std::wstring::npos &&
            (toks.empty() || toks.back().block)) {
            continue;  // whitespace after a block tag: trimmed anyway, and no formatting marker for it
        }
        if (t.kind == HtmlToken::Text && !in_code_block) {
            const std::wstring key = format_key();
            if (!key.empty() || !last_key.empty()) {
                if (toks.empty() || toks.back().kind != HtmlToken::Text || key != last_key) {
                    toks.push_back({HtmlToken::Text, L"\x2039" + key + L"\x203A", false, false});
                }
            }
            last_key = key;
        }
        if (t.kind == HtmlToken::Text) {
            std::wstring s;
            if (pre > 0) {
                s = t.text;
            } else {
                bool sp = false;
                for (wchar_t c : t.text) {
                    if (IsSpace(c)) { sp = true; continue; }
                    if (sp) s.push_back(L' ');
                    sp = false;
                    s.push_back(c);
                }
                if (sp) s.push_back(L' ');
            }
            if (!toks.empty() && toks.back().kind == HtmlToken::Text) toks.back().s += s;
            else toks.push_back({HtmlToken::Text, s, false, pre > 0});
        } else if (t.kind == HtmlToken::StartTag || t.kind == HtmlToken::EndTag) {
            std::wstring s = t.kind == HtmlToken::StartTag ? L"<" : L"</";
            s += t.name;
            auto attrs = t.attrs;
            std::sort(attrs.begin(), attrs.end(),
                      [](const HtmlAttr& a, const HtmlAttr& b) { return a.name < b.name; });
            for (const auto& a : attrs) s += L" " + a.name + L"=\"" + a.value + L"\"";
            s += L">";
            if (t.name == L"pre") pre += t.kind == HtmlToken::StartTag ? 1 : -1;
            toks.push_back({t.kind, s, IsBlockTag(t.name), pre > 0});
        }
    }
    std::wstring out;
    for (size_t i = 0; i < toks.size(); ++i) {
        Tok& k = toks[i];
        if (k.kind == HtmlToken::Text) {
            const bool prev_block = i == 0 || toks[i - 1].block;
            const bool next_block = i + 1 == toks.size() || toks[i + 1].block;
            std::wstring s = k.s;
            if (k.pre) {
                out += s;
                continue;
            }
            if (prev_block && !s.empty() && s.front() == L' ') s.erase(0, 1);
            if (next_block && !s.empty() && s.back() == L' ') s.pop_back();
            out += s;
        } else {
            out += k.s;
        }
    }
    // A paragraph without any text (e.g. "[](url)") is invisible either way.
    for (size_t p; (p = out.find(L"<p></p>")) != std::wstring::npos;) out.erase(p, 7);
    return WideToUtf8(out);
}

}  // namespace mdv::test

#include "core/html.h"

#include "core/util.h"

namespace mdv {
namespace {

// Same order as enum Tag (alphabetical), used for binary search.
constexpr std::wstring_view kTagNames[] = {
    L"a", L"abbr", L"acronym", L"address", L"article", L"aside", L"audio", L"b", L"bdi", L"bdo",
    L"big", L"blockquote", L"body", L"br", L"button", L"canvas", L"caption", L"center", L"cite",
    L"code", L"col", L"colgroup", L"dd", L"del", L"details", L"dfn", L"dir", L"div", L"dl", L"dt",
    L"em", L"embed", L"fieldset", L"figcaption", L"figure", L"font", L"footer", L"form", L"h1",
    L"h2", L"h3", L"h4", L"h5", L"h6", L"head", L"header", L"hgroup", L"hr", L"html", L"i",
    L"iframe", L"img", L"input", L"ins", L"kbd", L"label", L"legend", L"li", L"link", L"main",
    L"map", L"mark", L"marquee", L"math", L"menu", L"meta", L"nav", L"nobr", L"noembed",
    L"noframes", L"noscript", L"object", L"ol", L"optgroup", L"option", L"p", L"param",
    L"picture", L"plaintext", L"pre", L"q", L"rp", L"rt", L"ruby", L"s", L"samp", L"script",
    L"section", L"select", L"small", L"source", L"span", L"strike", L"strong", L"style", L"sub",
    L"summary", L"sup", L"svg", L"table", L"tbody", L"td", L"template", L"textarea", L"tfoot",
    L"th", L"thead", L"time", L"title", L"tr", L"track", L"tt", L"u", L"ul", L"var", L"video",
    L"wbr", L"xmp",
};
static_assert(std::size(kTagNames) == static_cast<size_t>(Tag::Count) - 1);

bool IsNameStart(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
bool IsNameChar(wchar_t c) {
    return IsNameStart(c) || (c >= L'0' && c <= L'9') || c == L'-' || c == L':' || c == L'_' ||
           c == L'.';
}

}  // namespace

Tag LookupTag(std::wstring_view name) {
    size_t lo = 0, hi = std::size(kTagNames);
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        const int c = name.compare(kTagNames[mid]);
        if (c == 0) return static_cast<Tag>(mid + 1);
        if (c < 0) hi = mid;
        else lo = mid + 1;
    }
    return Tag::Unknown;
}

bool IsRawTextTag(Tag t) {
    switch (t) {
    case Tag::Script: case Tag::Style: case Tag::Textarea: case Tag::Title: case Tag::Xmp:
    case Tag::Iframe: case Tag::Noembed: case Tag::Noframes: case Tag::Plaintext:
        return true;
    default:
        return false;
    }
}

const std::wstring* HtmlToken::Attr(std::wstring_view attr_name) const {
    for (const auto& a : attrs) {
        if (a.name == attr_name) return &a.value;
    }
    return nullptr;
}

void AppendHtmlDecoded(std::wstring& out, std::wstring_view t) {
    size_t i = 0;
    while (i < t.size()) {
        const size_t amp = t.find(L'&', i);
        if (amp == std::wstring_view::npos) {
            out.append(t.substr(i));
            return;
        }
        out.append(t.substr(i, amp - i));
        // Character reference: &name; &#123; &#x1F;
        size_t j = amp + 1;
        const size_t limit = std::min(t.size(), amp + 40);
        while (j < limit && (IsNameChar(t[j]) || t[j] == L'#')) ++j;
        if (j < t.size() && t[j] == L';' && j > amp + 1 &&
            AppendEntity(out, t.substr(amp, j - amp + 1))) {
            i = j + 1;
        } else {
            out.push_back(L'&');
            i = amp + 1;
        }
    }
}

bool HtmlTokenizer::Next(HtmlToken& tok) {
    tok.kind = HtmlToken::Text;
    tok.tag = Tag::Unknown;
    tok.self_closing = false;
    tok.name.clear();
    tok.text.clear();
    tok.attrs.clear();
    if (i_ >= s_.size()) return false;

    if (raw_tag_ != Tag::Unknown) {
        // Raw text up to "</name" (case-insensitive).
        size_t end = i_;
        for (;;) {
            end = s_.find(L"</", end);
            if (end == std::wstring_view::npos) { end = s_.size(); break; }
            if (StartsWithNoCase(s_.substr(end + 2), raw_name_)) break;
            end += 2;
        }
        tok.kind = HtmlToken::Text;
        if (raw_tag_ == Tag::Textarea || raw_tag_ == Tag::Title) {
            AppendHtmlDecoded(tok.text, s_.substr(i_, end - i_));
        } else {
            tok.text.assign(s_.substr(i_, end - i_));
        }
        tok.tag = raw_tag_;  // lets the consumer know where the text came from
        i_ = end;
        raw_tag_ = Tag::Unknown;
        raw_name_.clear();
        if (!tok.text.empty()) return true;
        return Next(tok);
    }

    if (s_[i_] == L'<' && ReadTag(tok)) {
        if (tok.kind == HtmlToken::StartTag && !tok.self_closing && IsRawTextTag(tok.tag)) {
            raw_tag_ = tok.tag;
            raw_name_ = tok.name;
        }
        return true;
    }
    ReadText(tok);
    return true;
}

void HtmlTokenizer::ReadText(HtmlToken& tok) {
    // Text runs to the next '<' that starts a plausible tag. A '<' that does
    // not (e.g. "a < b") is literal text.
    size_t j = i_;
    if (s_[j] == L'<') ++j;
    while (j < s_.size()) {
        if (s_[j] == L'<' && j + 1 < s_.size()) {
            const wchar_t n = s_[j + 1];
            if (IsNameStart(n) || n == L'/' || n == L'!' || n == L'?') break;
        }
        ++j;
    }
    tok.kind = HtmlToken::Text;
    AppendHtmlDecoded(tok.text, s_.substr(i_, j - i_));
    i_ = j;
}

bool HtmlTokenizer::ReadTag(HtmlToken& tok) {
    const size_t n = s_.size();
    size_t j = i_ + 1;
    if (j >= n) return false;

    if (s_.substr(i_, 4) == L"<!--") {
        const size_t end = s_.find(L"-->", i_ + 4);
        tok.kind = HtmlToken::Comment;
        i_ = (end == std::wstring_view::npos) ? n : end + 3;
        return true;
    }
    if (s_[j] == L'!' || s_[j] == L'?') {
        // <!DOCTYPE ...>, <![CDATA[...]]>, <?pi ...?>
        if (s_.substr(i_, 9) == L"<![CDATA[") {
            const size_t end = s_.find(L"]]>", i_ + 9);
            const size_t stop = (end == std::wstring_view::npos) ? n : end;
            tok.kind = HtmlToken::Text;
            tok.text.assign(s_.substr(i_ + 9, stop - (i_ + 9)));
            i_ = (end == std::wstring_view::npos) ? n : end + 3;
            return true;
        }
        const size_t end = s_.find(L'>', j);
        tok.kind = HtmlToken::Other;
        i_ = (end == std::wstring_view::npos) ? n : end + 1;
        return true;
    }

    const bool closing = s_[j] == L'/';
    if (closing) ++j;
    if (j >= n || !IsNameStart(s_[j])) return false;
    const size_t name_begin = j;
    while (j < n && IsNameChar(s_[j])) ++j;
    tok.name.reserve(j - name_begin);
    for (size_t k = name_begin; k < j; ++k) tok.name.push_back(AsciiLower(s_[k]));
    tok.tag = LookupTag(tok.name);
    tok.kind = closing ? HtmlToken::EndTag : HtmlToken::StartTag;

    // Attributes.
    for (;;) {
        while (j < n && IsSpace(s_[j])) ++j;
        if (j >= n) break;
        const wchar_t c = s_[j];
        if (c == L'>') { ++j; break; }
        if (c == L'/') {
            ++j;
            if (j < n && s_[j] == L'>') { tok.self_closing = true; ++j; break; }
            continue;
        }
        // Attribute name.
        const size_t an = j;
        while (j < n && !IsSpace(s_[j]) && s_[j] != L'=' && s_[j] != L'>' &&
               !(s_[j] == L'/' && j + 1 < n && s_[j + 1] == L'>')) {
            ++j;
        }
        if (j == an) { ++j; continue; }  // stray character, skip it
        HtmlAttr attr;
        attr.name.reserve(j - an);
        for (size_t k = an; k < j; ++k) attr.name.push_back(AsciiLower(s_[k]));
        size_t k = j;
        while (k < n && IsSpace(s_[k])) ++k;
        if (k < n && s_[k] == L'=') {
            ++k;
            while (k < n && IsSpace(s_[k])) ++k;
            if (k < n && (s_[k] == L'"' || s_[k] == L'\'')) {
                const wchar_t q = s_[k];
                const size_t vb = k + 1;
                size_t ve = s_.find(q, vb);
                if (ve == std::wstring_view::npos) ve = n;
                AppendHtmlDecoded(attr.value, s_.substr(vb, ve - vb));
                j = (ve < n) ? ve + 1 : n;
            } else {
                const size_t vb = k;
                while (k < n && !IsSpace(s_[k]) && s_[k] != L'>') ++k;
                AppendHtmlDecoded(attr.value, s_.substr(vb, k - vb));
                j = k;
            }
        }
        if (!closing) tok.attrs.push_back(std::move(attr));
    }
    i_ = j;
    return true;
}

}  // namespace mdv

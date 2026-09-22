// Minimal, forgiving HTML tokenizer for raw HTML embedded in Markdown.
// Only what is needed to interpret the HTML subset GitHub allows in READMEs.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mdv {

enum class Tag : uint16_t {
    Unknown = 0,
    A, Abbr, Acronym, Address, Article, Aside, Audio, B, Bdi, Bdo, Big, Blockquote, Body, Br,
    Button, Canvas, Caption, Center, Cite, Code, Col, Colgroup, Dd, Del, Details, Dfn, Dir, Div,
    Dl, Dt, Em, Embed, Fieldset, Figcaption, Figure, Font, Footer, Form, H1, H2, H3, H4, H5, H6,
    Head, Header, Hgroup, Hr, Html, I, Iframe, Img, Input, Ins, Kbd, Label, Legend, Li, Link,
    Main, Map, Mark, Marquee, Math, Menu, Meta, Nav, Nobr, Noembed, Noframes, Noscript, Object,
    Ol, Optgroup, Option, P, Param, Picture, Plaintext, Pre, Q, Rp, Rt, Ruby, S, Samp, Script,
    Section, Select, Small, Source, Span, Strike, Strong, Style, Sub, Summary, Sup, Svg, Table,
    Tbody, Td, Template, Textarea, Tfoot, Th, Thead, Time, Title, Tr, Track, Tt, U, Ul, Var,
    Video, Wbr, Xmp,
    Count
};

Tag LookupTag(std::wstring_view lowercase_name);

// Elements whose content is raw text up to the matching end tag.
bool IsRawTextTag(Tag t);

struct HtmlAttr {
    std::wstring name;   // lowercase
    std::wstring value;  // entity-decoded
};

struct HtmlToken {
    enum Kind : uint8_t { Text, StartTag, EndTag, Comment, Other };
    Kind kind = Text;
    Tag tag = Tag::Unknown;
    bool self_closing = false;
    std::wstring name;              // lowercase tag name
    std::wstring text;              // Text: entity-decoded content
    std::vector<HtmlAttr> attrs;

    const std::wstring* Attr(std::wstring_view attr_name) const;
    bool HasAttr(std::wstring_view attr_name) const { return Attr(attr_name) != nullptr; }
};

class HtmlTokenizer {
public:
    explicit HtmlTokenizer(std::wstring_view src) : s_(src) {}
    // Returns false at end of input.
    bool Next(HtmlToken& tok);

private:
    bool ReadTag(HtmlToken& tok);
    void ReadText(HtmlToken& tok);

    std::wstring_view s_;
    size_t i_ = 0;
    Tag raw_tag_ = Tag::Unknown;    // inside <script>, <style>, ...
    std::wstring raw_name_;
};

// Appends `text` to `out`, decoding character references.
void AppendHtmlDecoded(std::wstring& out, std::wstring_view text);

}  // namespace mdv

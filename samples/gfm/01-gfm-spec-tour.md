# GFM Spec Tour

This document walks through the [GitHub Flavored Markdown Spec](https://github.github.com/gfm/ "GFM spec 0.29-gfm") in the same order as the spec's chapters, with one section per chapter. Each section shows the construct rendered in place. Where the correct result isn't obvious, an *Expected:* note says what GitHub (cmark-gfm) renders, so a reviewer can check the viewer section by section. The table of contents below links to every section through GitHub-style anchors (lowercase, spaces become hyphens, punctuation is dropped). Click through them to test in-document navigation.

## Table of contents

- [Preliminaries](#preliminaries)
  - [Tabs](#tabs)
  - [Insecure characters](#insecure-characters)
- [Leaf blocks](#leaf-blocks)
  - [Thematic breaks](#thematic-breaks)
  - [ATX headings](#atx-headings)
  - [Setext headings](#setext-headings)
  - [Indented code blocks](#indented-code-blocks)
  - [Fenced code blocks](#fenced-code-blocks)
  - [HTML blocks](#html-blocks)
  - [Link reference definitions](#link-reference-definitions)
  - [Paragraphs](#paragraphs)
  - [Blank lines](#blank-lines)
  - [Tables](#tables)
- [Container blocks](#container-blocks)
  - [Block quotes](#block-quotes)
  - [List items](#list-items)
  - [Task list items](#task-list-items)
  - [Lists](#lists)
- [Inlines](#inlines)
  - [Backslash escapes](#backslash-escapes)
  - [Entity and numeric character references](#entity-and-numeric-character-references)
  - [Code spans](#code-spans)
  - [Emphasis and strong emphasis](#emphasis-and-strong-emphasis)
  - [Strikethrough](#strikethrough)
  - [Links](#links)
  - [Images](#images)
  - [Autolinks](#autolinks)
  - [Extended autolinks](#extended-autolinks)
  - [Raw HTML](#raw-html)
  - [Disallowed raw HTML](#disallowed-raw-html)
  - [Hard line breaks](#hard-line-breaks)
  - [Soft line breaks](#soft-line-breaks)
  - [Textual content](#textual-content)
- [Anchor edge cases](#anchor-edge-cases-äöü--日本語)

---

## Preliminaries

### Tabs

*Spec §2.2.* Tabs are not expanded to spaces, but when they define block structure they act like tab stops of 4 columns. Each code block below is indented with a single TAB character.

	foo	baz		bim

The next one uses two spaces and then a TAB, which still adds up to 4 columns:

  	foo	baz		bim

A list item whose continuation paragraph is indented with a TAB:

- foo

	bar (this paragraph belongs to the list item above)

Two TABs after `>`. The first TAB supplies the marker's optional space plus 3 columns and the second pushes past 4, so this is a code block inside the quote whose content starts with two spaces:

>		foo

### Insecure characters

*Spec §2.3.* For security, U+0000 has to be replaced with U+FFFD REPLACEMENT CHARACTER. The numeric reference `&#0;` must render as a replacement glyph here: &#0; (Expected: �).

---

## Leaf blocks

### Thematic breaks

*Spec §4.1.* Each of the following produces a horizontal rule (`<hr>`). Labels are paragraphs *between* the rules.

Three asterisks `***`:

***

Three hyphens `---`:

---

Three underscores `___`:

___

Spaced asterisks ` **  * ** * ** * **`:

 **  * ** * ** * **

Spaced hyphens `-     -      -      -`:

-     -      -      -

A long line of underscores:

_____________________________________

Up to 3 spaces of indentation are allowed (`   ***`):

   ***

**Not** thematic breaks (each must render as plain text):

+++

===

--

**

    *** (4 spaces of indent: this is an indented code block, not a rule)

_ _ _ _ a (letters are not allowed)

A thematic break can interrupt a paragraph:
***
and a `---` directly under a line of text makes a setext heading instead (see [Setext headings](#setext-headings)).

### ATX headings

*Spec §4.2.* All six levels follow. The demo headings are real headings, so they also show up in the document outline.

# Heading level 1
## Heading level 2
### Heading level 3
#### Heading level 4
##### Heading level 5
###### Heading level 6

####### Seven hashes is not a heading (plain paragraph)

#5 bolt (no space after `#`: paragraph)

#hashtag (paragraph)

\## Escaped hash: paragraph that starts with two literal hashes

# Inline *emphasis*, `code` and \*escaped stars\* in a heading

## Closing sequence is stripped ##

### Long closing sequence is stripped too ###################

### Closing sequence must be preceded by a space ### b

# Trailing hash without space stays: C#

### Escaped closing hashes stay: foo \###

   ### Up to three spaces of indentation is OK

Empty headings follow (an `h2`, an `h1` and an `h3`, each with no text):

## 
#
### ###

### Setext headings

*Spec §4.3.*

Setext heading level 1
======================

Setext heading level 2
----------------------

A *multi-line* setext
heading with **inline** content
===

Underline of a single `=` character
=

  Indented up to three spaces
  ---

Not a heading, because the underline is indented four spaces:
    ---

> A setext underline cannot be a lazy continuation line, so this is a quote
---

A setext underline can't contain internal spaces, so the `= =` below just continues this paragraph.
= =

### Indented code blocks

*Spec §4.4.* Four spaces of indentation give a code block, and inside it markdown is shown literally:

    a simple
      indented code block
    <div>*not emphasized*</div>
    - not a list

Blank lines inside an indented block are preserved:

    chunk1

    chunk2



    chunk3

An indented code block cannot interrupt a paragraph, so this line
    and this one (indented four spaces) are just one paragraph.

### Fenced code blocks

*Spec §4.5.*

Backtick fence:

```
<
 >
```

Tilde fence:

~~~
<
 >
~~~

Info string (language) with backticks:

```ruby
def greet(name)
  puts "Hello, #{name}!"
end
```

Info string with extra words and a longer tilde closing fence (the language is the first word, `ruby`):

~~~~    ruby startline=3 $%@#$
def foo(x)
  return 3
end
~~~~~~~

A four-backtick fence can contain a three-backtick fence as content:

````markdown
Here is some Markdown with a nested fence:

```js
console.log("nested");
```
````

A tilde fence can contain backtick lines, and the reverse also works:

~~~
```
still inside the tilde block
```
~~~

```
~~~
still inside the backtick block
~~~
```

The fence's own indentation is removed from the content lines (fence indented 2 spaces; expected content: `aaa`, `  aaa`, `aaa`):

  ```
  aaa
    aaa
  aaa
  ```

An empty fenced code block:

```
```

A fence with a single blank line:

```

```

A backtick info string cannot contain backticks, so this is an *inline code span*, not a fence:

``` aa ```
foo

A closing fence may be indented up to 3 spaces, and it must be at least as long as the opening one:

````
aaa
```
still code (the three-backtick line above did not close the block)
   ````

### HTML blocks

*Spec §4.6.* There are seven kinds of HTML block, each with its own start and end conditions.

**Kind 1** is `<pre>`, `<script>`, `<style>` (and `<textarea>`). It ends at the matching closing tag, blank lines are allowed inside, and markdown is not processed:

<pre>
**not bold** inside a raw pre block

  * not a list, spacing preserved
</pre>

**Kind 2** is an HTML comment. Nothing should be visible between these two lines.

<!-- This comment is hidden.

It spans several lines, including a blank one. -->

**Kind 3** is a processing instruction (hidden or shown literally, depending on the sanitizer):

<?php echo 'processing instruction'; ?>

**Kind 4** is a declaration:

<!DOCTYPE html>

**Kind 5** is CDATA:

<![CDATA[
function matchwo(a,b) { if (a < b && a < 0) then { return 1; } }
]]>

**Kind 6** is a block-level tag such as `div`, `table`, `details` or `p`. It ends at a blank line, so markdown **inside** it is only processed when blank lines surround it:

<div>
*This is NOT emphasized*, because the text is inside the same HTML block as the div.
</div>

<div>

*This IS emphasized*, because blank lines separate it from the `<div>` tags.

</div>

<table><tr><td>
<pre>
**Raw table cell**, *not* markdown
</pre>
</td></tr></table>

**Kind 7** is any other complete open or close tag on a line by itself. It can't interrupt a paragraph.

<del>

*This paragraph is inside `<del>` and emphasized*

</del>

<span title="inline, not a block">A tag followed by more text on the same line doesn't start a kind-7 block. It is inline HTML in an ordinary paragraph, so this *emphasis* IS processed.</span>

Kind 7 cannot interrupt a paragraph, so the tag lines below are inline HTML inside this paragraph:
<span title="hover">
*emphasis still works here*
</span>

### Link reference definitions

*Spec §4.7.* Definitions produce no output of their own. The links below them must resolve.

[foo]: /url "title"
[Case Insensitive]: https://github.github.com/gfm/#link-reference-definitions 'single-quoted title'
[with spaces]: <https://example.com/my url with spaces> (parenthesized title)
[multi-line title]: https://example.com/multi "
title spanning
several lines
"
[ẞ]: https://en.wikipedia.org/wiki/%C3%9F
  [indented-def]:
  https://example.com/indented
  'title on its own line'

- Simple: [foo] (hover for the tooltip "title")
- Case-insensitive match: [CASE INSENSITIVE] and [case insensitive]
- Angle-bracket destination with spaces: [with spaces]
- Multi-line title: [multi-line title]
- Unicode case folding, where `[SS]` matches the definition `[ẞ]`: [SS]
- Definition split over lines: [indented-def]
- Undefined reference stays literal text: [not defined]

[not a definition]: /url "title" trailing text makes this a paragraph

> [quoted-def]: https://example.com/from-a-quote
>
> A definition inside a block quote works document-wide: [quoted-def]

### Paragraphs

*Spec §4.8.*

A paragraph is a sequence of non-blank lines. The lines of this paragraph are
joined with soft line breaks, which the viewer should render as spaces and
wrap to fit the window width.

   Leading spaces (up to three) are skipped: this paragraph is not indented.

Leading spaces on
         continuation lines are skipped too.

A second paragraph starts after a blank line. Final spaces on the last line are stripped.     

### Blank lines

*Spec §4.9.* Blank lines between blocks are ignored except to separate blocks. Five blank lines follow this paragraph, and the next paragraph should sit at normal paragraph spacing, without a bigger gap.





There were five blank lines above this paragraph.

### Tables

*Spec §4.10 (extension).*

Column alignment (left, center, right, default):

| Left aligned | Centered | Right aligned | Default |
| :----------- | :------: | ------------: | ------- |
| apple        | banana   |          1.00 | x       |
| a much longer cell value | c | 1,234.56 | yy |
| z            | centered text here | 0.5 | zzz |

Escaped pipes inside cells, including inside code spans (expected: the pipe shows in the cell, and the cell is not split):

| Expression | Meaning |
| ---------- | ------- |
| `a \| b`   | bitwise OR in code |
| x \| y     | literal pipe in text |
| `\|\|`     | logical OR |

Inline formatting in cells:

| Style | Example |
|-------|---------|
| Bold | **important** |
| Italic | *emphasis* |
| Strikethrough | ~~removed~~ |
| Code | `printf("%d\n", x);` |
| Link | [GitHub](https://github.com) |
| Image | ![avatar](../avatar.png) |
| Mixed | ***bold italic*** with `code` and a [link](#tables) |

Leading and trailing pipes are optional, and the cells don't have to line up:

Name | Value
--- | ---
alpha | 1
beta | 2

Rows with fewer cells are padded with empty cells, and extra cells are ignored:

| a | b | c |
| --- | --- | --- |
| only one |
| one | two |
| one | two | three | FOUR (ignored) | FIVE (ignored) |

A header with no body rows:

| Header only | Second column |
| ----------- | ------------- |

The delimiter row must match the header's cell count, so this is **not** a table (it renders as a paragraph):

| abc | def |
| --- |
| bar |

A table ends at the first blank line or at the start of another block. In the next example `> quote` starts a block quote:

| abc | def |
| --- | --- |
| bar | baz |
> quote after the table

A line without pipes right after a table becomes a **row** (expected: an extra body row whose first cell is "continued row"), and the blank line then ends the table:

| abc | def |
| --- | --- |
| bar | baz |
continued row

This paragraph comes after the blank line.

---

## Container blocks

### Block quotes

*Spec §5.1.*

> # Heading inside a quote
> A paragraph inside a quote with **bold** and `code`.
>
> - a list
> - inside the quote
>
> ```python
> print("code inside a quote")
> ```

Lazy continuation. The second line has no `>` marker, but it still continues the paragraph (expected: a single paragraph "bar baz foo"):

> bar
baz
> foo

Laziness does not apply to lists, so the second item here is **outside** the quote:

> - item inside quote
- item outside quote

Nested quotes:

> Level 1
>> Level 2
> > > Level 3 (markers can be separated by spaces)
> > > > Level 4

A blank line separates two different quotes:

> First quote

> Second quote

An empty block quote:

>

A quote holding an indented code block (5 spaces after `>`: one belongs to the marker, four indent the code):

>     code in a quote

### List items

*Spec §5.2.*

Bullet list markers `-`, `+`, `*`. Changing the marker starts a new list:

- dash item one
- dash item two
+ plus item one
+ plus item two
* star item one
* star item two

Ordered lists with `.` and `)` delimiters. Changing the delimiter starts a new list:

1. first (period)
2. second (period)
3) third (paren, which starts a new list numbered 3)
4) fourth (paren)

Unusual start numbers. A paragraph separates each list, because a blank line alone would not end the previous one.

7. starts at seven
8. eight

Starting at zero:

0. zero
1. one

Nine digits, the most a list number may have (check that the wide marker still fits):

123456789. nine digits
123456790. next

1234567890. ten digits is not a list item (plain paragraph)

-1. A negative number is not a list item either.

Only a list that starts at `1` can interrupt a paragraph. Expected: one single paragraph here.
The number of windows in my house is
14.  The number of doors is 6.

A list item can start with a blank line (expected: three items holding a paragraph, a fenced code block and an indented code block):

-
  foo
-
  ```
  bar
  ```
-
      baz

Content indentation is defined by the width of the marker. The continuation paragraph here lines up with the text after `1.  ` (4 columns), so it belongs to the item:

1.  A paragraph
    with two lines.

        indented code inside the item

    > A block quote inside the item.

Not enough indentation, so the second line starts a new paragraph outside the list:

- one

 two

Code inside list items:

1. Install the package:

   ```bash
   npm install --save fastgrep
   ```

2. Configure it:

   ```json
   { "ignore": ["node_modules", ".git"] }
   ```

3. Run it (the next line is an indented code block inside the item, 3+4 = 7 spaces):

       fastgrep --stats "TODO" src/

Nested four or more levels deep:

- Level 1
  - Level 2
    - Level 3
      - Level 4
        - Level 5
          - Level 6
    - Back to level 3
- Back to level 1

1. Ordered level 1
   1. Ordered level 2
      - Bullet level 3
        1. Ordered level 4
           - [ ] Task level 5

Tight vs loose. The first list is tight: items are not wrapped in `<p>` and have no extra spacing.

- tight a
- tight b
- tight c

The second list is loose, because blank lines between items put every item in a `<p>`:

- loose a

- loose b

- loose c

A single blank line *inside* one item also makes the whole list loose:

- a
- b

  c
- d

### Task list items

*Spec §5.3 (extension).*

- [ ] unchecked task
- [x] checked task
- [X] checked with uppercase X
- [ ] task with **bold**, `code` and a [link](#task-list-items)
- [x] ~~done and struck through~~
  - [ ] nested unchecked
  - [x] nested checked
    - [ ] third-level task
- regular item (no checkbox) mixed into the task list
- [ ]not a task: there's no space after the brackets
- [  ] not a task: two spaces inside the brackets

1. [x] ordered task one
2. [ ] ordered task two

A loose task list:

- [x] Write the parser

  With a second paragraph in the same item.

- [ ] Write the renderer

### Lists

*Spec §5.4.* Two lists of the same type have to be separated by something that isn't a list item. An HTML comment works:

- foo
- bar

<!-- -->

- baz
- bim

A list that directly follows a paragraph (no blank line) is still a list, because bullet lists can interrupt a paragraph:
- interrupting item one
- interrupting item two

An empty list item can't interrupt a paragraph, so the lone `*` on the next line
*
is plain text inside this paragraph. (A lone `-` would not work for this demo: it would turn the line above into a setext heading.)

---

## Inlines

### Backslash escapes

*Spec §6.1.* Any ASCII punctuation character can be backslash-escaped. Expected: the whole line of punctuation shows, with no backslashes.

\!\"\#\$\%\&\'\(\)\*\+\,\-\.\/\:\;\<\=\>\?\@\[\\\]\^\_\`\{\|\}\~

Before any other character the backslash is literal: \→\A\a\ \3\φ\«

Escaped markers lose their special meaning:

\*not emphasized*
\<br/> not a tag
\[not a link](/foo)
\`not code`
1\. not a list
\* not a list
\# not a heading
\[foo]: /url "not a reference"
\&ouml; not a character reference

Escapes don't work in code spans or code blocks: `\[\]` and

    \[\]

In URLs and titles they do: [escaped link](https://example.com/bar\* "ti\*tle") (expected URL `https://example.com/bar*`, title `ti*tle`).

A backslash at the end of a line is a hard line break\
(see [Hard line breaks](#hard-line-breaks)).

### Entity and numeric character references

*Spec §6.2.*

Named: &nbsp;(nbsp) &amp; &copy; &AElig; &Dcaron; &frac34; &HilbertSpace; &DifferentialD; &ClockwiseContourIntegral; &ngE;

Decimal: &#35; &#1234; &#992; &#0;

Hexadecimal: &#X22; &#XD06; &#xcab; &#x1F600; &#x1f680;

Expected for the line above: `" ആ ಫ 😀 🚀`

Not references, so these stay literal: &nbsp &x; &#; &#x; &#abcdef0; &ThisIsNotDefined; &hi?;

Implementations disagree on this one: `&#87654321;` has 8 digits and the spec allows at most 7, so by the spec it stays literal, but cmark-gfm turns it into �. Here it is: &#87654321;

In code spans they are not decoded: `f&ouml;&ouml;`

In link destinations and titles they are: [link](https://example.com/f&ouml;&ouml; "f&ouml;&ouml;")

A reference can't stand in for a structural character: &#42;not emphasized&#42; and

&#42; not a list item

A non-breaking space keeps "10&nbsp;km" and "Windows&nbsp;11" together when the line wraps.

### Code spans

*Spec §6.3.*

- Simple: `foo`
- Double backticks around a single backtick: `` foo ` bar ``
- Only a backtick pair: ` `` ` (expected content: two backticks)
- One stripped space on each side: `  ``  ` (expected content: space, two backticks, space)
- Spaces stripped only when present on both sides: ` a` (expected: " a" with the leading space kept)
- Interior spaces are not collapsed: `foo   bar`
- A line ending becomes a space: `foo
bar`
- A backslash is literal inside code: `foo\`bar` (expected: a code span holding foo and a backslash, then plain text bar with a literal backtick)
- Longer delimiters: ``foo`bar`` and ` foo `` bar `
- Code binds tighter than emphasis: *foo`*` (expected: literal `*foo` and then code `*`)
- Code vs HTML: `<a href="`">` (expected: a code span showing an unfinished anchor tag up to the opening quote, then a plain quote and greater-than sign, then a literal backtick)
- Unmatched backtick strings stay literal: ```foo`` and `foo
- HTML special characters in code: `<script>alert("x & y")</script>`

### Emphasis and strong emphasis

*Spec §6.4.* The cases below are grouped by rule. Unless a line says otherwise, the markup renders.

Basics:

- *single asterisks* and _single underscores_ give emphasis
- **double asterisks** and __double underscores__ give strong
- ***triple asterisks*** give strong emphasis (expected: `<em><strong>`)
- ___triple underscores___ do the same

Nesting:

- **bold with *italic* inside**
- *italic with **bold** inside*
- ***strong** in emph* (expected: emphasis containing strong "strong")
- ***emph* in strong** (expected: strong containing emphasis "emph")
- **foo "*bar*" foo**
- *foo **bar** baz*
- *foo**bar**baz* (expected: em containing a nested strong "bar")
- foo***bar***baz (expected: strong emphasis on "bar")
- *(*foo*)* (expected: nested em with parentheses)
- **Gomphocarpus (*Gomphocarpus physocarpus*, syn. *Asclepias physocarpa*)**
- *emphasis with a [link](#emphasis-and-strong-emphasis) inside*

Intraword `*` works, intraword `_` does not:

- foo*bar*baz (em "bar")
- 5*6*78 (em "6")
- foo**bar**baz (strong "bar")
- foo_bar_baz (literal underscores)
- snake_case_identifier_name (literal)
- __init__ at word start and end works: __init__ (strong "init")
- пристаням_стремятся_ (literal, Unicode letters count as word characters)

Flanking rules. These are **not** emphasis:

- a * foo bar* (opening `*` followed by whitespace)
- a*"foo"* (`*` preceded by alphanumeric and followed by punctuation)
- \* a \* (escaped)
- * a * (the `* ` at the start makes this a nested bullet item)
- _foo bar _ (closing `_` preceded by whitespace)
- ** is not strong **

Mismatched delimiters:

- **foo* (expected: literal `*` then em "foo")
- *foo** (expected: em "foo" then literal `*`)
- *foo _bar* baz_ (expected: `foo _bar` in italics, then plain ` baz_`)
- __foo, __bar__, baz__ (expected: "foo, bar, baz" all bold. The AST nests two strong nodes, but cmark-gfm's HTML output collapses them.)

Precedence against links, code and HTML:

- *[bar*](https://example.com) (expected: a literal asterisk, then a link whose text is `bar*`)
- *a `*`* (expected: em containing code `*`)
- **a<https://foo.bar/?q=**> (expected: literal `**a` then an autolink)

The rule of 3:

- *foo**bar* (expected: em "foo**bar")
- foo******bar*********baz (expected: bold `bar`, then literal `***baz`. The spec nests three strong elements, cmark-gfm prints one.)

### Strikethrough

*Spec §6.5 (extension).*

- Two tildes: ~~Hi~~ Hello, world!
- One tilde (GitHub.com renders this too): ~one~
- Intraword: foo~~bar~~baz
- With nested formatting: ~~**bold** and *italic* and `code` struck through~~
- Mismatched lengths don't pair: ~~foo~ (literal)
- Three tildes are not strikethrough: ~~~three~~~ (literal)

Strikethrough cannot span paragraphs (both lines below stay literal):

This ~~has a

new paragraph~~.

### Links

*Spec §6.6.* Hover the links to check that titles show up as tooltips.

Inline links:

- [link](https://example.com)
- [link with double-quoted title](https://example.com "Title here")
- [link with single-quoted title](https://example.com 'Single quotes')
- [link with parenthesized title](https://example.com (Parens))
- [empty destination]()
- [empty angle destination](<>)
- [angle destination with spaces](<https://example.com/my page.html>)
- [balanced nested parens](https://en.wikipedia.org/wiki/Markdown_(disambiguation))
- [escaped parens](https://example.com/foo\)\:)
- [query and fragment](https://example.com/search?q=markdown&sort=desc#results)
- [link *with* **inline** `markup` ~~inside~~](https://example.com)
- [no space allowed] (https://example.com) (expected: literal brackets and parentheses, though the extended-autolink extension still links the bare URL)
- [links [can't] contain [other](https://a.example) links](https://b.example) (expected: "other" is a link, the outer brackets stay literal, and the bare outer URL is autolinked)
- [foo *bar](https://example.com/baz*) (expected: link text "foo *bar")

Reference links, using definitions at the bottom of this section:

- Full reference: [GFM spec][gfm-spec]
- Case-insensitive label: [GFM spec][GFM-SPEC]
- Collapsed reference: [CommonMark][]
- Shortcut reference: [CommonMark]
- Shortcut with markup: [*emphasized shortcut*]
- Reference whose destination has a title: [md4c][]

[gfm-spec]: https://github.github.com/gfm/ "GitHub Flavored Markdown Spec"
[commonmark]: https://commonmark.org
[*emphasized shortcut*]: https://example.com/emph
[md4c]: https://github.com/mity/md4c 'md4c: Markdown parser for C'

Relative links to files:

- [Sibling file: README.md](README.md)
- [Sibling file with an anchor: 05-tables.md#very-wide-table](05-tables.md#very-wide-table)
- [Parent-directory file: ../sample-readme.md](../sample-readme.md)
- [Image file as link target](img/photo.jpg)
- [Link with %20 encoding](img/%D0%BA%D0%B0%D1%80%D1%82%D0%B8%D0%BD%D0%BA%D0%B0%20%D1%81%20%D0%BF%D1%80%D0%BE%D0%B1%D0%B5%D0%BB%D0%B0%D0%BC%D0%B8.png)
- [Missing file](does-not-exist.md)

Links to anchors in this document:

- [Jump to Tables](#tables)
- [Jump to Hard line breaks](#hard-line-breaks)
- [Jump to the ATX demo "Heading level 4"](#heading-level-4)
- [Jump to the setext demo](#setext-heading-level-2)
- [Back to top](#gfm-spec-tour)
- [Broken anchor, no such heading](#this-anchor-does-not-exist)

Links with images inside:

- [![logo](../logo.png)](https://github.com/mity/md4c) (the logo is a link)
- [![avatar](../avatar.png "Avatar title") Avatar and text inside one link](#images)

### Images

*Spec §6.7.*

Inline image with a title:

![Project logo](../logo.png "The project logo (title tooltip)")

An image with *emphasis* in its alt text (the alt text is "an image with emphasis"):

![an image *with* emphasis](../avatar.png)

Reference-style images (full, collapsed, shortcut):

![Architecture][diagram-ref]

![diagram-ref][]

![diagram-ref]

[diagram-ref]: ../diagram.png "Architecture diagram"

An angle-bracket destination with spaces and Cyrillic characters:

![Cyrillic file name](<img/картинка с пробелами.png>)

A remote image (only loads if network images are supported):

![Octocat](https://github.githubassets.com/images/modules/logos_page/Octocat.png)

A missing image (expected: alt text or a broken-image placeholder):

![This image does not exist](img/missing.png)

Inline images in running text: some text ![](../avatar.png) more text ![tiny](img/viewbox-only.svg) end.

### Autolinks

*Spec §6.8.* Angle-bracket autolinks:

- <https://github.github.com/gfm/>
- <http://foo.bar.baz/test?q=hello&id=22&boolean>
- <irc://foo.bar:2233/baz>
- <MAILTO:FOO@BAR.BAZ>
- <mailto:someone@example.com>
- <a+b+c:d> (made-up scheme)
- <localhost:5001/foo> (the scheme is "localhost")
- <foo@bar.example.com> (email autolink, becomes `mailto:`)
- <foo+special@Bar.baz-bar0.com>

Not autolinks (rendered literally):

- <https://foo.bar/baz bim> (a space isn't allowed, so the angle brackets stay literal, though the extended-autolink extension still links the URL part)
- <foo.bar.baz> (no scheme)
- <m:abc> (scheme too short)
- <foo\+@bar.example.com> (a backslash isn't allowed in an email autolink, but the extension then links the unescaped address)

### Extended autolinks

*Spec §6.9 (extension).* These are linked without angle brackets:

- www.commonmark.org
- Visit www.commonmark.org/help for more information.
- Visit www.commonmark.org. (the trailing period is not part of the link)
- Visit www.commonmark.org/a.b. (inner dots are kept, the final one is not)
- www.google.com/search?q=Markup+(business) (balanced parentheses kept)
- www.google.com/search?q=Markup+(business))) (unbalanced trailing `))` excluded)
- (www.google.com/search?q=Markup+(business)) (the outer parentheses are not part of the link)
- (www.google.com/search?q=Markup+(business)
- www.google.com/search?q=(business))+ok (parentheses in the middle are kept)
- www.google.com/search?q=commonmark&hl=en
- www.google.com/search?q=commonmark&hl; (trailing entity-like `&hl;` excluded)
- www.commonmark.org/he<lp (a `<` ends the link)
- https://example.com/path?query=1&x=2
- http://commonmark.org
- (Visit https://encrypted.google.com/search?q=Markup+(business))
- Anonymous FTP is available at ftp://foo.bar.baz.
- https://example.com/trailing-punctuation?! (the `?!` is excluded)
- "https://example.com/in-quotes" (the quotes are excluded)
- Emails: foo@bar.baz and hello+xyz@mail.example (but hello@mail+xyz.example is not linked)
- a.b-c_d@a.b links, a.b-c_d@a.b. excludes the final dot, and a.b-c_d@a.b- and a.b-c_d@a.b_ are not linked at all
- Protocol prefixes: mailto:foo@bar.baz and xmpp:foo@bar.baz/txt

Extended autolinks don't fire inside code spans or existing links: `www.example.com` and [www.example.com](https://example.org).

### Raw HTML

*Spec §6.10.* Inline HTML that GitHub allows:

- Keyboard keys: <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>P</kbd>
- <b>b</b>, <i>i</i>, <em>em</em>, <strong>strong</strong>, <code>code</code>, <s>s</s>, <del>del</del>, <ins>ins</ins>, <u>u</u>, <mark>mark</mark>
- H<sub>2</sub>O and E = mc<sup>2</sup>
- Line<br>break with `<br>`, and another with `<br/>`:<br/>here
- <a href="https://example.com" title="HTML anchor title">An HTML link</a>
- <img src="../avatar.png" width="24" height="24" alt="inline avatar"> inline `<img>` with width/height
- <span title="hover me">span with a title attribute</span>
- <abbr title="GitHub Flavored Markdown">GFM</abbr> abbreviation
- Custom element: <responsive-image src="foo.jpg" />
- Inline comment (hidden): before<!-- hidden inline comment -->after
- Attributes spread over lines: <a href="https://example.com"
  title="multi-line tag">link</a>

Not HTML, so these are rendered literally:

- <33> <__>
- <a h*#ref="hi">
- </a href="foo">
- a < b > c
- 5 < 6 and 7 > 4

### Disallowed raw HTML

*Spec §6.11 (extension, the "tagfilter").* The tags `<title>`, `<textarea>`, `<style>`, `<xmp>`, `<iframe>`, `<noembed>`, `<noframes>`, `<script>` and `<plaintext>` are filtered: the `<` is escaped, so each one shows as **literal text** (github.com goes further and removes them). None of them may execute, change the page style, or swallow the text that follows.

<script>alert("If you see an alert box, the tag filter failed!");</script>

<style>body { background: #f00 !important; } p { color: #f00 !important; }</style>

<iframe src="https://example.com" width="400" height="100"></iframe>

<textarea rows="2" cols="40">A textarea should not be rendered as a control.</textarea>

<title>Injected document title</title>

<xmp>xmp content with **markdown** and <b>html</b> inside</xmp>

<noembed>noembed content</noembed>

<noframes>noframes content</noframes>

Inline: <strong>allowed strong</strong> next to <title>filtered title</title> and <em>allowed em</em>.

Last one: <plaintext> in a browser, an unfiltered `plaintext` tag would turn **everything** after it into plain text.

If this sentence renders with normal formatting (**bold** and *italic* working), none of the tags above swallowed the rest of the document.

### Hard line breaks

*Spec §6.12.*

This line ends with two spaces  
so this text starts on a new line.

This line ends with a backslash\
so this text starts on a new line too.

More than two trailing spaces also work.      
Leading spaces on the next line are ignored.

Inside emphasis: *line one  
line two* and **bold\
across the break**.

A trailing backslash at the **end of a paragraph** is shown literally\

A hard break doesn't work inside a code span: `code  
span` (expected: no break, the line ending becomes a space)

An HTML break:<br>after an explicit `<br>`.

### Soft line breaks

*Spec §6.13.*

These three lines
are joined by soft breaks,
and should render as one flowing paragraph separated by spaces.

This line ends with exactly one trailing space, which is not enough for a hard break, 
so this text continues on the same visual line.

### Textual content

*Spec §6.14.* Any characters not given a meaning above are literal text:

hello $.;'there

Foo χρῆν

Multiple     spaces     inside     text (HTML collapses them, and a viewer matching GitHub should too)

Characters with no markup meaning: # (mid-line), + - = | ~ ^ { } %, and a lone backtick ` in text.

---

## Anchor edge cases äöü — 日本語

GitHub keeps Unicode letters when it builds an anchor, drops punctuation, and turns each space into a hyphen. This heading's anchor is `#anchor-edge-cases-äöü--日本語` (two hyphens, because the em dash is dropped but both surrounding spaces become hyphens). The TOC at the top links here.

Duplicate headings get numeric suffixes:

#### Duplicate

#### Duplicate

#### Duplicate

- [First "Duplicate"](#duplicate)
- [Second "Duplicate"](#duplicate-1)
- [Third "Duplicate"](#duplicate-2)
- [Back to top](#gfm-spec-tour)

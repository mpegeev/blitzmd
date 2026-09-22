# Lists and Block Quotes

This file tests list and block-quote layout. It covers nesting six levels deep with bullets, numbers and tasks mixed, tight versus loose spacing, list items that contain paragraphs, code, quotes, tables, images and headings, ordered lists starting at 0, 7, 99 and 1000, a 150-item list (the markers must stay right-aligned as the numbers grow from one to three digits), block quotes nested five levels deep with rich content, and lazy continuation lines. On github.com, nested bullets cycle through disc, circle and square, and nested ordered lists use decimal, then lower-roman, then lower-alpha.

## Contents

- [Deep nesting](#deep-nesting)
- [Tight and loose lists](#tight-and-loose-lists)
- [Rich list items](#rich-list-items)
- [Unusual start numbers](#unusual-start-numbers)
- [Wrapping and hanging indents](#wrapping-and-hanging-indents)
- [Block quotes](#block-quotes)
- [Lazy continuation](#lazy-continuation)
- [150-item list](#150-item-list)

---

## Deep nesting

### Bullets, six levels

- Level 1: disc
  - Level 2: circle
    - Level 3: square
      - Level 4: square (GitHub keeps using square from here)
        - Level 5
          - Level 6: the deepest level. This text is long enough to wrap, and the wrapped lines must stay aligned with the text of this item, not with its marker.
        - Back to level 5
      - Back to level 4
    - Back to level 3
  - Back to level 2
- Back to level 1

### Ordered, six levels

1. Level 1 (decimal)
   1. Level 2 (lower-roman on github.com: i, ii, iii)
   2. Second item at level 2
      1. Level 3 (lower-alpha: a, b, c)
      2. Second item at level 3
         1. Level 4
            1. Level 5
               1. Level 6
               2. Level 6, item 2
            2. Level 5, item 2
         2. Level 4, item 2
   3. Third item at level 2
2. Level 1, item 2

### Mixed bullets, numbers and tasks

1. Plan the release
   - [x] Freeze the feature branch
   - [x] Update the changelog
     1. Collect merged PRs
     2. Group them by label
        - `feat:` → **Features**
        - `fix:` → **Bug fixes**
          - [ ] Double-check the security fixes
            1. CVE numbers
            2. Credits
   - [ ] Tag the release
2. Build
   * x64
   * x86
   * ARM64
     + cross-compiled
     + not tested on real hardware :warning:
3. Publish
   - [ ] GitHub release
   - [ ] Total Commander forum post

### Nested task lists

- [x] Parser
  - [x] Block structure
  - [x] Inline structure
    - [x] Emphasis
    - [ ] Links with nested brackets
- [ ] Renderer
  - [x] Paragraphs and headings
  - [ ] Tables
    - [ ] Column width algorithm
    - [ ] Horizontal scrolling
  - [ ] Images
- [ ] Documentation

---

## Tight and loose lists

A **tight** list: no blank lines between items, so the items sit close together.

- apples
- oranges
- pears

A **loose** list: blank lines between items, so each item is a paragraph with paragraph spacing.

- apples

- oranges

- pears

**Loose** because one item has two paragraphs. Every item in this list gets paragraph spacing, not only the one with the blank line:

1. First item.
2. Second item.

   Its second paragraph.
3. Third item.

A tight outer list with a loose inner list:

- outer 1
  - inner a

  - inner b
- outer 2
- outer 3

A loose outer list with a tight inner list:

- outer 1

  - inner a
  - inner b

- outer 2

---

## Rich list items

1. **An item with several paragraphs.**

   This second paragraph belongs to item 1. It's indented to the content column (3 spaces for `1. `).

   A third paragraph, with *emphasis* and a [link](#rich-list-items).

2. **An item with a fenced code block:**

   ```python
   def fib(n: int) -> int:
       return n if n < 2 else fib(n - 1) + fib(n - 2)
   ```

3. **An item with an indented code block** (content column + 4 spaces):

       $ make && make test

4. **An item with a block quote:**

   > Quoted text inside a list item.
   > — Someone famous

5. **An item with a table:**

   | Key | Value |
   |-----|-------|
   | a   | 1     |
   | b   | 2     |

6. **An item with an image:**

   ![Logo inside a list item](../logo.png)

7. **An item with a heading:**

   #### A heading inside a list item

   Text after the heading.

8. **An item with a horizontal rule:**

   ---

   Text after the rule.

9. **An item with a nested alert:**

   > [!NOTE]
   > An alert inside a list item (github.com may render it as a plain quote).

10. **The tenth item.** The marker is now two digits wide, and the text must still line up with items 1–9. (github.com right-aligns the numbers.)

- ```
  A list item that *starts* with a fenced code block.
  ```
- > A list item that starts with a block quote.
- # A list item that starts with a heading
- ![](../avatar.png) A list item that starts with an image.
- | table | in first line |
  |-------|---------------|
  | a     | b             |

Empty list items:

-
- non-empty
-

1.
2. non-empty
3.

---

## Unusual start numbers

Each list is separated from the next by a paragraph, because a blank line alone wouldn't end the list.

Starts at **0**:

0. zero
1. one
2. two

Starts at **7**, with the `)` delimiter:

7) seven
8) eight
9) nine
10) ten: the marker width changes here

Starts at **99**:

99. ninety-nine
100. one hundred: the marker width changes here
101. one hundred one

Starts at **1000**:

1000. one thousand
1001. one thousand one
1002. one thousand two

Only the **first** number matters. The rest are renumbered sequentially (expected display: 5, 6, 7, 8):

5. first (source says 5)
1. second (source says 1)
1. third (source says 1)
42. fourth (source says 42)

A very large start number, 999999999 (nine digits, the maximum):

999999999. the marker is very wide
1000000000. has ten digits, which is not a list marker, so this line joins the previous item as a lazy continuation

---

## Wrapping and hanging indents

- This is a long list item that will certainly wrap onto several lines in any reasonably sized window. The continuation lines must line up under the first character of the text, not under the bullet. That's the "hanging indent" that makes lists readable. Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.
- Short item.
- Another long item with a hard line break right here,  
  and then a continuation line that must also align with the text above.
- A list item with a very long unbreakable word: Pneumonoultramicroscopicsilicovolcanoconiosis_and_then_some_more_characters_to_make_it_really_long_0123456789

1. Numbered item with long text that wraps. The wrapped lines align with the text after "1.", not with the digit. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.
2. Short.

---

## Block quotes

### Five levels deep, with rich content

The task items at level 5 are an implementation difference: cmark-gfm's tasklist extension leaves `[ ]` literal inside block quotes, while md4c renders checkboxes. Either result is acceptable, as long as it's consistent.

> # Level 1: a heading in a quote
>
> Level 1 paragraph with **bold**, *italic* and `code`.
>
> - A list at level 1
> - with two items
>
> > ## Level 2 heading
> >
> > Level 2 paragraph.
> >
> > ```js
> > // code at level 2
> > console.log("quoted twice");
> > ```
> >
> > > ### Level 3 heading
> > >
> > > 1. Ordered list at level 3
> > > 2. Second item
> > >
> > > > #### Level 4 heading
> > > >
> > > > | Table | at level 4 |
> > > > |-------|------------|
> > > > | a     | b          |
> > > >
> > > > > ##### Level 5 heading
> > > > >
> > > > > The deepest quote. Its left edge should show five vertical bars, one per level, and the text still needs a readable width. Long text wraps inside all five borders. Lorem ipsum dolor sit amet, consectetur adipiscing elit.
> > > > >
> > > > > - [ ] A task at level 5
> > > > > - [x] Done at level 5
> > > >
> > > > Back to level 4.
> > >
> > > Back to level 3.
> >
> > Back to level 2.
>
> Back to level 1.

### Quote with an attribution

> The best way to predict the future is to invent it.
>
> — <cite>Alan Kay</cite>

### Consecutive quotes

A blank line between two quotes makes them separate blocks:

> First quote.

> Second quote.

Without a blank line, they merge into one paragraph:

> First line.
> Second line, same paragraph.

A line with only `>` in between gives two paragraphs in the same quote:

> Paragraph one.
>
> Paragraph two.

### A quote inside a list, inside a quote

> 1. Item in a quote
>    > A quote in that item
>    > - with a list in that quote
>    >   > and another quote in that list

---

## Lazy continuation

Lines without the `>` marker still continue the paragraph inside the quote. This is called laziness. Expected: **one** quote containing one paragraph.

> This paragraph starts in a quote,
continues on a line without a marker,
and on another one,
> and returns to the marker.

Laziness also works for nested quotes. Expected: one paragraph inside a level-3 quote.

> > > Deeply quoted paragraph
continued lazily without any markers.

Laziness also works in list items. Expected: one list item with one paragraph.

- A list item paragraph
that continues on an unindented line.
- Next item.

Laziness **does not** carry other block types. The code fence below doesn't continue the quote, because a fence isn't a paragraph continuation:

> Quote paragraph.
```
this code block is NOT inside the quote
```

A thematic break ends a quote even without a blank line:

> Quote paragraph.
---

Text after the rule.

---

## 150-item list

The markers grow from 1 to 3 digits. On github.com the numbers are right-aligned, so the item text stays in one column the whole way down.

1. Documented image scaling on HiDPI displays, see `src/render/layout.cpp`
2. Improved dark theme colors
3. Removed image scaling on HiDPI displays ~~(reverted)~~
4. Documented emoji fallback fonts
5. Documented image scaling on HiDPI displays (#192)
6. Removed tab stop width ~~(reverted)~~
7. Documented image scaling on HiDPI displays
8. Added CP1251 fallback decoding (#147)
9. Improved tab stop width. This entry is intentionally long so it wraps onto a second line, which lets you check the hanging indent next to a three-digit marker as well as next to a one-digit marker.
10. Added footnote back-references — thanks to @contributor673
11. Removed dark theme colors ~~(reverted)~~
12. Improved tab stop width
13. Removed memory usage on 10 MB files
14. Optimized smooth scrolling :tada:
15. Optimized CP1251 fallback decoding ~~(reverted)~~
16. Added heading anchors (#913)
17. Refactored RTL paragraph direction
18. Optimized smooth scrolling **[breaking]**
19. Improved footnote back-references — thanks to @contributor723
20. Added link hover tooltips :tada:
21. Documented image scaling on HiDPI displays
22. Optimized link hover tooltips
23. Tested tab stop width, see `src/render/layout.cpp`
24. Improved UTF-16 detection **[breaking]**
25. Improved image scaling on HiDPI displays **[breaking]**
26. Tested CP1251 fallback decoding — thanks to @contributor762
27. Optimized table column sizing. This entry is intentionally long so it wraps onto a second line, which lets you check the hanging indent next to a three-digit marker as well as next to a one-digit marker.
28. Added alert icons **[breaking]**
29. Fixed task list checkboxes
30. Removed selection and copy — thanks to @contributor232
31. Tested emoji fallback fonts. This entry is intentionally long so it wraps onto a second line, which lets you check the hanging indent next to a three-digit marker as well as next to a one-digit marker.
32. Documented soft hyphen handling
33. Documented soft hyphen handling — thanks to @contributor240
34. Documented dark theme colors — thanks to @contributor823
35. Added emoji fallback fonts. This entry is intentionally long so it wraps onto a second line, which lets you check the hanging indent next to a three-digit marker as well as next to a one-digit marker.
36. Removed heading anchors
37. Added UTF-16 detection
38. Added search highlighting — thanks to @contributor104
39. Optimized nested block quotes :tada:
40. Fixed smooth scrolling :tada:
41. Documented selection and copy :tada:
42. Tested selection and copy. This entry is intentionally long so it wraps onto a second line, which lets you check the hanging indent next to a three-digit marker as well as next to a one-digit marker.
43. Improved task list checkboxes
44. Improved link hover tooltips **[breaking]**
45. Improved table column sizing ~~(reverted)~~
46. Improved dark theme colors ~~(reverted)~~
47. Improved task list checkboxes ~~(reverted)~~
48. Added UTF-16 detection ~~(reverted)~~
49. Optimized memory usage on 10 MB files, see `src/render/layout.cpp`
50. Tested smooth scrolling
51. Refactored emoji fallback fonts **[breaking]**
52. Optimized UTF-16 detection
53. Added RTL paragraph direction **[breaking]**
54. Optimized nested block quotes
55. Refactored emoji fallback fonts :tada:
56. Optimized code block horizontal scrolling — thanks to @contributor630
57. Removed soft hyphen handling, see `src/render/layout.cpp`
58. Optimized heading anchors :tada:
59. Removed heading anchors ~~(reverted)~~
60. Removed task list checkboxes. This entry is intentionally long so it wraps onto a second line, which lets you check the hanging indent next to a three-digit marker as well as next to a one-digit marker.
61. Optimized table column sizing :tada:
62. Refactored memory usage on 10 MB files
63. Optimized smooth scrolling — thanks to @contributor298
64. Improved heading anchors, see `src/render/layout.cpp`
65. Tested task list checkboxes
66. Tested alert icons, see `src/render/layout.cpp`
67. Fixed memory usage on 10 MB files ~~(reverted)~~
68. Improved footnote back-references, see `src/render/layout.cpp`
69. Removed memory usage on 10 MB files. This entry is intentionally long so it wraps onto a second line, which lets you check the hanging indent next to a three-digit marker as well as next to a one-digit marker.
70. Optimized emoji fallback fonts
71. Documented emoji fallback fonts. This entry is intentionally long so it wraps onto a second line, which lets you check the hanging indent next to a three-digit marker as well as next to a one-digit marker.
72. Added table column sizing
73. Tested nested block quotes
74. Tested dark theme colors ~~(reverted)~~
75. Added table column sizing
76. Improved RTL paragraph direction
77. Removed task list checkboxes
78. Removed CP1251 fallback decoding
79. Optimized UTF-16 detection :tada:
80. Added image scaling on HiDPI displays :tada:
81. Documented RTL paragraph direction, see `src/render/layout.cpp`
82. Added RTL paragraph direction
83. Tested code block horizontal scrolling :tada:
84. Added code block horizontal scrolling ~~(reverted)~~
85. Improved soft hyphen handling
86. Tested footnote back-references
87. Removed task list checkboxes :tada:
88. Improved RTL paragraph direction — thanks to @contributor143
89. Fixed emoji fallback fonts **[breaking]**
90. Removed UTF-16 detection **[breaking]**
91. Tested RTL paragraph direction **[breaking]**
92. Refactored soft hyphen handling (#815)
93. Tested nested block quotes (#960)
94. Documented smooth scrolling. This entry is intentionally long so it wraps onto a second line, which lets you check the hanging indent next to a three-digit marker as well as next to a one-digit marker.
95. Removed search highlighting, see `src/render/layout.cpp`
96. Refactored footnote back-references
97. Optimized nested block quotes
98. Tested heading anchors — thanks to @contributor240
99. Tested code block horizontal scrolling
100. Documented RTL paragraph direction (#265)
101. Documented task list checkboxes. This entry is intentionally long so it wraps onto a second line, which lets you check the hanging indent next to a three-digit marker as well as next to a one-digit marker.
102. Improved dark theme colors, see `src/render/layout.cpp`
103. Tested smooth scrolling
104. Optimized RTL paragraph direction
105. Improved footnote back-references ~~(reverted)~~
106. Improved emoji fallback fonts (#997)
107. Fixed code block horizontal scrolling — thanks to @contributor378
108. Added search highlighting — thanks to @contributor873
109. Added soft hyphen handling — thanks to @contributor515
110. Tested link hover tooltips :tada:
111. Fixed code block horizontal scrolling
112. Refactored table column sizing. This entry is intentionally long so it wraps onto a second line, which lets you check the hanging indent next to a three-digit marker as well as next to a one-digit marker.
113. Refactored emoji fallback fonts
114. Removed emoji fallback fonts ~~(reverted)~~
115. Improved smooth scrolling — thanks to @contributor983
116. Documented UTF-16 detection
117. Fixed RTL paragraph direction ~~(reverted)~~
118. Added UTF-16 detection (#212)
119. Removed CP1251 fallback decoding
120. Removed CP1251 fallback decoding — thanks to @contributor643
121. Added UTF-16 detection **[breaking]**
122. Fixed UTF-16 detection, see `src/render/layout.cpp`
123. Fixed RTL paragraph direction
124. Tested heading anchors :tada:
125. Documented memory usage on 10 MB files **[breaking]**
126. Documented RTL paragraph direction :tada:
127. Removed heading anchors — thanks to @contributor804
128. Added selection and copy, see `src/render/layout.cpp`
129. Added table column sizing, see `src/render/layout.cpp`
130. Refactored search highlighting
131. Improved selection and copy
132. Refactored alert icons :tada:
133. Refactored image scaling on HiDPI displays (#809)
134. Added UTF-16 detection **[breaking]**
135. Refactored dark theme colors **[breaking]**
136. Optimized heading anchors, see `src/render/layout.cpp`
137. Removed dark theme colors
138. Optimized selection and copy
139. Refactored RTL paragraph direction
140. Fixed emoji fallback fonts (#354)
141. Improved nested block quotes — thanks to @contributor936
142. Fixed selection and copy. This entry is intentionally long so it wraps onto a second line, which lets you check the hanging indent next to a three-digit marker as well as next to a one-digit marker.
143. Refactored heading anchors
144. Added alert icons
145. Optimized memory usage on 10 MB files. This entry is intentionally long so it wraps onto a second line, which lets you check the hanging indent next to a three-digit marker as well as next to a one-digit marker.
146. Added image scaling on HiDPI displays
147. Documented RTL paragraph direction :tada:
148. Fixed tab stop width
149. Fixed image scaling on HiDPI displays (#187)
150. Optimized footnote back-references

End of the 150-item list. [Back to top](#lists-and-block-quotes)

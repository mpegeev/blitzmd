---
title: "Front matter: how GitHub shows it"
layout: post
date: 2026-09-23
draft: false
tags: [markdown, yaml, "total commander"]
authors:
  - name: Ann Example
    email: ann@example.com
  - name: Bob Example
seo:
  description: A document with Jekyll/Hugo-style metadata.
  keywords: [viewer, wlx]
summary: |
  Block scalars keep
  their line breaks.
---

# Front matter

The YAML block at the very top of this file (between the `---` lines) is not
Markdown. mdview shows it as a two-column table, one row per top-level key.
(GitHub puts the keys side by side as columns, which gets wide and scrolls;
reading top to bottom is easier.) It must **not** render as a thematic break
followed by a setext heading.

Things to check:

- the table above has the keys in bold on the left, values on the right,
  in light and dark themes, without horizontal scrolling;
- quoted values keep their colons (`title`), short lists are joined with
  commas (`tags`), nested mappings are nested key/value tables (`seo`), a
  list of mappings is a stack of small tables (`authors`);
- `summary` keeps its line break;
- keys stay at the top of their row when the value is tall.

---

The thematic break above (in the middle of the document) is an ordinary
`<hr>`, and so is this setext heading's underline:

Setext heading
---

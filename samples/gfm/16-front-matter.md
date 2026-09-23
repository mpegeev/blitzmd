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
Markdown. GitHub shows it as a table: one column per top-level key, with
lists and nested mappings as nested tables. It must **not** render as a
thematic break followed by a setext heading.

Things to check:

- the table above looks like GitHub's in light and dark themes;
- quoted values keep their colons (`title`), lists become one-row tables
  (`tags`), a list of mappings becomes a row of small tables (`authors`);
- `summary` keeps its line break;
- wide tables scroll horizontally instead of squeezing columns.

---

The thematic break above (in the middle of the document) is an ordinary
`<hr>`, and so is this setext heading's underline:

Setext heading
---

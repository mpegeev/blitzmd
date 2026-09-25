# Mermaid: git graphs, kanban boards, packets, radar charts and treemaps

Still more diagrams drawn natively, like the others in [20-mermaid-flowchart.md](20-mermaid-flowchart.md) to [26-mermaid-more.md](26-mermaid-more.md).

## Git graph

```mermaid
gitGraph
    commit id: "Alpha"
    commit id: "Beta" tag: "v1.0"
    branch feature
    commit id: "Gamma" type: HIGHLIGHT
    checkout main
    commit id: "Delta" type: REVERSE
    branch hotfix
    commit id: "Fix"
    checkout main
    merge hotfix tag: "v1.1"
    checkout feature
    merge main
    commit id: "Epsilon"
    checkout main
    cherry-pick id: "Gamma"
    merge feature id: "Merged" type: HIGHLIGHT
```

## Git graph, top to bottom

```mermaid
gitGraph TB:
    commit
    branch develop
    commit
    commit
    checkout main
    commit
    merge develop
```

## Kanban

```mermaid
kanban
  Todo
    [Create Documentation]
    docs[Create Blog about the new diagram]
  [In progress]
    id6[Create renderer so that it works in all cases]
  id9[Ready for deploy]
    id8[Design grammar]@{ assigned: 'knsv' }
  id10[Ready for test]
    id4[Create parsing tests]@{ ticket: MC-2038, assigned: 'K.Sveidqvist', priority: 'High' }
    id66[last item]@{ priority: 'Very Low', assigned: 'knsv' }
  id11[Done]
    id5[define getData]
    id2[Title of diagram is more than 100 chars]@{ ticket: MC-2036, priority: 'Very High'}
```

## Packet

```mermaid
packet-beta
title UDP Packet
0-15: "Source Port"
16-31: "Destination Port"
32-47: "Length"
48-63: "Checksum"
64-95: "Data (variable length)"
```

## Radar chart

```mermaid
radar-beta
  title Grades
  axis m["Math"], s["Science"], e["English"]
  axis h["History"], g["Geography"], a["Art"]
  curve a["Alice"]{85, 90, 80, 70, 75, 90}
  curve b["Bob"]{70, 75, 85, 80, 90, 85}
  max 100
  min 0
```

## Treemap

```mermaid
treemap-beta
"Section 1"
    "Leaf 1.1": 12
    "Section 1.2"
      "Leaf 1.2.1": 12
"Section 2"
    "Leaf 2.1": 20
    "Leaf 2.2": 25
```

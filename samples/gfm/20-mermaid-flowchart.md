# Mermaid: flowcharts

GitHub draws ```` ```mermaid ```` blocks as diagrams. mdview lays them out itself (no browser, no JavaScript), in the colors of GitHub's light and dark Mermaid themes; like images, they zoom with Ctrl + wheel and open in the whole window. Copying a diagram gives its source, and search finds text on its labels.

## A decision

```mermaid
flowchart TD
    A[Christmas] -->|Get money| B(Go shopping)
    B --> C{Let me think}
    C -->|One| D[Laptop]
    C -->|Two| E[iPhone]
    C -->|Three| F[fa:fa-car Car]
```

## Node shapes, left to right

```mermaid
flowchart LR
    A[Rect] --> B(Rounded) --> C([Stadium]) --> D[[Subroutine]] --> E[(Database)]
    E --> F((Circle)) --> G(((Double circle)))
    A --> H>Asymmetric] --> I{Rhombus} --> J{{Hexagon}}
    J --> K[/Lean right/] --> L[\Lean left\] --> M[/Trapezoid\] --> N[\Alt trapezoid/]
```

## Links

```mermaid
flowchart LR
    A -- text --> B -- more text --> C
    A --- D
    A -.-> E
    A == thick ==> F
    A --o G
    A --x H
    I <--> J
    K o--o L
    M x--x N
    O -.- P
    Q ---->|longer| R
```

## Subgraphs

Subgraphs that nothing links into from outside are laid out on their own, in their own direction:

```mermaid
flowchart LR
  subgraph TOP
    direction TB
    subgraph B1
        direction RL
        i1 -->f1
    end
    subgraph B2
        direction BT
        i2 -->f2
    end
  end
  A --> TOP --> B
  B1 --> B2
```

Links crossing into subgraphs keep them side by side:

```mermaid
flowchart TB
    c1-->a2
    subgraph one
    a1-->a2
    end
    subgraph two
    b1-->b2
    end
    subgraph three
    c1-->c2
    end
```

## A pipeline with styles

```mermaid
---
title: Build pipeline
---
flowchart LR
    src[Source] --> build[Build]:::step --> test{Tests pass?}
    test -->|yes| pkg[Package]:::step --> rel([Release])
    test -->|no| fix[Fix it] --> build
    classDef step fill:#dbeafe,stroke:#3b82f6,stroke-width:2px,color:#1e3a8a
    style rel fill:#dcfce7,stroke:#16a34a,stroke-width:2px,color:#14532d
    linkStyle 3 stroke:#16a34a,stroke-width:3px
```

## Markdown in labels

```mermaid
flowchart TD
    A["`**Bold** and *italic*
    on two lines`"] --> B["A #quot;quoted#quot; label<br>with a line break"]
    B --> C{{"Unicode: привет, 日本語, ✓"}}
```

## Not drawn

A diagram type mdview doesn't draw yet stays code, with a note:

```mermaid
C4Context
    title System Context diagram
    Person(customer, "Customer")
    System(shop, "Online shop")
    Rel(customer, shop, "Buys from")
```

So does a diagram with an error in it:

```mermaid
flowchart TD
    A[Unclosed --> B
```

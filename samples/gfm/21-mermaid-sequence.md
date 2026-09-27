# Mermaid: sequence diagrams

Sequence diagrams drawn natively, like the flowcharts in [20-mermaid-flowchart.md](20-mermaid-flowchart.md).

## Messages

```mermaid
sequenceDiagram
    Alice->>John: Hello John, how are you?
    John-->>Alice: Great!
    Alice-)John: See you later!
```

## Every arrow

```mermaid
sequenceDiagram
    participant A
    participant B
    A->B: Solid line, no arrow
    A-->B: Dotted line, no arrow
    A->>B: Solid line with arrowhead
    A-->>B: Dotted line with arrowhead
    A<<->>B: Arrowheads at both ends
    A-xB: Solid line with a cross
    A--xB: Dotted line with a cross
    A-)B: Solid line, open arrow (async)
    A--)B: Dotted line, open arrow
    A->>A: To itself
```

## Actors, activations, notes, numbers

```mermaid
sequenceDiagram
    autonumber
    participant A as Alice
    actor B as Bob
    participant C as Carol
    A->>+B: Hello Bob, how are you?
    B->>+C: Ask Carol
    C-->>-B: Fine
    B-->>-A: Great!
    Note right of C: Carol thinks<br/>a long time
    Note over A,B: A typical interaction
    activate A
    A->>C: Ping
    deactivate A
```

## Blocks

```mermaid
sequenceDiagram
    participant A as Alice
    participant J as John
    loop Every minute
        J-->>A: Great!
    end
    alt is sick
        J->>A: Not so good :(
    else is well
        J->>A: Feeling fresh like a daisy
    end
    opt Extra response
        J->>A: Thanks for asking
    end
    par Alice to John
        A->>J: Hello guys!
    and Alice to John again
        A->>J: Hello again!
    end
    critical Establish a connection
        A-->J: connect
    option Timeout
        A-->J: fail
    end
    break when the booking process fails
        A-->J: show failure
    end
    rect rgb(191, 223, 255)
        A->>J: A colored background
    end
```

## Boxes, creating and destroying

```mermaid
sequenceDiagram
    box Purple Alice & John
    participant A
    participant J
    end
    box Another Group
    participant C
    end
    A->>J: Hello John, how are you?
    J->>A: Great!
    A->>C: Hello Charly
    create participant E
    C->>E: Hi Eve
    destroy E
    E-->>C: Bye
```

## A real one

```mermaid
---
title: Opening a file in Total Commander
---
sequenceDiagram
    actor U as User
    participant TC as Total Commander
    participant P as blitzmd.wlx
    U->>TC: F3 on README.md
    TC->>+P: ListLoadW(parent, "README.md", flags)
    P->>P: parse + layout
    P-->>-TC: HWND of the viewer window
    U->>TC: Ctrl+C
    TC->>P: ListSendCommand(lc_copy)
    P-->>TC: LISTPLUGIN_OK
```

# Mermaid: state diagrams

State diagrams drawn natively, like the other Mermaid diagrams in [20-mermaid-flowchart.md](20-mermaid-flowchart.md), [21-mermaid-sequence.md](21-mermaid-sequence.md) and [22-mermaid-class.md](22-mermaid-class.md).

## States and transitions

```mermaid
---
title: Simple sample
---
stateDiagram-v2
    [*] --> Still
    Still --> [*]
    Still --> Moving
    Moving --> Still
    Moving --> Crash
    Crash --> [*]
```

## Descriptions, choices, forks and notes

```mermaid
stateDiagram-v2
    state if_state <<choice>>
    [*] --> IsPositive
    IsPositive --> if_state
    if_state --> False: if n < 0
    if_state --> True : if n >= 0
    state fork_state <<fork>>
    True --> fork_state
    fork_state --> State2
    fork_state --> State3
    state join_state <<join>>
    State2 --> join_state
    State3 --> join_state
    join_state --> State4
    State4 --> [*]
    state "This is a state description" as s2
    False --> s2
    s2 : more text
    s2 : and even more
    s2 --> s2 : retry
    note left of State4
        Important information! You can write
        notes.
    end note
```

## Composite states and concurrency

```mermaid
stateDiagram-v2
    [*] --> Active

    state Active {
        [*] --> NumLockOff
        NumLockOff --> NumLockOn : EvNumLockPressed
        NumLockOn --> NumLockOff : EvNumLockPressed
        --
        [*] --> CapsLockOff
        CapsLockOff --> CapsLockOn : EvCapsLockPressed
        CapsLockOn --> CapsLockOff : EvCapsLockPressed
    }
    state SomethingElse {
        A --> B
        B --> A
    }

    Active --> SomethingElse
    note right of SomethingElse : This is the note to the right.

    SomethingElse --> [*]
```

## Nested composites, left to right, styles

```mermaid
stateDiagram-v2
    direction LR
    classDef done fill:#dcfce7,stroke:#16a34a,color:#14532d
    [*] --> Loading
    state Loading {
        [*] --> Reading
        Reading --> Parsing
        state Parsing {
            [*] --> Blocks
            Blocks --> Inlines
        }
    }
    Loading --> Ready:::done : laid out
    Ready --> Loading : file changed
    Ready --> [*]
```

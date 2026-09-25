# Mermaid: beta diagram types

Mermaid's newest (beta) diagram types, drawn natively like the others in [20-mermaid-flowchart.md](20-mermaid-flowchart.md) to [28-mermaid-c4-architecture.md](28-mermaid-c4-architecture.md).

## Tree view

```mermaid
treeView-beta
    title Project
    src/
        main.cpp ## entry point
        util/ :::highlight
            strings.h icon(file)
    docs/ icon(folder)
    README.md
```

## Ishikawa (fishbone)

```mermaid
ishikawa-beta
    Late delivery
    People
        Not enough drivers
        Training
    Process
        Manual routing
    Equipment
        Old trucks
            Breakdowns
    Weather
```

## Cynefin

```mermaid
cynefin-beta
  title Where our work sits
  complex
    "New product line"
  complicated
    "Tax rules"
    "Capacity planning"
  clear
    "Invoicing"
  chaotic
    "Outage"
  confusion
    "Reorganisation"
  chaotic --> complex : "stabilise"
  complex --> complicated : "learn"
```

## Venn

```mermaid
venn-beta
  title Programming languages
  set Python["Python"]: 30
  set Rust: 12
  set Go: 18
  union Python,Rust: 4
  union Python,Go["Scripts and servers"]: 6
  union Rust,Go: 3
  union Python,Rust,Go: 1
```

```mermaid
venn-beta
  set A["Frontend"]
    text r1["React"]
    text v1["Vue"]
  set B["Backend"]
    text n1["Node"]
  union A,B["Fullstack"]
    text t1["TypeScript"]
  style A fill:#ff9900
  style A,B fill:#ffd700
```

## Wardley map

```mermaid
wardley-beta
title Tea Shop
size [800, 500]
evolution Genesis -> Custom -> Product -> Commodity
anchor Business [0.95, 0.63]
anchor Public [0.95, 0.78]
component Cup of Tea [0.79, 0.61] label [19, -4]
component Cup [0.73, 0.78]
component Tea [0.63, 0.81]
component Hot Water [0.52, 0.80]
component Kettle [0.43, 0.35] (buy) inertia
component Power [0.10, 0.7] (outsource)
Business -> Cup of Tea
Public -> Cup of Tea
Cup of Tea -> Cup
Cup of Tea -> Tea; limited by
Cup of Tea -> Hot Water
Hot Water -> Kettle
Kettle +> Power
evolve Kettle 0.62
note "Standardising power allows Kettles to evolve faster" [0.30, 0.49]
```

## Railroad diagrams

```mermaid
railroad-beta
expr = sequence(nonterminal("term"), zeroOrMore(sequence(choice(terminal("+"), terminal("-")), nonterminal("term"))));
term = sequence(nonterminal("factor"), optional(choice(terminal("*"), terminal("/"))));
factor = choice(terminal("number"), sequence(terminal("("), nonterminal("expr"), terminal(")")));
```

```mermaid
railroad-ebnf-beta
value = object | array | string | number | "true" | "false" | "null" ;
object = "{" , [ member , { "," , member } ] , "}" ;
member = string ":" value ;
letter = ? any unicode letter ? - digit ;
```

```mermaid
railroad-abnf-beta
rulelist = 1*( rule / (*c-wsp c-nl) ) ;
digit = %x30-39 ;
opt = [ "a" ] 2"b" 3*5"c" ;
```

```mermaid
railroad-peg-beta
Sum <- Product (('+' / "-") Product)* ;
Value <- Number / "(" Sum ")" / !"x" . ;
```

## Event model

```mermaid
eventmodeling
tf 01 ui Web.OrderForm
tf 02 cmd PlaceOrder { orderId: 1, items: 3 }
tf 03 evt Orders.OrderPlaced [[OrderData]]
tf 04 pcr Billing.Charger ->> 03
tf 05 cmd ChargeCard
tf 06 evt Payments.CardCharged
rf 07 rmo OrderStatus ->> 03 ->> 06

data OrderData {
  orderId: string
  total: number
}
```

## Swimlanes

```mermaid
swimlane-beta LR
  subgraph Customer
    A[Place order] --> B{Paid?}
  end
  subgraph Shop
    C[Pack items] --> D[Ship]
  end
  subgraph Bank
    E[Charge card]
  end
  B -->|yes| C
  B -->|no| E
  E --> C
```

```mermaid
swimlane-beta TD
  subgraph Dev
    a[Write code] --> b[Open PR]
  end
  subgraph Review
    c[Review] --> d{OK?}
  end
  subgraph Ops
    e[Deploy]
  end
  b --> c
  d -->|yes| e
  d -->|no| a
```

## Info

```mermaid
info
```

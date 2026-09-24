# Mermaid: class diagrams

Class diagrams drawn natively, like the flowcharts in [20-mermaid-flowchart.md](20-mermaid-flowchart.md) and the sequence diagrams in [21-mermaid-sequence.md](21-mermaid-sequence.md).

## Classes and inheritance

```mermaid
---
title: Animal example
---
classDiagram
    note "From Duck till Zebra"
    Animal <|-- Duck
    note for Duck "can fly<br>can swim<br>can dive<br>can help in debugging"
    Animal <|-- Fish
    Animal <|-- Zebra
    Animal : +int age
    Animal : +String gender
    Animal: +isMammal()
    Animal: +mate()
    class Duck{
        +String beakColor
        +swim()
        +quack()
    }
    class Fish{
        -int sizeInFeet
        -canEat()
    }
    class Zebra{
        +bool is_wild
        +run()
    }
```

## Members: visibility, generics, static and abstract

```mermaid
classDiagram
    class Square~Shape~{
        int id
        List~int~ position
        setPoints(List~int~ points)
        getPoints() List~int~
    }
    Square : -List~string~ messages$
    Square : +setMessages(List~string~ messages)
    Square : +getMessages() List~string~
    Square : +getDistanceMatrix() List~List~int~~
    Square : +someAbstractMethod()*
    class BankAccount{
        <<Entity>>
        +String owner
        +BigDecimal balance
        #deposit(amount) bool
        ~withdrawal(amount) int
    }
```

## Every relation

```mermaid
classDiagram
    classA --|> classB : Inheritance
    classC --* classD : Composition
    classE --o classF : Aggregation
    classG --> classH : Association
    classI -- classJ : Link(Solid)
    classK ..> classL : Dependency
    classM ..|> classN : Realization
    classO .. classP : Link(Dashed)
```

## Cardinality, two-way relations and lollipops

```mermaid
classDiagram
    Customer "1" --> "*" Ticket
    Student "1" --> "1..*" Course
    Galaxy --> "many" Star : Contains
    Animal <|--|> Zebra
    bar ()-- foo
```

## Annotations, namespaces and styles

```mermaid
classDiagram
    class Shape
    <<interface>> Shape
    Shape : noOfVertices
    Shape : draw()
    namespace BaseShapes {
        class Triangle
        class Rectangle {
            double width
            double height
        }
    }
    Shape <|.. Triangle
    Shape <|.. Rectangle
    class Circle["A round shape"]
    Shape <|.. Circle
    classDef round fill:#fde68a,stroke:#b45309,color:#78350f
    cssClass "Circle" round
```

## Left to right

```mermaid
classDiagram
    direction LR
    class Document {
        +String path
        +parse() Block
    }
    class Block {
        <<abstract>>
        +layout()*
    }
    class Paragraph
    class CodeBlock {
        +String language
    }
    Document "1" *-- "many" Block : contains
    Block <|-- Paragraph
    Block <|-- CodeBlock
    CodeBlock ..> Highlighter : uses
```

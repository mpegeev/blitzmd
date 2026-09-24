# Mermaid: entity relationship diagrams

Entity relationship diagrams drawn natively, like the other Mermaid diagrams in [20-mermaid-flowchart.md](20-mermaid-flowchart.md) to [23-mermaid-state.md](23-mermaid-state.md).

## Entities and relationships

```mermaid
---
title: Order example
---
erDiagram
    CUSTOMER ||--o{ ORDER : places
    ORDER ||--|{ LINE-ITEM : contains
    CUSTOMER }|..|{ DELIVERY-ADDRESS : uses
```

## Attributes, keys and comments

```mermaid
erDiagram
    CAR ||--o{ NAMED-DRIVER : allows
    CAR {
        string registrationNumber PK
        string make
        string model
        string[] parts
    }
    PERSON ||--o{ NAMED-DRIVER : is
    PERSON {
        string driversLicense PK "The license #"
        string(99) firstName "Only 99 characters are allowed"
        string lastName
        string phone UK
        int age
    }
    NAMED-DRIVER {
        string carRegistrationNumber PK, FK
        string driverLicence PK, FK
    }
    MANUFACTURER only one to zero or more CAR : makes
```

## Every cardinality, aliases, left to right

```mermaid
erDiagram
    direction LR
    p[Person]
    a["Customer Account"]
    o[Order]
    i[Item]
    p ||--o| a : has
    a |o--|{ o : "places"
    o }|..o{ i : lists
    EMPLOYEE |o--o{ EMPLOYEE : manages
```

## Subgraphs

```mermaid
erDiagram
    subgraph sales ["Sales"]
        CUSTOMER ||--o{ ORDER : places
        ORDER ||--|{ LINE-ITEM : contains
    end
    subgraph catalog ["Catalog"]
        PRODUCT {
            string sku PK
            string name
        }
    end
    LINE-ITEM }o--|| PRODUCT : refers
    sales }o..o{ catalog : browses
```

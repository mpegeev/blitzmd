# Mermaid: requirement, sankey, block, C4 and architecture diagrams

The last Mermaid types drawn natively, like the others in [20-mermaid-flowchart.md](20-mermaid-flowchart.md) to [27-mermaid-git-kanban.md](27-mermaid-git-kanban.md).

## Requirement diagram

```mermaid
requirementDiagram

requirement test_req {
    id: 1
    text: the test text.
    risk: high
    verifymethod: test
}

functionalRequirement test_req2 {
    id: 1.1
    text: the second test text.
    risk: low
    verifymethod: inspection
}

performanceRequirement test_req3 {
    id: 1.2
    text: the third test text.
    risk: medium
    verifymethod: demonstration
}

element test_entity {
    type: simulation
}

element test_entity2 {
    type: word doc
    docRef: reqs/test_entity
}

test_entity - satisfies -> test_req2
test_req - traces -> test_req2
test_req - contains -> test_req3
test_entity2 - verifies -> test_req
```

## Sankey diagram

```mermaid
sankey-beta

Agricultural 'waste',Bio-conversion,124.729
Bio-conversion,Liquid,0.597
Bio-conversion,Losses,26.862
Bio-conversion,Solid,280.322
Bio-conversion,Gas,81.144
Biofuel imports,Liquid,35
Coal imports,Coal,11.606
Coal,Solid,75.571
Gas,Industry,10.639
Liquid,Industry,121.066
Solid,Industry,46.477
Solid,"Thermal generation",400.12
"Thermal generation",Losses,787.129
```

## Block diagram

```mermaid
block-beta
    columns 3
    frontend["Frontend"] blockArrowId<["&nbsp;&nbsp;&nbsp;"]>(right) backend(("Backend"))
    space:2 down<[" "]>(down)
    disk[("Disk")] left<[" "]>(left) db[("Database")]
    block:group1:3
        columns 2
        a{"Decision"} b>"Flag"]
    end
    backend --> db
    db -- "stores" --> disk
    style frontend fill:#969,stroke:#333,stroke-width:4px
```

## C4 context diagram

```mermaid
C4Context
    title System Context diagram for Internet Banking System
    Enterprise_Boundary(b0, "BankBoundary0") {
        Person(customerA, "Banking Customer A", "A customer of the bank, with personal bank accounts.")
        Person_Ext(customerB, "Banking Customer B")
        System(SystemAA, "Internet Banking System", "Allows customers to view information about their bank accounts, and make payments.")

        Enterprise_Boundary(b1, "BankBoundary") {
            SystemDb_Ext(SystemE, "Mainframe Banking System", "Stores all of the core banking information about customers, accounts, transactions, etc.")
            SystemQueue(SystemQ, "Message queue", "Carries events between the systems.")
        }
    }

    BiRel(customerA, SystemAA, "Uses")
    Rel(SystemAA, SystemE, "Uses")
    Rel(SystemAA, SystemQ, "Sends events", "AMQP")
```

## C4 container diagram

```mermaid
C4Container
    title Container diagram for the Internet Banking System
    Person(customer, "Customer", "A customer of the bank")
    Container_Boundary(c1, "Internet Banking") {
        Container(spa, "Single-Page App", "JavaScript, Angular", "All of the banking functions in the browser")
        Container(api, "API Application", "Java, Docker", "Banking functions over a JSON/HTTPS API")
        ContainerDb(db, "Database", "SQL Database", "User registration, hashed credentials, access logs")
    }
    Rel(customer, spa, "Uses", "HTTPS")
    Rel(spa, api, "Uses", "JSON/HTTPS")
    Rel_Back(db, api, "Reads from and writes to", "JDBC")
```

## Architecture diagram

```mermaid
architecture-beta
    group api(cloud)[API]

    service db(database)[Database] in api
    service disk1(disk)[Storage] in api
    service disk2(disk)[Storage] in api
    service server(server)[Server] in api

    db:L -- R:server
    disk1:T -- B:server
    disk2:T -- B:db
```

## Architecture: junctions, arrows, labels, group edges

```mermaid
architecture-beta
    group public(internet)[Public]
    group private(cloud)[Private]

    service gateway(internet)[Gateway] in public
    service web "Web app"[Front end] in private
    service cache(disk)[Cache] in private
    service store(database)[Store] in private
    service other(logos:aws-s3)[From an icon pack]
    junction hub in private

    gateway{group}:B -[HTTPS]-> T:web{group}
    web:R --> L:hub
    hub:T -- B:cache
    hub:B -- T:store
    store:R <-[sync]-> L:other
```

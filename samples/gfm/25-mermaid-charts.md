# Mermaid: pie charts, user journeys and Gantt charts

Charts drawn natively, like the other Mermaid diagrams in [20-mermaid-flowchart.md](20-mermaid-flowchart.md) to [24-mermaid-er.md](24-mermaid-er.md).

## Pie chart

```mermaid
pie showData
    title Key elements in Product X
    "Calcium" : 42.96
    "Potassium" : 50.05
    "Magnesium" : 10.01
    "Iron" :  5
```

## Pie chart with a front matter title

```mermaid
---
title: Pets adopted by volunteers
---
pie
    "Dogs" : 386
    "Cats" : 85
    "Rats" : 15
```

## User journey

```mermaid
journey
    title My working day
    section Go to work
      Make tea: 5: Me
      Go upstairs: 3: Me
      Do work: 1: Me, Cat
    section Go home
      Go downstairs: 5: Me
      Sit down: 5: Me
```

## Gantt chart

```mermaid
gantt
    dateFormat  YYYY-MM-DD
    title       Adding GANTT diagram functionality to mermaid
    excludes    weekends

    section A section
    Completed task            :done,    des1, 2014-01-06,2014-01-08
    Active task               :active,  des2, 2014-01-09, 3d
    Future task               :         des3, after des2, 5d
    Future task2              :         des4, after des3, 5d

    section Critical tasks
    Completed task in the critical line :crit, done, 2014-01-06,24h
    Implement parser and jison          :crit, done, after des1, 2d
    Create tests for parser             :crit, active, 3d
    Future task in critical line        :crit, 5d
    Create tests for renderer           :2d
    Add to mermaid                      :until isadded
    Functionality added                 :milestone, isadded, 2014-01-25, 0d

    section Documentation
    Describe gantt syntax               :active, a1, after des1, 3d
    Add gantt diagram to demo page      :after a1  , 20h
    Add another diagram to demo page    :doc1, after a1  , 48h
```

## Gantt chart: hours, compact rows, a marker

```mermaid
---
displayMode: compact
---
gantt
    title Release day
    dateFormat YYYY-MM-DD HH:mm
    axisFormat %H:%M
    tickInterval 2hour
    section Build
    Compile          :a1, 2024-01-01 08:00, 3h
    Package          :a2, 2024-01-01 10:00, 4h
    Hotfix           :crit, active, 2024-01-01 09:00, 2h
    section Ship
    Upload           :2024-01-01 12:00, 2h
    Announce         :done, 1h
    Release          :milestone, m1, 2024-01-01 17:00, 0h
    Code freeze      :vert, v1, 2024-01-01 16:00, 0h
```

## Gantt chart: other date formats, holidays, "until"

```mermaid
gantt
    dateFormat DD.MM.YYYY
    axisFormat %b %e
    inclusiveEndDates
    excludes sunday, 01.05.2024
    section Planning
    Kickoff      :milestone, k, 01.04.2024, 0d
    Research     :r, after k, 10d
    Specs        :s, 08.04.2024, 19.04.2024
    section Build
    Backend      :crit, b, after s, 3w
    Frontend     :f, after s, until q
    QA           :q, after b, 1w
    Launch       :milestone, after q, 0d
```

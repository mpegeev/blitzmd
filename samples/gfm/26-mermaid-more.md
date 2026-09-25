# Mermaid: timelines, mindmaps, quadrant charts and XY charts

More diagrams drawn natively, like the others in [20-mermaid-flowchart.md](20-mermaid-flowchart.md) to [25-mermaid-charts.md](25-mermaid-charts.md).

## Timeline

```mermaid
timeline
    title History of Social Media Platform
    2002 : LinkedIn
    2004 : Facebook
         : Google
    2005 : Youtube
    2006 : Twitter
```

## Timeline with sections

```mermaid
timeline
    title Timeline of Industrial Revolution
    section 17th-20th century
        Industry 1.0 : Machinery, Water power, Steam <br>power
        Industry 2.0 : Electricity, Internal combustion engine, Mass production
        Industry 3.0 : Electronics, Computers, Automation
    section 21st century
        Industry 4.0 : Internet, Robotics, Internet of Things
        Industry 5.0 : Artificial intelligence, Big data, 3D printing
```

## Timeline, top to bottom

```mermaid
timeline TD
    title Release plan
    section Spring
        March : Design
        April : Prototype : User tests
    section Summer
        June : Release
```

## Mindmap

```mermaid
mindmap
  root((mindmap))
    Origins
      Long history
      ::icon(fa fa-book)
      Popularisation
        British popular psychology author Tony Buzan
    Research
      On effectiveness<br/>and features
      On Automatic creation
        Uses
            Creative techniques
            Strategic planning
            Argument mapping
    Tools
      Pen and paper
      Mermaid
```

## Mindmap node shapes

```mermaid
mindmap
    Root
        A[A square]
        B(Rounded)
        C((Circle))
        D))Bang((
        E)Cloud(
        F{{Hexagon}}
        G["`**Markdown** text`"]
```

## Quadrant chart

```mermaid
quadrantChart
    title Reach and engagement of campaigns
    x-axis Low Reach --> High Reach
    y-axis Low Engagement --> High Engagement
    quadrant-1 We should expand
    quadrant-2 Need to promote
    quadrant-3 Re-evaluate
    quadrant-4 May be improved
    Campaign A: [0.3, 0.6]
    Campaign B: [0.45, 0.23]
    Campaign C: [0.57, 0.69]
    Campaign D: [0.78, 0.34]
    Campaign E: [0.40, 0.34]
    Campaign F: [0.35, 0.78]
```

## Quadrant chart with styled points

```mermaid
quadrantChart
  x-axis Urgent --> Not Urgent
  y-axis Not Important --> "Important ❤"
  quadrant-1 Plan
  quadrant-2 Do
  quadrant-3 Delegate
  quadrant-4 Delete
  Point A:::class1: [0.9, 0.0]
  Point B: [0.8, 0.1] radius: 12
  Point C: [0.7, 0.2] radius: 10, color: #ff3300
  Point D: [0.6, 0.3] radius: 15, stroke-color: #00ff0f, stroke-width: 5px ,color: #ff33f0
  classDef class1 color: #109060, radius : 20, stroke-color: #310085, stroke-width: 10px
```

## XY chart

```mermaid
xychart-beta
    title "Sales Revenue"
    x-axis [jan, feb, mar, apr, may, jun, jul, aug, sep, oct, nov, dec]
    y-axis "Revenue (in $)" 4000 --> 11000
    bar [5000, 6000, 7500, 8200, 9500, 10500, 11000, 10200, 9200, 8500, 7000, 6000]
    line [5000, 6000, 7500, 8200, 9500, 10500, 11000, 10200, 9200, 8500, 7000, 6000]
```

## XY chart, horizontal, with a legend

```mermaid
xychart-beta horizontal
    title "Units per quarter"
    x-axis "Quarter" [Q1, Q2, Q3, Q4]
    y-axis "Units"
    bar "Sold" [30, 45, 28, 60]
    line "Target" [25, 40, 34, 42]
```

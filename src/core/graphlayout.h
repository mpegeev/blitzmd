// Layered drawing of directed graphs (Sugiyama's method, as dagre does it for
// Mermaid): ranks by network simplex, crossing reduction by barycenters,
// coordinates by weighted medians with minimal separations, and edges routed
// through the ranks they span. Nodes may be grouped in (nested) clusters,
// drawn as boxes around them: clusters span whole ranks, their members stay
// together in every rank and edges leave them through their sides.
#pragma once

#include <d2d1.h>

#include <vector>

namespace mdv {

enum class RankDir : uint8_t { TB, BT, LR, RL };

struct LayeredGraph {
    struct Node {
        float w = 0, h = 0;
        int cluster = -1;             // innermost cluster (index into clusters)
    };
    struct Edge {
        int from = 0, to = 0;
        int minlen = 1;               // ranks between the ends, at least
        float weight = 1;             // keeps the ends close
        float label_w = 0, label_h = 0;  // a label sits on the edge's middle rank
    };
    struct Cluster {
        int parent = -1;
        float title_w = 0, title_h = 0;  // a title band at the box's top
    };
    std::vector<Node> nodes;
    std::vector<Edge> edges;          // no self loops (the caller draws those)
    std::vector<Cluster> clusters;    // parents before children
    RankDir dir = RankDir::TB;
    float node_sep = 50;              // between nodes of a rank
    float rank_sep = 50;              // between ranks
    float edge_sep = 20;              // between edges passing through a rank
    float cluster_pad = 12;           // inside cluster boxes
};

struct LayeredLayout {
    std::vector<D2D1_POINT_2F> centers;                // per node
    std::vector<std::vector<D2D1_POINT_2F>> bends;     // per edge, from its source to its target
    std::vector<D2D1_POINT_2F> labels;                 // per edge: center of its label
    std::vector<D2D1_RECT_F> clusters;                 // per cluster (empty ones: zero size)
    float width = 0, height = 0;                       // bounding box (origin at 0,0)
};

// Lays out `g`; the work is bounded, so huge graphs still finish (with a
// plainer layout).
LayeredLayout LayoutLayered(const LayeredGraph& g);

}  // namespace mdv

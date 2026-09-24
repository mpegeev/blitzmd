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
        float after = 0;              // room kept beside it in its rank (right; below
                                      // in LR/RL), as dagre does for self loops
        float before = 0;             // and on the other side (left; above)
    };
    struct Edge {
        int from = 0, to = 0;
        int minlen = 1;               // ranks between the ends, at least
        float weight = 1;             // keeps the ends close
        float label_w = 0, label_h = 0;  // a label sits on the edge's middle rank
        // A cluster's box as an end (from / to then name a node inside it):
        // the edge leaves it through its bottom, or enters through its top.
        int from_cluster = -1, to_cluster = -1;
    };
    struct Cluster {
        int parent = -1;
        float title_w = 0, title_h = 0;  // a title band at the box's top
        // Starts and ends on its parent's ranks, no empty rank rows inside
        // (a state's concurrent region: no box of its own is drawn).
        bool share_ranks = false;
        float after = 0;              // room kept beside its box (right; below sideways)
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
    bool too_large = false;                            // over the budget below: nothing laid out
};

// Bends and cluster sides the layout may create: long edges through many
// ranks cost one node per rank, and the cost grows with the square of the
// graph's size.
constexpr int kMaxLayoutNodes = 40000;

// Lays out `g`; the work is bounded, so huge graphs still finish (with a
// plainer layout) or give up (too_large). `ranks_only`: only finds out
// whether it would give up (the node sizes don't matter for that).
LayeredLayout LayoutLayered(const LayeredGraph& g, bool ranks_only = false);

}  // namespace mdv

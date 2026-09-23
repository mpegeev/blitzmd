#include "core/graphlayout.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <numeric>

namespace mdv {
namespace {

// Budgets: beyond these the layout gets plainer, never slow.
constexpr long long kSimplexWork = 60'000'000;  // edge visits in network simplex
constexpr int kMaxSweeps = 24;                  // crossing reduction passes
constexpr int kCoordinatePasses = 8;

// ---------------------------------------------------------------------------
// Ranking: network simplex (Gansner et al.), per connected component.
// ---------------------------------------------------------------------------

struct RankEdge {
    int u, v;       // u above v
    int minlen;
    float weight;
};

class NetworkSimplex {
public:
    NetworkSimplex(int n, std::vector<RankEdge> edges, long long* work)
        : n_(n), e_(std::move(edges)), work_(work), rank_(static_cast<size_t>(n), 0) {
        adj_.resize(static_cast<size_t>(n));
        for (int i = 0; i < static_cast<int>(e_.size()); ++i) {
            adj_[static_cast<size_t>(e_[static_cast<size_t>(i)].u)].push_back(i);
            adj_[static_cast<size_t>(e_[static_cast<size_t>(i)].v)].push_back(i);
        }
    }

    // Ranks of a connected graph (the smallest is 0).
    std::vector<int> Run() {
        if (n_ == 0) return {};
        LongestPath();
        if (n_ > 1 && !e_.empty()) {
            FeasibleTree();
            LowLim();
            CutValues();
            for (int guard = 0; guard < 4 * n_ + 100 && *work_ > 0; ++guard) {
                const int leave = LeaveEdge();
                if (leave < 0) break;
                const int enter = EnterEdge(leave);
                if (enter < 0) break;
                in_tree_[static_cast<size_t>(leave)] = 0;
                in_tree_[static_cast<size_t>(enter)] = 1;
                LowLim();
                CutValues();
                UpdateRanks();
            }
        }
        const int lo = *std::min_element(rank_.begin(), rank_.end());
        for (int& r : rank_) r -= lo;
        return rank_;
    }

private:
    int Slack(int e) const {
        const RankEdge& x = e_[static_cast<size_t>(e)];
        return rank_[static_cast<size_t>(x.v)] - rank_[static_cast<size_t>(x.u)] - x.minlen;
    }
    int Other(int e, int v) const {
        const RankEdge& x = e_[static_cast<size_t>(e)];
        return x.u == v ? x.v : x.u;
    }

    // Sinks at 0, every node as high as its edges allow.
    void LongestPath() {
        std::vector<char> state(static_cast<size_t>(n_), 0);
        std::vector<std::pair<int, size_t>> stack;
        for (int s = 0; s < n_; ++s) {
            if (state[static_cast<size_t>(s)]) continue;
            stack.push_back({s, 0});
            state[static_cast<size_t>(s)] = 1;
            while (!stack.empty()) {
                auto& [v, i] = stack.back();
                const auto& a = adj_[static_cast<size_t>(v)];
                bool pushed = false;
                while (i < a.size()) {
                    const int e = a[i++];
                    const int w = e_[static_cast<size_t>(e)].v;
                    if (e_[static_cast<size_t>(e)].u != v || state[static_cast<size_t>(w)]) continue;
                    state[static_cast<size_t>(w)] = 1;
                    stack.push_back({w, 0});
                    pushed = true;
                    break;
                }
                if (pushed) continue;
                int r = INT_MAX;
                for (int e : a) {
                    const RankEdge& x = e_[static_cast<size_t>(e)];
                    if (x.u == v) r = std::min(r, rank_[static_cast<size_t>(x.v)] - x.minlen);
                }
                rank_[static_cast<size_t>(v)] = r == INT_MAX ? 0 : r;
                stack.pop_back();
            }
        }
    }

    // A spanning tree of tight edges (slack 0), shifting ranks as needed.
    void FeasibleTree() {
        in_tree_.assign(e_.size(), 0);
        std::vector<char> node_in(static_cast<size_t>(n_), 0);
        node_in[0] = 1;
        int size = 1;
        std::vector<int> stack;
        auto grow = [&] {
            stack.clear();
            for (int v = 0; v < n_; ++v) {
                if (node_in[static_cast<size_t>(v)]) stack.push_back(v);
            }
            while (!stack.empty()) {
                const int v = stack.back();
                stack.pop_back();
                for (int e : adj_[static_cast<size_t>(v)]) {
                    --*work_;
                    const int w = Other(e, v);
                    if (node_in[static_cast<size_t>(w)] || Slack(e) != 0) continue;
                    node_in[static_cast<size_t>(w)] = 1;
                    in_tree_[static_cast<size_t>(e)] = 1;
                    ++size;
                    stack.push_back(w);
                }
            }
        };
        grow();
        while (size < n_) {
            int best = -1, best_slack = INT_MAX;
            for (int e = 0; e < static_cast<int>(e_.size()); ++e) {
                const RankEdge& x = e_[static_cast<size_t>(e)];
                if (node_in[static_cast<size_t>(x.u)] == node_in[static_cast<size_t>(x.v)]) continue;
                const int s = Slack(e);
                if (s < best_slack) {
                    best_slack = s;
                    best = e;
                }
            }
            *work_ -= static_cast<long long>(e_.size());
            if (best < 0) break;  // not connected (the caller splits components)
            const int delta = node_in[static_cast<size_t>(e_[static_cast<size_t>(best)].u)] ? best_slack : -best_slack;
            for (int v = 0; v < n_; ++v) {
                if (node_in[static_cast<size_t>(v)]) rank_[static_cast<size_t>(v)] += delta;
            }
            grow();
        }
    }

    // Postorder numbers (low, lim) and parents from the root, node 0.
    void LowLim() {
        low_.assign(static_cast<size_t>(n_), 0);
        lim_.assign(static_cast<size_t>(n_), 0);
        parent_.assign(static_cast<size_t>(n_), -1);
        parent_edge_.assign(static_cast<size_t>(n_), -1);
        order_.clear();
        std::vector<char> seen(static_cast<size_t>(n_), 0);
        std::vector<std::pair<int, size_t>> stack{{0, 0}};
        seen[0] = 1;
        int next = 1;
        low_[0] = 1;
        while (!stack.empty()) {
            auto& [v, i] = stack.back();
            const auto& a = adj_[static_cast<size_t>(v)];
            bool pushed = false;
            while (i < a.size()) {
                const int e = a[i++];
                if (!in_tree_[static_cast<size_t>(e)]) continue;
                const int w = Other(e, v);
                if (seen[static_cast<size_t>(w)]) continue;
                seen[static_cast<size_t>(w)] = 1;
                parent_[static_cast<size_t>(w)] = v;
                parent_edge_[static_cast<size_t>(w)] = e;
                low_[static_cast<size_t>(w)] = next;
                stack.push_back({w, 0});
                pushed = true;
                break;
            }
            if (pushed) continue;
            lim_[static_cast<size_t>(v)] = next++;
            order_.push_back(v);  // postorder
            stack.pop_back();
        }
        *work_ -= n_;
    }

    // Cut value of each tree edge, children before parents.
    void CutValues() {
        cut_.assign(e_.size(), 0.f);
        for (int v : order_) {
            if (parent_[static_cast<size_t>(v)] < 0) continue;
            const int p = parent_[static_cast<size_t>(v)];
            const int te = parent_edge_[static_cast<size_t>(v)];
            const bool child_is_tail = e_[static_cast<size_t>(te)].u == v;
            float cut = e_[static_cast<size_t>(te)].weight;
            for (int e : adj_[static_cast<size_t>(v)]) {
                --*work_;
                if (e == te) continue;
                const bool out = e_[static_cast<size_t>(e)].u == v;
                const int other = out ? e_[static_cast<size_t>(e)].v : e_[static_cast<size_t>(e)].u;
                if (other == p) continue;
                const bool to_head = out == child_is_tail;
                const float w = e_[static_cast<size_t>(e)].weight;
                cut += to_head ? w : -w;
                if (in_tree_[static_cast<size_t>(e)]) cut += to_head ? -cut_[static_cast<size_t>(e)] : cut_[static_cast<size_t>(e)];
            }
            cut_[static_cast<size_t>(te)] = cut;
        }
    }

    int LeaveEdge() const {
        for (int e = 0; e < static_cast<int>(e_.size()); ++e) {
            if (in_tree_[static_cast<size_t>(e)] && cut_[static_cast<size_t>(e)] < -1e-6f) return e;
        }
        return -1;
    }

    bool Descendant(int v, int root) const {
        return low_[static_cast<size_t>(root)] <= lim_[static_cast<size_t>(v)] &&
               lim_[static_cast<size_t>(v)] <= lim_[static_cast<size_t>(root)];
    }

    int EnterEdge(int leave) {
        int tail = e_[static_cast<size_t>(leave)].u, head = e_[static_cast<size_t>(leave)].v;
        // The subtree cut off by the edge is below its lower end.
        bool flip = false;
        int sub = tail;
        if (lim_[static_cast<size_t>(tail)] > lim_[static_cast<size_t>(head)]) {
            sub = head;
            flip = true;
        }
        int best = -1, best_slack = INT_MAX;
        for (int e = 0; e < static_cast<int>(e_.size()); ++e) {
            const RankEdge& x = e_[static_cast<size_t>(e)];
            if (flip == Descendant(x.u, sub) && flip != Descendant(x.v, sub)) {
                const int s = Slack(e);
                if (s < best_slack) {
                    best_slack = s;
                    best = e;
                }
            }
        }
        *work_ -= static_cast<long long>(e_.size());
        return best;
    }

    void UpdateRanks() {
        // Preorder = reverse postorder.
        for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
            const int v = *it;
            const int p = parent_[static_cast<size_t>(v)];
            if (p < 0) continue;
            const RankEdge& x = e_[static_cast<size_t>(parent_edge_[static_cast<size_t>(v)])];
            rank_[static_cast<size_t>(v)] = rank_[static_cast<size_t>(p)] + (x.u == p ? x.minlen : -x.minlen);
        }
    }

    int n_;
    std::vector<RankEdge> e_;
    long long* work_;
    std::vector<int> rank_;
    std::vector<std::vector<int>> adj_;
    std::vector<char> in_tree_;
    std::vector<int> low_, lim_, parent_, parent_edge_, order_;
    std::vector<float> cut_;
};

// ---------------------------------------------------------------------------
// The working graph: real nodes, cluster borders, dummies on long edges.
// ---------------------------------------------------------------------------

enum class Kind : uint8_t {
    Real,
    Dummy,    // a bend or the label of an edge spanning ranks
    Top,      // a cluster's first rank (holds its title band)
    Bottom,   // its last rank
    Left,     // its left side, on each of its ranks
    Right,
};

struct WNode {
    float w = 0, h = 0;
    Kind kind = Kind::Real;
    int owner = -1;           // innermost cluster it is inside
    int rank = 0;
    int order = 0;
    float x = 0, y = 0;
};

struct WEdge {  // between adjacent ranks, upper end first
    int u, v;
    float weight;
};

// Crossings between two adjacent layers (Barth, Juenger & Mutzel).
float CountCrossings(const std::vector<int>& north, const std::vector<int>& south,
                     const std::vector<std::vector<std::pair<int, float>>>& down, const std::vector<WNode>& nodes) {
    if (north.empty() || south.empty()) return 0;
    std::vector<std::pair<int, float>> entries;  // south positions in north order
    for (int u : north) {
        const size_t start = entries.size();
        for (const auto& [v, w] : down[static_cast<size_t>(u)]) entries.push_back({nodes[static_cast<size_t>(v)].order, w});
        std::sort(entries.begin() + static_cast<std::ptrdiff_t>(start), entries.end());
    }
    size_t first = 1;
    while (first < south.size()) first <<= 1;
    const size_t size = 2 * first - 1;
    first -= 1;
    std::vector<float> tree(size, 0.f);
    float cc = 0;
    for (const auto& [pos, w] : entries) {
        size_t index = static_cast<size_t>(pos) + first;
        tree[index] += w;
        float sum = 0;
        while (index > 0) {
            if (index % 2) sum += tree[index + 1];
            index = (index - 1) >> 1;
            tree[index] += w;
        }
        cc += w * sum;
    }
    return cc;
}

// Entries sorted by barycenter; those without one keep their place (dagre's
// sort, ties broken towards the side of `bias_right`).
struct SortEntry {
    float bc = 0;
    bool has = false;
    int i = 0;        // position before sorting
    int payload = 0;
};

void SortEntries(std::vector<SortEntry>& entries, bool bias_right) {
    std::vector<SortEntry> sortable, unsortable;
    for (const SortEntry& e : entries) (e.has ? sortable : unsortable).push_back(e);
    std::stable_sort(sortable.begin(), sortable.end(), [bias_right](const SortEntry& a, const SortEntry& b) {
        if (a.bc != b.bc) return a.bc < b.bc;
        return bias_right ? a.i > b.i : a.i < b.i;
    });
    std::sort(unsortable.begin(), unsortable.end(), [](const SortEntry& a, const SortEntry& b) { return a.i > b.i; });
    std::vector<SortEntry> out;
    out.reserve(entries.size());
    auto consume = [&] {
        while (!unsortable.empty() && unsortable.back().i <= static_cast<int>(out.size())) {
            out.push_back(unsortable.back());
            unsortable.pop_back();
        }
    };
    consume();
    for (const SortEntry& e : sortable) {
        out.push_back(e);
        consume();
    }
    while (!unsortable.empty()) {
        out.push_back(unsortable.back());
        unsortable.pop_back();
    }
    entries = std::move(out);
}

// Weighted median of values (the average of the two middle ones when the
// weight splits evenly).
float WeightedMedian(std::vector<std::pair<float, float>>& vw) {
    std::sort(vw.begin(), vw.end());
    float total = 0;
    for (const auto& p : vw) total += p.second;
    float acc = 0;
    for (size_t i = 0; i < vw.size(); ++i) {
        acc += vw[i].second;
        if (acc > total * 0.5f + 1e-4f) return vw[i].first;
        if (std::fabs(acc - total * 0.5f) <= 1e-4f && i + 1 < vw.size()) return (vw[i].first + vw[i + 1].first) * 0.5f;
    }
    return vw.back().first;
}

// Positions closest to `desired` (weighted least squares) keeping each node at
// least `sep[i]` right of the one before: isotonic regression by pooling
// adjacent violators on the targets desired - offset.
void PlaceLayer(const std::vector<float>& desired, const std::vector<float>& weight, const std::vector<float>& sep,
                std::vector<float>& x) {
    const size_t n = desired.size();
    std::vector<float> offset(n, 0.f);
    for (size_t i = 1; i < n; ++i) offset[i] = offset[i - 1] + sep[i - 1];
    struct Block {
        double w, wt;
        size_t count;
    };
    std::vector<Block> blocks;
    for (size_t i = 0; i < n; ++i) {
        const double w = std::max(weight[i], 1e-3f);
        blocks.push_back({w, w * (desired[i] - offset[i]), 1});
        while (blocks.size() > 1) {
            const Block& b = blocks.back();
            const Block& a = blocks[blocks.size() - 2];
            if (a.wt / a.w <= b.wt / b.w) break;
            const Block merged{a.w + b.w, a.wt + b.wt, a.count + b.count};
            blocks.pop_back();
            blocks.back() = merged;
        }
    }
    x.resize(n);
    size_t i = 0;
    for (const Block& b : blocks) {
        const float y = static_cast<float>(b.wt / b.w);
        for (size_t k = 0; k < b.count; ++k, ++i) x[i] = y + offset[i];
    }
}

}  // namespace

LayeredLayout LayoutLayered(const LayeredGraph& g) {
    LayeredLayout out;
    const int n = static_cast<int>(g.nodes.size());
    const int nc = static_cast<int>(g.clusters.size());
    out.centers.assign(static_cast<size_t>(n), D2D1::Point2F(0, 0));
    out.bends.assign(g.edges.size(), {});
    out.labels.assign(g.edges.size(), D2D1::Point2F(0, 0));
    out.clusters.assign(static_cast<size_t>(nc), D2D1::RectF(0, 0, 0, 0));
    if (n == 0) return out;
    const bool sideways = g.dir == RankDir::LR || g.dir == RankDir::RL;
    const float pad = g.cluster_pad;

    // Clusters: parents come first; those without nodes are left out.
    std::vector<int> parent(static_cast<size_t>(nc), -1), depth(static_cast<size_t>(nc), 0);
    for (int c = 0; c < nc; ++c) {
        const int p = g.clusters[static_cast<size_t>(c)].parent;
        parent[static_cast<size_t>(c)] = p >= 0 && p < c ? p : -1;
        depth[static_cast<size_t>(c)] = parent[static_cast<size_t>(c)] < 0 ? 1 : depth[static_cast<size_t>(parent[static_cast<size_t>(c)])] + 1;
    }
    auto cluster_of = [&](int v) {
        const int c = g.nodes[static_cast<size_t>(v)].cluster;
        return c >= 0 && c < nc ? c : -1;
    };
    std::vector<char> used(static_cast<size_t>(nc), 0);
    for (int v = 0; v < n; ++v) {
        for (int c = cluster_of(v); c >= 0 && !used[static_cast<size_t>(c)]; c = parent[static_cast<size_t>(c)]) used[static_cast<size_t>(c)] = 1;
    }
    auto lca = [&](int a, int b) {
        while (a != b) {
            if (a < 0 || b < 0) return -1;
            if (depth[static_cast<size_t>(a)] >= depth[static_cast<size_t>(b)]) a = parent[static_cast<size_t>(a)];
            else b = parent[static_cast<size_t>(b)];
        }
        return a;
    };
    // Parts not connected to each other (clusters hold theirs together) are
    // laid out on their own and put side by side, aligned on their first
    // rank: a tall node of one doesn't stretch the ranks of the other.
    {
        std::vector<int> uf(static_cast<size_t>(n));
        std::iota(uf.begin(), uf.end(), 0);
        auto find = [&](int v) {
            while (uf[static_cast<size_t>(v)] != v) v = uf[static_cast<size_t>(v)] = uf[static_cast<size_t>(uf[static_cast<size_t>(v)])];
            return v;
        };
        auto unite = [&](int a, int b) { uf[static_cast<size_t>(find(a))] = find(b); };
        for (const auto& e : g.edges) {
            if (e.from >= 0 && e.to >= 0 && e.from < n && e.to < n) unite(e.from, e.to);
        }
        std::vector<int> first_in(static_cast<size_t>(nc), -1);
        for (int v = 0; v < n; ++v) {
            int c = cluster_of(v);
            if (c < 0) continue;
            while (parent[static_cast<size_t>(c)] >= 0) c = parent[static_cast<size_t>(c)];
            if (first_in[static_cast<size_t>(c)] < 0) first_in[static_cast<size_t>(c)] = v;
            else unite(v, first_in[static_cast<size_t>(c)]);
        }
        std::vector<int> comp_of(static_cast<size_t>(n), -1), roots;
        for (int v = 0; v < n; ++v) {
            const int r = find(v);
            auto it = std::find(roots.begin(), roots.end(), r);
            if (it == roots.end()) {
                comp_of[static_cast<size_t>(v)] = static_cast<int>(roots.size());
                roots.push_back(r);
            } else {
                comp_of[static_cast<size_t>(v)] = static_cast<int>(it - roots.begin());
            }
            if (roots.size() > 64) break;  // many parts: one layout (packing them is not worth it)
        }
        if (roots.size() > 1 && roots.size() <= 64) {
            std::vector<LayeredLayout> parts(roots.size());
            std::vector<std::vector<int>> node_map(roots.size()), cluster_map(roots.size()), edge_map(roots.size());
            for (size_t p = 0; p < roots.size(); ++p) {
                LayeredGraph sub;
                sub.dir = g.dir;
                sub.node_sep = g.node_sep;
                sub.rank_sep = g.rank_sep;
                sub.edge_sep = g.edge_sep;
                sub.cluster_pad = g.cluster_pad;
                std::vector<int> local(static_cast<size_t>(n), -1), local_c(static_cast<size_t>(nc), -1);
                std::vector<char> need(static_cast<size_t>(nc), 0);
                for (int v = 0; v < n; ++v) {
                    if (comp_of[static_cast<size_t>(v)] != static_cast<int>(p)) continue;
                    for (int c = cluster_of(v); c >= 0 && !need[static_cast<size_t>(c)]; c = parent[static_cast<size_t>(c)]) need[static_cast<size_t>(c)] = 1;
                }
                for (int c = 0; c < nc; ++c) {
                    if (!need[static_cast<size_t>(c)]) continue;
                    local_c[static_cast<size_t>(c)] = static_cast<int>(sub.clusters.size());
                    LayeredGraph::Cluster cl = g.clusters[static_cast<size_t>(c)];
                    cl.parent = parent[static_cast<size_t>(c)] >= 0 ? local_c[static_cast<size_t>(parent[static_cast<size_t>(c)])] : -1;
                    sub.clusters.push_back(cl);
                    cluster_map[p].push_back(c);
                }
                for (int v = 0; v < n; ++v) {
                    if (comp_of[static_cast<size_t>(v)] != static_cast<int>(p)) continue;
                    local[static_cast<size_t>(v)] = static_cast<int>(sub.nodes.size());
                    LayeredGraph::Node nd = g.nodes[static_cast<size_t>(v)];
                    nd.cluster = cluster_of(v) >= 0 ? local_c[static_cast<size_t>(cluster_of(v))] : -1;
                    sub.nodes.push_back(nd);
                    node_map[p].push_back(v);
                }
                for (size_t i = 0; i < g.edges.size(); ++i) {
                    const auto& e = g.edges[i];
                    if (e.from < 0 || e.to < 0 || e.from >= n || e.to >= n || comp_of[static_cast<size_t>(e.from)] != static_cast<int>(p)) continue;
                    LayeredGraph::Edge se = e;
                    se.from = local[static_cast<size_t>(e.from)];
                    se.to = local[static_cast<size_t>(e.to)];
                    sub.edges.push_back(se);
                    edge_map[p].push_back(static_cast<int>(i));
                }
                parts[p] = LayoutLayered(sub);
            }
            float along = 0, across = 0;  // along: where parts line up; across: their common extent
            for (const LayeredLayout& part : parts) across = std::max(across, sideways ? part.width : part.height);
            for (size_t p = 0; p < parts.size(); ++p) {
                const LayeredLayout& part = parts[p];
                float dx = 0, dy = 0;
                if (sideways) {
                    dy = along;
                    dx = g.dir == RankDir::RL ? across - part.width : 0.f;
                    along += part.height + g.node_sep;
                } else {
                    dx = along;
                    dy = g.dir == RankDir::BT ? across - part.height : 0.f;
                    along += part.width + g.node_sep;
                }
                auto shift = [&](D2D1_POINT_2F q) { return D2D1::Point2F(q.x + dx, q.y + dy); };
                for (size_t k = 0; k < node_map[p].size(); ++k) out.centers[static_cast<size_t>(node_map[p][k])] = shift(part.centers[k]);
                for (size_t k = 0; k < cluster_map[p].size(); ++k) {
                    const D2D1_RECT_F& r = part.clusters[k];
                    out.clusters[static_cast<size_t>(cluster_map[p][k])] = D2D1::RectF(r.left + dx, r.top + dy, r.right + dx, r.bottom + dy);
                }
                for (size_t k = 0; k < edge_map[p].size(); ++k) {
                    const size_t ei = static_cast<size_t>(edge_map[p][k]);
                    for (const D2D1_POINT_2F& q : part.bends[k]) out.bends[ei].push_back(shift(q));
                    out.labels[ei] = shift(part.labels[k]);
                }
            }
            along -= g.node_sep;
            out.width = sideways ? across : along;
            out.height = sideways ? along : across;
            return out;
        }
    }

    // Title bands: at the top of the box on screen.
    auto band = [&](int c) {
        const auto& cl = g.clusters[static_cast<size_t>(c)];
        return cl.title_h > 0 ? cl.title_h + pad * 0.5f : 0.f;
    };

    // Every edge spans at least two ranks: its middle rank holds its label
    // or a bend, as dagre does it (and so the rank gap is halved).
    std::vector<WNode> nodes(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const auto& s = g.nodes[static_cast<size_t>(i)];
        WNode& d = nodes[static_cast<size_t>(i)];
        d.w = sideways ? s.h : s.w;
        d.h = sideways ? s.w : s.h;
        d.owner = cluster_of(i);
    }
    std::vector<int> top(static_cast<size_t>(nc), -1), bottom(static_cast<size_t>(nc), -1);
    for (int c = 0; c < nc; ++c) {
        if (!used[static_cast<size_t>(c)]) continue;
        WNode t;
        t.kind = Kind::Top;
        t.owner = c;
        t.h = g.dir == RankDir::TB && band(c) > 0 ? band(c) + pad * 0.5f : pad;
        t.w = sideways ? 0.f : g.clusters[static_cast<size_t>(c)].title_w;
        top[static_cast<size_t>(c)] = static_cast<int>(nodes.size());
        nodes.push_back(t);
        WNode b;
        b.kind = Kind::Bottom;
        b.owner = c;
        b.h = g.dir == RankDir::BT && band(c) > 0 ? band(c) + pad * 0.5f : pad;
        b.w = g.dir == RankDir::BT ? g.clusters[static_cast<size_t>(c)].title_w : 0.f;
        bottom[static_cast<size_t>(c)] = static_cast<int>(nodes.size());
        nodes.push_back(b);
    }
    const int ranked = static_cast<int>(nodes.size());  // real nodes and cluster tops/bottoms
    const float rank_sep = g.rank_sep / 2;

    // Break cycles: edges back to a node still on the DFS path turn around.
    const size_t ne = g.edges.size();
    std::vector<char> reversed(ne, 0), valid(ne, 0);
    std::vector<std::vector<int>> out_edges(static_cast<size_t>(n));
    for (size_t i = 0; i < ne; ++i) {
        const auto& e = g.edges[i];
        if (e.from < 0 || e.to < 0 || e.from >= n || e.to >= n || e.from == e.to) continue;
        valid[i] = 1;
        out_edges[static_cast<size_t>(e.from)].push_back(static_cast<int>(i));
    }
    {
        std::vector<char> state(static_cast<size_t>(n), 0);
        std::vector<std::pair<int, size_t>> stack;
        for (int s = 0; s < n; ++s) {
            if (state[static_cast<size_t>(s)]) continue;
            state[static_cast<size_t>(s)] = 1;
            stack.push_back({s, 0});
            while (!stack.empty()) {
                auto& [v, i] = stack.back();
                const auto& a = out_edges[static_cast<size_t>(v)];
                if (i < a.size()) {
                    const int e = a[i++];
                    const int w = g.edges[static_cast<size_t>(e)].to;
                    if (state[static_cast<size_t>(w)] == 1) {
                        reversed[static_cast<size_t>(e)] = 1;
                    } else if (state[static_cast<size_t>(w)] == 0) {
                        state[static_cast<size_t>(w)] = 1;
                        stack.push_back({w, 0});
                    }
                    continue;
                }
                state[static_cast<size_t>(v)] = 2;
                stack.pop_back();
            }
        }
    }
    auto upper = [&](size_t i) { return reversed[i] ? g.edges[i].to : g.edges[i].from; };
    auto lower = [&](size_t i) { return reversed[i] ? g.edges[i].from : g.edges[i].to; };

    // Ranks, per connected component (parallel edges merged). Clusters wrap
    // their members: top border -> member -> bottom border.
    {
        std::vector<RankEdge> all;
        for (size_t i = 0; i < ne; ++i) {
            if (valid[i]) all.push_back({upper(i), lower(i), std::max(1, g.edges[i].minlen) * 2, g.edges[i].weight});
        }
        for (int v = 0; v < n; ++v) {
            const int c = cluster_of(v);
            if (c < 0) continue;
            all.push_back({top[static_cast<size_t>(c)], v, 1, 1.f});
            all.push_back({v, bottom[static_cast<size_t>(c)], 1, 1.f});
        }
        for (int c = 0; c < nc; ++c) {
            const int p = parent[static_cast<size_t>(c)];
            if (!used[static_cast<size_t>(c)] || p < 0) continue;
            all.push_back({top[static_cast<size_t>(p)], top[static_cast<size_t>(c)], 1, 1.f});
            all.push_back({bottom[static_cast<size_t>(c)], bottom[static_cast<size_t>(p)], 1, 1.f});
        }
        std::vector<int> comp(static_cast<size_t>(ranked), -1);
        std::vector<std::vector<int>> adj(static_cast<size_t>(ranked));
        for (const RankEdge& e : all) {
            adj[static_cast<size_t>(e.u)].push_back(e.v);
            adj[static_cast<size_t>(e.v)].push_back(e.u);
        }
        int comps = 0;
        for (int s = 0; s < ranked; ++s) {
            if (comp[static_cast<size_t>(s)] >= 0 || (s >= n && !used[static_cast<size_t>(nodes[static_cast<size_t>(s)].owner)])) continue;
            std::vector<int> stack{s};
            comp[static_cast<size_t>(s)] = comps;
            while (!stack.empty()) {
                const int v = stack.back();
                stack.pop_back();
                for (int w : adj[static_cast<size_t>(v)]) {
                    if (comp[static_cast<size_t>(w)] < 0) {
                        comp[static_cast<size_t>(w)] = comps;
                        stack.push_back(w);
                    }
                }
            }
            ++comps;
        }
        std::vector<std::vector<int>> members(static_cast<size_t>(comps));
        std::vector<int> local(static_cast<size_t>(ranked), 0);
        for (int v = 0; v < ranked; ++v) {
            if (comp[static_cast<size_t>(v)] < 0) continue;
            local[static_cast<size_t>(v)] = static_cast<int>(members[static_cast<size_t>(comp[static_cast<size_t>(v)])].size());
            members[static_cast<size_t>(comp[static_cast<size_t>(v)])].push_back(v);
        }
        std::vector<std::map<std::pair<int, int>, size_t>> merged(static_cast<size_t>(comps));
        std::vector<std::vector<RankEdge>> edges(static_cast<size_t>(comps));
        for (const RankEdge& e : all) {
            const int c = comp[static_cast<size_t>(e.u)];
            auto key = std::make_pair(local[static_cast<size_t>(e.u)], local[static_cast<size_t>(e.v)]);
            auto& m = merged[static_cast<size_t>(c)];
            auto it = m.find(key);
            if (it == m.end()) {
                m.emplace(key, edges[static_cast<size_t>(c)].size());
                edges[static_cast<size_t>(c)].push_back({key.first, key.second, e.minlen, e.weight});
            } else {
                RankEdge& re = edges[static_cast<size_t>(c)][it->second];
                re.minlen = std::max(re.minlen, e.minlen);
                re.weight += e.weight;
            }
        }
        long long work = kSimplexWork;
        for (int c = 0; c < comps; ++c) {
            const auto& mem = members[static_cast<size_t>(c)];
            NetworkSimplex ns(static_cast<int>(mem.size()), std::move(edges[static_cast<size_t>(c)]), &work);
            const std::vector<int> r = ns.Run();
            for (size_t k = 0; k < mem.size(); ++k) nodes[static_cast<size_t>(mem[k])].rank = r[k];
        }
    }

    // Dummies along edges spanning ranks (inside the smallest cluster
    // holding both ends); the middle one of a labelled edge is as big as its
    // label.
    std::vector<std::vector<int>> chains(ne);  // dummies from the upper end down
    std::vector<int> label_node(ne, -1);
    std::vector<WEdge> wedges;
    for (size_t i = 0; i < ne; ++i) {
        if (!valid[i]) continue;
        const int u = upper(i), v = lower(i);
        const int ru = nodes[static_cast<size_t>(u)].rank, rv = nodes[static_cast<size_t>(v)].rank;
        const int mid = ru + (rv - ru) / 2;
        const bool labelled = g.edges[i].label_w > 0 && g.edges[i].label_h > 0;
        const float weight = g.edges[i].weight;
        const int owner = lca(cluster_of(u), cluster_of(v));
        int prev = u;
        for (int r = ru + 1; r < rv; ++r) {
            WNode d;
            d.kind = Kind::Dummy;
            d.owner = owner;
            d.rank = r;
            if (labelled && r == mid) {
                d.w = sideways ? g.edges[i].label_h : g.edges[i].label_w;
                d.h = sideways ? g.edges[i].label_w : g.edges[i].label_h;
                label_node[i] = static_cast<int>(nodes.size());
            }
            nodes.push_back(d);
            const int id = static_cast<int>(nodes.size()) - 1;
            chains[i].push_back(id);
            wedges.push_back({prev, id, weight});
            prev = id;
        }
        wedges.push_back({prev, v, weight});
    }
    // Cluster sides on every rank they span.
    std::vector<std::vector<int>> lefts(static_cast<size_t>(nc)), rights(static_cast<size_t>(nc));
    for (int c = 0; c < nc; ++c) {
        if (!used[static_cast<size_t>(c)]) continue;
        const int r0 = nodes[static_cast<size_t>(top[static_cast<size_t>(c)])].rank;
        const int r1 = nodes[static_cast<size_t>(bottom[static_cast<size_t>(c)])].rank;
        for (int r = r0; r <= r1; ++r) {
            WNode l;
            l.kind = Kind::Left;
            l.owner = c;
            l.rank = r;
            lefts[static_cast<size_t>(c)].push_back(static_cast<int>(nodes.size()));
            nodes.push_back(l);
            WNode rt = l;
            rt.kind = Kind::Right;
            rights[static_cast<size_t>(c)].push_back(static_cast<int>(nodes.size()));
            nodes.push_back(rt);
        }
    }
    const int total = static_cast<int>(nodes.size());
    std::vector<std::vector<std::pair<int, float>>> up(static_cast<size_t>(total)), down(static_cast<size_t>(total));
    for (const WEdge& e : wedges) {
        down[static_cast<size_t>(e.u)].push_back({e.v, e.weight});
        up[static_cast<size_t>(e.v)].push_back({e.u, e.weight});
    }
    auto kind = [&](int v) { return nodes[static_cast<size_t>(v)].kind; };

    int max_rank = 0;
    for (const WNode& d : nodes) max_rank = std::max(max_rank, d.rank);
    std::vector<std::vector<int>> layers(static_cast<size_t>(max_rank) + 1);

    // Orders a layer with the nodes of each cluster together, between its
    // sides; entries sorted by barycenter at every level of nesting.
    std::vector<int> block_of(static_cast<size_t>(nc) + 1, -1);
    struct Block {
        int cluster = -1;
        int left = -1, right = -1;
        std::vector<int> nodes;     // members directly in it
        std::vector<int> children;  // blocks of clusters directly in it
        float bc_sum = 0;
        float bc_n = 0;
        int pos = INT_MAX;
    };
    std::vector<Block> blocks;
    // Order of each cluster among its siblings in the last rank arranged.
    std::vector<int> sibling_rank(static_cast<size_t>(nc), -1);
    std::vector<int> sibling_count(static_cast<size_t>(nc) + 1, 0);
    auto record_siblings = [&](const std::vector<int>& layer) {
        std::fill(sibling_count.begin(), sibling_count.end(), 0);
        for (int v : layer) {
            const WNode& d = nodes[static_cast<size_t>(v)];
            if (d.kind != Kind::Left) continue;
            sibling_rank[static_cast<size_t>(d.owner)] = sibling_count[static_cast<size_t>(parent[static_cast<size_t>(d.owner)] + 1)]++;
        }
    };
    auto arrange = [&](std::vector<int>& layer, const std::vector<float>& bary, const std::vector<char>& has,
                       bool bias_right) {
        if (nc == 0) {
            std::vector<SortEntry> entries;
            for (int i = 0; i < static_cast<int>(layer.size()); ++i) {
                const int v = layer[static_cast<size_t>(i)];
                entries.push_back({bary[static_cast<size_t>(v)], has[static_cast<size_t>(v)] != 0, i, v});
            }
            SortEntries(entries, bias_right);
            for (size_t i = 0; i < entries.size(); ++i) layer[i] = entries[i].payload;
            return;
        }
        blocks.clear();
        std::vector<int> touched;
        auto block = [&](int c) -> int {
            int& b = block_of[static_cast<size_t>(c + 1)];
            if (b < 0) {
                b = static_cast<int>(blocks.size());
                blocks.push_back(Block{});
                blocks.back().cluster = c;
                touched.push_back(c);
                if (c >= 0) {
                    const int pb = [&] {
                        // parent block (created on demand)
                        int& p = block_of[static_cast<size_t>(parent[static_cast<size_t>(c)] + 1)];
                        if (p < 0) {
                            p = static_cast<int>(blocks.size());
                            blocks.push_back(Block{});
                            blocks.back().cluster = parent[static_cast<size_t>(c)];
                            touched.push_back(parent[static_cast<size_t>(c)]);
                        }
                        return p;
                    }();
                    (void)pb;
                }
            }
            return b;
        };
        // Blocks exist for every cluster on this rank; link them.
        block(-1);
        for (int v : layer) {
            const WNode& d = nodes[static_cast<size_t>(v)];
            const int b = block(d.owner);
            Block& bl = blocks[static_cast<size_t>(b)];
            bl.pos = std::min(bl.pos, d.order);
            if (d.kind == Kind::Left) bl.left = v;
            else if (d.kind == Kind::Right) bl.right = v;
            else {
                bl.nodes.push_back(v);
                if (has[static_cast<size_t>(v)]) {
                    bl.bc_sum += bary[static_cast<size_t>(v)];
                    bl.bc_n += 1;
                }
            }
        }
        // Ancestors of every block, and the tree links (deepest first so
        // barycenters and positions add up).
        for (size_t k = 0; k < touched.size(); ++k) {
            for (int c = touched[k]; c >= 0; c = parent[static_cast<size_t>(c)]) block(parent[static_cast<size_t>(c)]);
        }
        std::vector<int> order_by_depth(blocks.size());
        std::iota(order_by_depth.begin(), order_by_depth.end(), 0);
        std::sort(order_by_depth.begin(), order_by_depth.end(), [&](int a, int b) {
            const int ca = blocks[static_cast<size_t>(a)].cluster, cb = blocks[static_cast<size_t>(b)].cluster;
            return (ca < 0 ? 0 : depth[static_cast<size_t>(ca)]) > (cb < 0 ? 0 : depth[static_cast<size_t>(cb)]);
        });
        for (int b : order_by_depth) {
            const Block& bl = blocks[static_cast<size_t>(b)];
            if (bl.cluster < 0) continue;
            Block& pb = blocks[static_cast<size_t>(block_of[static_cast<size_t>(parent[static_cast<size_t>(bl.cluster)] + 1)])];
            pb.children.push_back(b);
            pb.bc_sum += bl.bc_sum;
            pb.bc_n += bl.bc_n;
            pb.pos = std::min(pb.pos, bl.pos);
        }
        // Sort each block's entries, then flatten from the root.
        std::vector<int> result;
        result.reserve(layer.size());
        std::vector<SortEntry> entries;
        auto flatten = [&](auto&& self, int b) -> void {
            Block& bl = blocks[static_cast<size_t>(b)];
            if (bl.left >= 0) result.push_back(bl.left);
            std::vector<std::pair<int, int>> items;  // (position, entry): node >= 0, block -(b+1)
            for (int v : bl.nodes) items.push_back({nodes[static_cast<size_t>(v)].order, v});
            for (int c : bl.children) items.push_back({blocks[static_cast<size_t>(c)].pos, -(c + 1)});
            std::sort(items.begin(), items.end());
            entries.clear();
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                const int e = items[static_cast<size_t>(i)].second;
                if (e >= 0) {
                    entries.push_back({bary[static_cast<size_t>(e)], has[static_cast<size_t>(e)] != 0, i, e});
                } else {
                    const Block& cb = blocks[static_cast<size_t>(-e - 1)];
                    entries.push_back({cb.bc_n > 0 ? cb.bc_sum / cb.bc_n : 0.f, cb.bc_n > 0, i, e});
                }
            }
            SortEntries(entries, bias_right);
            // Sibling clusters keep the order they had in the rank before
            // (a box can't cross another one between two ranks).
            {
                std::vector<size_t> slots;
                std::vector<int> kept;
                for (size_t k = 0; k < entries.size(); ++k) {
                    const int p = entries[k].payload;
                    if (p < 0 && sibling_rank[static_cast<size_t>(blocks[static_cast<size_t>(-p - 1)].cluster)] >= 0) {
                        slots.push_back(k);
                        kept.push_back(p);
                    }
                }
                std::stable_sort(kept.begin(), kept.end(), [&](int a, int b) {
                    return sibling_rank[static_cast<size_t>(blocks[static_cast<size_t>(-a - 1)].cluster)] <
                           sibling_rank[static_cast<size_t>(blocks[static_cast<size_t>(-b - 1)].cluster)];
                });
                for (size_t k = 0; k < slots.size(); ++k) entries[slots[k]].payload = kept[k];
            }
            const std::vector<SortEntry> sorted = entries;
            for (const SortEntry& s : sorted) {
                if (s.payload >= 0) result.push_back(s.payload);
                else self(self, -s.payload - 1);
            }
            if (bl.right >= 0) result.push_back(bl.right);
        };
        flatten(flatten, block_of[0]);
        for (int c : touched) block_of[static_cast<size_t>(c + 1)] = -1;
        layer = std::move(result);
        record_siblings(layer);
    };

    // Layers in DFS order from the top, then barycenter sweeps keeping the
    // order with the fewest crossings.
    {
        std::vector<int> by_rank(static_cast<size_t>(total));
        std::iota(by_rank.begin(), by_rank.end(), 0);
        std::stable_sort(by_rank.begin(), by_rank.end(),
                         [&](int a, int b) { return nodes[static_cast<size_t>(a)].rank < nodes[static_cast<size_t>(b)].rank; });
        std::vector<char> seen(static_cast<size_t>(total), 0);
        std::vector<int> stack;
        for (int s : by_rank) {
            if (seen[static_cast<size_t>(s)] || (s >= n && s < ranked && !used[static_cast<size_t>(nodes[static_cast<size_t>(s)].owner)])) continue;
            stack.push_back(s);
            while (!stack.empty()) {
                const int v = stack.back();
                stack.pop_back();
                if (seen[static_cast<size_t>(v)]) continue;
                seen[static_cast<size_t>(v)] = 1;
                layers[static_cast<size_t>(nodes[static_cast<size_t>(v)].rank)].push_back(v);
                const auto& d = down[static_cast<size_t>(v)];
                for (auto it = d.rbegin(); it != d.rend(); ++it) {
                    if (!seen[static_cast<size_t>(it->first)]) stack.push_back(it->first);
                }
            }
        }
    }
    auto number = [&] {
        for (auto& layer : layers) {
            for (int i = 0; i < static_cast<int>(layer.size()); ++i) nodes[static_cast<size_t>(layer[static_cast<size_t>(i)])].order = i;
        }
    };
    auto crossings = [&] {
        float cc = 0;
        for (size_t r = 0; r + 1 < layers.size(); ++r) cc += CountCrossings(layers[r], layers[r + 1], down, nodes);
        return cc;
    };
    number();
    std::vector<float> bary(static_cast<size_t>(total), 0.f);
    std::vector<char> has(static_cast<size_t>(total), 0);
    if (nc > 0) {
        // Clusters together from the start.
        std::fill(sibling_rank.begin(), sibling_rank.end(), -1);
        for (auto& layer : layers) {
            for (int v : layer) {
                bary[static_cast<size_t>(v)] = static_cast<float>(nodes[static_cast<size_t>(v)].order);
                has[static_cast<size_t>(v)] = 1;
            }
            arrange(layer, bary, has, false);
        }
        number();
    }
    {
        std::vector<std::vector<int>> best = layers;
        float best_cc = crossings();
        const int sweeps = total > 3000 ? 4 : kMaxSweeps;
        for (int i = 0, since_best = 0; i < sweeps && since_best < 4 && best_cc > 0; ++i, ++since_best) {
            const bool going_down = i % 2 == 1;
            const bool bias_right = i % 4 >= 2;
            const int r0 = going_down ? 1 : max_rank - 1;
            const int r1 = going_down ? max_rank + 1 : -1;
            const int step = going_down ? 1 : -1;
            if (nc > 0) {
                std::fill(sibling_rank.begin(), sibling_rank.end(), -1);
                if (r0 - step >= 0 && r0 - step <= max_rank) record_siblings(layers[static_cast<size_t>(r0 - step)]);
            }
            for (int r = r0; r != r1; r += step) {
                auto& layer = layers[static_cast<size_t>(r)];
                for (int v : layer) {
                    const auto& nb = going_down ? up[static_cast<size_t>(v)] : down[static_cast<size_t>(v)];
                    float sum = 0, wsum = 0;
                    for (const auto& [u, w] : nb) {
                        sum += w * static_cast<float>(nodes[static_cast<size_t>(u)].order);
                        wsum += w;
                    }
                    has[static_cast<size_t>(v)] = wsum > 0;
                    bary[static_cast<size_t>(v)] = wsum > 0 ? sum / wsum : 0.f;
                }
                arrange(layer, bary, has, bias_right);
                for (int k = 0; k < static_cast<int>(layer.size()); ++k) nodes[static_cast<size_t>(layer[static_cast<size_t>(k)])].order = k;
            }
            const float cc = crossings();
            if (cc < best_cc) {
                best_cc = cc;
                best = layers;
                since_best = -1;
            }
        }
        layers = std::move(best);
        number();
    }

    // Rank positions. A cluster's top hugs its first members (the bottom of
    // its rank), its bottom its last ones: tall neighbours in those ranks
    // don't open gaps in the box.
    {
        float y = 0;
        for (auto& layer : layers) {
            float h = 0;
            for (int v : layer) h = std::max(h, nodes[static_cast<size_t>(v)].h);
            for (int v : layer) {
                WNode& d = nodes[static_cast<size_t>(v)];
                d.y = d.kind == Kind::Top ? y + h - d.h / 2 : (d.kind == Kind::Bottom ? y + d.h / 2 : y + h / 2);
            }
            y += h + rank_sep;
        }
    }

    // Positions in the ranks: each node where its neighbours in the rank
    // above (or below) pull it, with long edges pulling hardest so they run
    // straight, no closer to the next node than allowed. Cluster sides pull
    // on their own continuation so boxes stay straight.
    auto half = [&](int v) { return nodes[static_cast<size_t>(v)].w / 2; };
    auto gap = [&](int v) { return kind(v) == Kind::Dummy ? g.edge_sep : g.node_sep; };
    auto side_band = [&](int c) { return sideways ? band(c) : 0.f; };
    auto sep = [&](int a, int b) {
        const Kind ka = kind(a), kb = kind(b);
        const bool sa = ka == Kind::Left || ka == Kind::Right, sb = kb == Kind::Left || kb == Kind::Right;
        if (ka == Kind::Left) return pad + side_band(nodes[static_cast<size_t>(a)].owner) + (sb ? 0.f : half(b));
        if (kb == Kind::Right) return (sa ? 0.f : half(a)) + pad;
        if (ka == Kind::Right && kb == Kind::Left) return g.node_sep * 0.5f;
        if (ka == Kind::Right) return half(b) + gap(b) * 0.5f;
        if (kb == Kind::Left) return half(a) + gap(a) * 0.5f;
        return half(a) + gap(a) / 2 + gap(b) / 2 + half(b);
    };
    std::vector<std::vector<std::pair<int, float>>> pup = up, pdown = down;
    for (int c = 0; c < nc; ++c) {
        for (const auto* side : {&lefts[static_cast<size_t>(c)], &rights[static_cast<size_t>(c)]}) {
            for (size_t k = 0; k + 1 < side->size(); ++k) {
                pdown[static_cast<size_t>((*side)[k])].push_back({(*side)[k + 1], 1.f});
                pup[static_cast<size_t>((*side)[k + 1])].push_back({(*side)[k], 1.f});
            }
        }
    }
    for (auto& layer : layers) {
        float x = 0;
        for (size_t i = 0; i < layer.size(); ++i) {
            if (i > 0) x += sep(layer[i - 1], layer[i]);
            nodes[static_cast<size_t>(layer[i])].x = x;
        }
        for (int v : layer) nodes[static_cast<size_t>(v)].x -= x / 2;
    }
    auto pull = [&](int v, int u, float w) {
        const bool dv = kind(v) != Kind::Real, du = kind(u) != Kind::Real;
        return w * (dv && du ? 8.f : (dv || du ? 2.f : 1.f));
    };
    std::vector<float> desired, weight, gaps, placed;
    std::vector<std::pair<float, float>> vw;
    auto place = [&](const std::vector<int>& layer, int mode) {  // 0 above, 1 below, 2 both
        desired.assign(layer.size(), 0.f);
        weight.assign(layer.size(), 0.f);
        gaps.assign(layer.size(), 0.f);
        for (size_t i = 0; i < layer.size(); ++i) {
            const int v = layer[i];
            float sum = 0, wsum = 0;
            for (int s = 0; s < 2; ++s) {
                if (mode != 2 && mode != s) continue;
                const auto& nb = s == 0 ? pup[static_cast<size_t>(v)] : pdown[static_cast<size_t>(v)];
                if (nb.empty()) continue;
                vw.clear();
                float wt = 0;
                for (const auto& [u, w] : nb) {
                    const float pw = pull(v, u, w);
                    vw.push_back({nodes[static_cast<size_t>(u)].x, pw});
                    wt += pw;
                }
                sum += WeightedMedian(vw) * wt;
                wsum += wt;
            }
            desired[i] = wsum > 0 ? sum / wsum : nodes[static_cast<size_t>(v)].x;
            weight[i] = wsum > 0 ? wsum : 0.01f;
            if (i + 1 < layer.size()) gaps[i] = sep(v, layer[i + 1]);
        }
        PlaceLayer(desired, weight, gaps, placed);
        for (size_t i = 0; i < layer.size(); ++i) nodes[static_cast<size_t>(layer[i])].x = placed[i];
    };
    const int passes = total > 5000 ? 2 : kCoordinatePasses;
    for (int p = 0; p < passes; ++p) {
        if (p % 2 == 0) {
            for (int r = 1; r <= max_rank; ++r) place(layers[static_cast<size_t>(r)], 0);
        } else {
            for (int r = max_rank - 1; r >= 0; --r) place(layers[static_cast<size_t>(r)], 1);
        }
    }
    for (int r = 0; r <= max_rank; ++r) place(layers[static_cast<size_t>(r)], 2);

    // Bounding box, then the requested direction.
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
    for (int v = 0; v < total; ++v) {
        const WNode& d = nodes[static_cast<size_t>(v)];
        if (v >= n && v < ranked && !used[static_cast<size_t>(d.owner)]) continue;
        x0 = std::min(x0, d.x - d.w / 2);
        x1 = std::max(x1, d.x + d.w / 2);
        y0 = std::min(y0, d.y - d.h / 2);
        y1 = std::max(y1, d.y + d.h / 2);
    }
    const float lw = x1 - x0, lh = y1 - y0;
    auto map = [&](float px, float py) {
        const float x = px - x0, y = py - y0;
        switch (g.dir) {
        case RankDir::BT: return D2D1::Point2F(x, lh - y);
        case RankDir::LR: return D2D1::Point2F(y, x);
        case RankDir::RL: return D2D1::Point2F(lh - y, x);
        default: return D2D1::Point2F(x, y);
        }
    };
    auto map_node = [&](int v) { return map(nodes[static_cast<size_t>(v)].x, nodes[static_cast<size_t>(v)].y); };
    out.width = sideways ? lh : lw;
    out.height = sideways ? lw : lh;
    for (int v = 0; v < n; ++v) out.centers[static_cast<size_t>(v)] = map_node(v);
    for (int c = 0; c < nc; ++c) {
        if (!used[static_cast<size_t>(c)]) continue;
        float cx0 = 1e30f, cx1 = -1e30f;
        for (int v : lefts[static_cast<size_t>(c)]) cx0 = std::min(cx0, nodes[static_cast<size_t>(v)].x);
        for (int v : rights[static_cast<size_t>(c)]) cx1 = std::max(cx1, nodes[static_cast<size_t>(v)].x);
        const WNode& t = nodes[static_cast<size_t>(top[static_cast<size_t>(c)])];
        const WNode& b = nodes[static_cast<size_t>(bottom[static_cast<size_t>(c)])];
        const D2D1_POINT_2F p = map(cx0, t.y - t.h / 2), q = map(cx1, b.y + b.h / 2);
        out.clusters[static_cast<size_t>(c)] =
            D2D1::RectF(std::min(p.x, q.x), std::min(p.y, q.y), std::max(p.x, q.x), std::max(p.y, q.y));
    }
    for (size_t i = 0; i < ne; ++i) {
        if (!valid[i]) continue;
        auto& b = out.bends[i];
        for (int d : chains[i]) b.push_back(map_node(d));
        if (reversed[i]) std::reverse(b.begin(), b.end());
        if (label_node[i] >= 0) {
            out.labels[i] = map_node(label_node[i]);
        } else if (!b.empty()) {
            out.labels[i] = b[b.size() / 2];
        } else {
            const D2D1_POINT_2F a = out.centers[static_cast<size_t>(g.edges[i].from)];
            const D2D1_POINT_2F c = out.centers[static_cast<size_t>(g.edges[i].to)];
            out.labels[i] = D2D1::Point2F((a.x + c.x) / 2, (a.y + c.y) / 2);
        }
    }
    return out;
}

}  // namespace mdv

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using Vec = std::vector<float>;

struct Matrix {
    int dim = 0;
    std::vector<float> data;
    size_t rows() const { return dim ? data.size() / static_cast<size_t>(dim) : 0; }
    const float* operator[](size_t i) const { return data.data() + i * dim; }
};

static Matrix read_fvecs(const std::string& path, size_t limit = 0) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    Matrix m;
    while (!limit || m.rows() < limit) {
        int32_t d;
        if (!in.read(reinterpret_cast<char*>(&d), sizeof(d))) break;
        if (d <= 0 || (m.dim && d != m.dim)) throw std::runtime_error("bad fvecs file: " + path);
        m.dim = d;
        const size_t old = m.data.size();
        m.data.resize(old + d);
        if (!in.read(reinterpret_cast<char*>(m.data.data() + old), sizeof(float) * d))
            throw std::runtime_error("truncated fvecs file: " + path);
    }
    return m;
}

static std::vector<std::vector<int>> read_ivecs(const std::string& path, size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::vector<std::vector<int>> out;
    while (out.size() < limit) {
        int32_t d;
        if (!in.read(reinterpret_cast<char*>(&d), sizeof(d))) break;
        out.emplace_back(d);
        if (!in.read(reinterpret_cast<char*>(out.back().data()), sizeof(int32_t) * d))
            throw std::runtime_error("truncated ivecs file: " + path);
    }
    return out;
}

static float l2sq(const float* a, const float* b, int d) {
    float s = 0;
    for (int j = 0; j < d; ++j) { const float x = a[j] - b[j]; s += x * x; }
    return s;
}

struct Node {
    Vec center;
    float radius = 0;
    int begin = 0, end = 0, left = -1, right = -1;
    bool leaf() const { return left < 0; }
};

struct ClusterTree {
    std::vector<int> ids;
    std::vector<Node> nodes;
};

class ResidualForest {
    const Matrix& xb_;
    int leaf_size_;

    int build_node(ClusterTree& t, int begin, int end) {
        const int d = xb_.dim, n = end - begin;
        Node node;
        node.begin = begin; node.end = end; node.center.assign(d, 0.0f);
        Vec sumsq(d, 0.0f);
        for (int p = begin; p < end; ++p) {
            const float* x = xb_[t.ids[p]];
            for (int j = 0; j < d; ++j) {
                node.center[j] += x[j]; sumsq[j] += x[j] * x[j];
            }
        }
        for (float& v : node.center) v /= n;
        int split_dim = 0;
        float best_var = -1;
        for (int j = 0; j < d; ++j) {
            // Var(x-c) == Var(x); use double-like stable enough for SIFT coordinates.
            const float var = sumsq[j] / n - node.center[j] * node.center[j];
            if (var > best_var) { best_var = var; split_dim = j; }
        }
        float r2 = 0;
        for (int p = begin; p < end; ++p) r2 = std::max(r2, l2sq(xb_[t.ids[p]], node.center.data(), d));
        node.radius = std::sqrt(r2) * (1.0f + 1e-6f) + 1e-4f; // conservative outward rounding
        const int me = static_cast<int>(t.nodes.size());
        t.nodes.push_back(std::move(node));
        if (n <= leaf_size_) return me;
        const int mid = begin + n / 2;
        std::nth_element(t.ids.begin() + begin, t.ids.begin() + mid, t.ids.begin() + end,
            [&](int a, int b) { return xb_[a][split_dim] < xb_[b][split_dim]; });
        const int left = build_node(t, begin, mid), right = build_node(t, mid, end);
        t.nodes[me].left = left; t.nodes[me].right = right;
        return me;
    }

public:
    std::vector<ClusterTree> trees;
    ResidualForest(const Matrix& xb, std::vector<std::vector<int>> lists,
                   const std::vector<Vec>& centroids, int leaf_size)
        : xb_(xb), leaf_size_(leaf_size), trees(lists.size()) {
        (void)centroids; // A per-cluster translation leaves the Ball Tree and its bounds unchanged.
        for (size_t c = 0; c < lists.size(); ++c) {
            trees[c].ids = std::move(lists[c]);
            if (!trees[c].ids.empty()) build_node(trees[c], 0, static_cast<int>(trees[c].ids.size()));
        }
    }
};

struct Config {
    std::string dataset = "../dataset/sift";
    std::string assignments = "../populated_index/PriorMax_SIFT1M_C100_clusterid.tsv";
    std::string output = "results.csv";
    int nlist = 100, k = 10, queries = 100, leaf = 32;
    std::vector<int> nprobes{1, 2, 4, 8, 16, 32, 64, 100};
};

static std::vector<int> csv_ints(const std::string& s) {
    std::vector<int> v; std::stringstream ss(s); std::string x;
    while (std::getline(ss, x, ',')) v.push_back(std::stoi(x));
    return v;
}

static Config args(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::string { if (++i == argc) throw std::runtime_error("missing value after " + a); return argv[i]; };
        if (a == "--dataset") c.dataset = value();
        else if (a == "--assignments") c.assignments = value();
        else if (a == "--output") c.output = value();
        else if (a == "--nlist") c.nlist = std::stoi(value());
        else if (a == "--k") c.k = std::stoi(value());
        else if (a == "--queries") c.queries = std::stoi(value());
        else if (a == "--leaf-size") c.leaf = std::stoi(value());
        else if (a == "--nprobes") c.nprobes = csv_ints(value());
        else if (a == "--help") {
            std::cout << "Usage: veriann [--dataset DIR] [--assignments FILE] [--queries N] "
                         "[--k K] [--leaf-size N] [--nprobes 1,2,...] [--output FILE]\n";
            std::exit(0);
        } else throw std::runtime_error("unknown option: " + a);
    }
    return c;
}

static std::vector<std::vector<int>> load_lists(const std::string& path, int nlist, size_t nbase) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::vector<std::vector<int>> lists(nlist);
    std::vector<uint8_t> seen(nbase, 0);
    long id, cluster; size_t count = 0;
    while (in >> id >> cluster) {
        if (id < 0 || static_cast<size_t>(id) >= nbase || cluster < 0 || cluster >= nlist)
            throw std::runtime_error("invalid assignment entry");
        lists[cluster].push_back(static_cast<int>(id)); seen[id] = 1; ++count;
    }
    if (count != nbase || std::find(seen.begin(), seen.end(), 0) != seen.end())
        throw std::runtime_error("assignment file must contain every base vector exactly once");
    return lists;
}

static std::vector<Vec> means(const Matrix& xb, const std::vector<std::vector<int>>& lists) {
    std::vector<Vec> cs(lists.size(), Vec(xb.dim, 0));
    for (size_t c = 0; c < lists.size(); ++c) {
        for (int id : lists[c]) for (int j = 0; j < xb.dim; ++j) cs[c][j] += xb[id][j];
        for (float& x : cs[c]) x /= lists[c].size();
    }
    return cs;
}

struct VO { uint64_t opened = 0, frontier = 0, pruned = 0, opened_far = 0; };

static VO verify_tree(const ClusterTree& t, const Matrix& xb, const float* q, float tau2, int d) {
    VO v; if (t.nodes.empty()) return v;
    std::vector<int> stack{0};
    const float tau = std::sqrt(tau2);
    while (!stack.empty()) {
        const Node& n = t.nodes[stack.back()]; stack.pop_back();
        const float lb = std::max(0.0f, std::sqrt(l2sq(q, n.center.data(), d)) - n.radius);
        if (lb > tau) { ++v.frontier; v.pruned += n.end - n.begin; }
        else if (n.leaf()) {
            v.opened += n.end - n.begin;
            for (int p = n.begin; p < n.end; ++p) v.opened_far += l2sq(q, xb[t.ids[p]], d) > tau2;
        }
        else { stack.push_back(n.left); stack.push_back(n.right); }
    }
    return v;
}

int main(int argc, char** argv) try {
    const Config cfg = args(argc, argv);
    std::cerr << "Loading SIFT base/query/ground truth...\n";
    const Matrix xb = read_fvecs(cfg.dataset + "/sift_base.fvecs");
    const Matrix xq = read_fvecs(cfg.dataset + "/sift_query.fvecs", cfg.queries);
    const auto gt = read_ivecs(cfg.dataset + "/sift_groundtruth.ivecs", xq.rows());
    auto lists = load_lists(cfg.assignments, cfg.nlist, xb.rows());
    const auto centroids = means(xb, lists);
    std::cerr << "Building residual Ball Trees (translation-equivalent centers), leaf=" << cfg.leaf << "...\n";
    ResidualForest forest(xb, lists, centroids, cfg.leaf);
    std::ofstream csv(cfg.output);
    csv << "nprobe,recall_at_k,avg_visited,avg_opened,avg_frontier,avg_total_vo_items,vector_reduction,total_item_reduction,node_pruning_ratio,point_prune_potential\n";
    std::cout << "nprobe recall  visited   opened frontier vector_reduction total_item_reduction\n";
    for (int np : cfg.nprobes) {
        if (np < 1 || np > cfg.nlist) throw std::runtime_error("nprobe outside [1,nlist]");
        uint64_t visited = 0, opened = 0, frontier = 0, pruned = 0, point_far = 0, hits = 0;
        for (size_t qi = 0; qi < xq.rows(); ++qi) {
            std::vector<int> probes(cfg.nlist); std::iota(probes.begin(), probes.end(), 0);
            std::partial_sort(probes.begin(), probes.begin() + np, probes.end(), [&](int a, int b) {
                return l2sq(xq[qi], centroids[a].data(), xb.dim) < l2sq(xq[qi], centroids[b].data(), xb.dim);
            });
            using Pair = std::pair<float, int>;
            std::priority_queue<Pair> top;
            for (int p = 0; p < np; ++p) for (int id : forest.trees[probes[p]].ids) {
                const float dis = l2sq(xq[qi], xb[id], xb.dim); ++visited;
                if (static_cast<int>(top.size()) < cfg.k) top.push({dis, id});
                else if (dis < top.top().first) { top.pop(); top.push({dis, id}); }
            }
            if (static_cast<int>(top.size()) < cfg.k) throw std::runtime_error("fewer than k candidates");
            const float tau2 = top.top().first;
            std::unordered_set<int> answer;
            while (!top.empty()) { answer.insert(top.top().second); top.pop(); }
            for (int j = 0; j < cfg.k && j < static_cast<int>(gt[qi].size()); ++j) hits += answer.count(gt[qi][j]);
            for (int p = 0; p < np; ++p) {
                const auto& tree = forest.trees[probes[p]];
                const VO v = verify_tree(tree, xb, xq[qi], tau2, xb.dim);
                opened += v.opened; frontier += v.frontier; pruned += v.pruned;
                point_far += v.pruned + v.opened_far;
            }
        }
        const double nq = xq.rows(), recall = hits / (nq * cfg.k);
        const double vr = 1.0 - static_cast<double>(opened) / visited;
        const double tr = 1.0 - static_cast<double>(opened + frontier) / visited;
        csv << np << ',' << recall << ',' << visited/nq << ',' << opened/nq << ',' << frontier/nq << ','
            << (opened+frontier)/nq << ',' << vr << ',' << tr << ',' << static_cast<double>(pruned)/visited
            << ',' << static_cast<double>(point_far)/visited << '\n';
        std::cout << std::setw(6) << np << ' ' << std::fixed << std::setprecision(4) << recall << ' '
                  << std::setw(9) << visited/nq << ' ' << std::setw(8) << opened/nq << ' '
                  << std::setw(8) << frontier/nq << ' ' << std::setw(16) << vr << ' ' << tr << '\n';
    }
    std::cerr << "Wrote " << cfg.output << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n'; return 1;
}

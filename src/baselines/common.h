#pragma once
#include "graph.h"
#include "keccak.h"
#include <functional>
#include <map>
#include <omp.h>
#include <random>
#include <set>
#include <sstream>
#include <sys/resource.h>

// Shared graph records, serialization and measurement for authenticated baselines.
namespace paper_repro {
using Hash = vann::Hash;
using Bytes = std::vector<uint8_t>;
using Clock = std::chrono::steady_clock;
inline bool keccak = false;
inline int build_threads = 1;

inline double ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

inline Hash hash(const Bytes& b) {
    Hash h{};
    if (keccak) {
        similarity::Keccak256::getHash(b.data(), b.size(), h.data());
    } else {
        SHA256(b.data(), b.size(), h.data());
    }
    return h;
}

inline Hash pair_hash(const Hash& a, const Hash& b) {
    Bytes x(a.begin(), a.end());
    x.insert(x.end(), b.begin(), b.end());
    return hash(x);
}

inline void u32(Bytes& b, uint32_t x) {
    for (int i = 0; i < 4; ++i) {
        b.push_back(uint8_t(x >> (8 * i)));
    }
}

inline void u64(Bytes& b, uint64_t x) {
    for (int i = 0; i < 8; ++i) {
        b.push_back(uint8_t(x >> (8 * i)));
    }
}

inline void f32(Bytes& b, float x) {
    uint32_t u;
    std::memcpy(&u, &x, 4);
    u32(b, u);
}

inline void digest(Bytes& b, const Hash& h) {
    b.insert(b.end(), h.begin(), h.end());
}

inline void string(Bytes& b, const std::string& s) {
    u32(b, s.size());
    b.insert(b.end(), s.begin(), s.end());
}

inline std::string hex(const Hash& h) {
    std::ostringstream s;
    s << std::hex << std::setfill('0');
    for (auto c : h) {
        s << std::setw(2) << int(c);
    }
    return s.str();
}

struct Reader {
    const Bytes& b;
    size_t p = 0;

    void need(size_t n) {
        if (n > b.size() - p) {
            throw std::runtime_error("truncated VO");
        }
    }

    uint32_t u32() {
        need(4);
        uint32_t x = 0;
        for (int i = 0; i < 4; ++i) {
            x |= uint32_t(b[p++]) << (8 * i);
        }
        return x;
    }

    uint64_t u64() {
        need(8);
        uint64_t x = 0;
        for (int i = 0; i < 8; ++i) {
            x |= uint64_t(b[p++]) << (8 * i);
        }
        return x;
    }

    float f32() {
        auto u = u32();
        float x;
        std::memcpy(&x, &u, 4);
        if (!std::isfinite(x)) {
            throw std::runtime_error("nonfinite float");
        }
        return x;
    }

    Hash digest() {
        need(32);
        Hash h;
        std::copy_n(b.begin() + p, 32, h.begin());
        p += 32;
        return h;
    }

    std::string string() {
        auto n = u32();
        need(n);
        std::string s(b.begin() + p, b.begin() + p + n);
        p += n;
        return s;
    }

    uint32_t count(size_t minimum = 1) {
        auto n = u32();
        if (n > (b.size() - p) / minimum) {
            throw std::runtime_error("invalid VO count");
        }
        return n;
    }

    void end() {
        if (p != b.size()) {
            throw std::runtime_error("extra VO bytes");
        }
    }
};

struct ProofNode {
    uint32_t level, position;
    Hash value;
};

using Proof = std::vector<ProofNode>;

struct Tree {
    uint32_t n;
    std::vector<std::vector<Hash>> levels;

    explicit Tree(std::vector<Hash> leaves) : n(leaves.size()) {
        if (!n) {
            throw std::runtime_error("empty tree");
        }
        size_t p = 1;
        while (p < n) {
            p *= 2;
        }
        leaves.resize(p, leaves.back());
        levels.push_back(std::move(leaves));
        while (levels.back().size() > 1) {
            auto& c = levels.back();
            std::vector<Hash> next(c.size() / 2);
            for (size_t i = 0; i < next.size(); ++i) {
                next[i] = pair_hash(c[2 * i], c[2 * i + 1]);
            }
            levels.push_back(std::move(next));
        }
    }

    Hash root() const {
        return levels.back()[0];
    }

    Proof prove(std::set<uint32_t> targets, bool paths = false) const {
        if (paths) {
            Proof all;
            for (auto id : targets) {
                auto p = prove({id});
                all.insert(all.end(), p.begin(), p.end());
            }
            return all;
        }
        Proof p;
        for (uint32_t l = 0; l + 1 < levels.size(); ++l) {
            std::set<uint32_t> next;
            for (auto id : targets) {
                if (!targets.count(id ^ 1)) {
                    p.push_back({l, id ^ 1, levels[l].at(id ^ 1)});
                }
                next.insert(id / 2);
            }
            targets = std::move(next);
        }
        return p;
    }

    static bool verify(Hash root, uint32_t n, std::map<uint32_t, Hash> targets, const Proof& p) {
        if (!n || targets.empty()) {
            return false;
        }
        size_t width = 1;
        while (width < n) {
            width *= 2;
        }
        for (auto [id, h] : targets) {
            if (id >= n) {
                return false;
            }
        }
        std::map<std::pair<uint32_t, uint32_t>, Hash> proof;
        uint32_t height = 0;
        for (size_t w = width; w > 1; w /= 2) {
            ++height;
        }
        for (auto node : p) {
            if (node.level >= height || node.position >= (width >> node.level)) {
                return false;
            }
            auto [it, ok] = proof.emplace(std::make_pair(node.level, node.position), node.value);
            if (!ok && it->second != node.value) {
                return false;
            }
        }
        std::map<std::pair<uint32_t, uint32_t>, Hash> authenticated;
        for (uint32_t l = 0; width > 1; ++l, width /= 2) {
            std::map<uint32_t, Hash> next;
            for (auto [id, h] : targets) {
                authenticated[{l, id}] = h;
                uint32_t sibling = id ^ 1;
                Hash sh;
                auto it = targets.find(sibling);
                if (it != targets.end()) {
                    sh = it->second;
                } else {
                    auto key = std::make_pair(l, sibling);
                    auto pi = proof.find(key);
                    if (pi == proof.end()) {
                        return false;
                    }
                    sh = pi->second;
                    authenticated[key] = sh;
                }
                auto ph = (id % 2) ? pair_hash(sh, h) : pair_hash(h, sh);
                auto [ni, ok] = next.emplace(id / 2, ph);
                if (!ok && ni->second != ph) {
                    return false;
                }
            }
            targets = std::move(next);
        }
        // Independent paths include redundant siblings that were also targets.
        // Check these against reconstructed values rather than silently ignoring them.
        for (auto [key, value] : proof) {
            auto it = authenticated.find(key);
            if (it == authenticated.end() || it->second != value) {
                return false;
            }
        }
        return targets.size() == 1 && targets.begin()->first == 0 &&
               targets.begin()->second == root;
    }
};

inline void write_proof(Bytes& b, const Proof& p) {
    u32(b, p.size());
    for (auto n : p) {
        u32(b, n.level);
        u32(b, n.position);
        digest(b, n.value);
    }
}

inline Proof read_proof(Reader& r) {
    Proof p;
    auto n = r.count(40);
    for (uint32_t i = 0; i < n; ++i) {
        p.push_back({r.u32(), r.u32(), r.digest()});
    }
    return p;
}

struct Edge {
    uint32_t id;
    float distance;
};

struct Record {
    uint32_t id;
    std::vector<float> vector;
    std::vector<std::vector<Edge>> links;
};

inline Bytes vector_bytes(const std::vector<float>& v) {
    Bytes b;
    for (float x : v) {
        f32(b, x);
    }
    return b;
}

inline Bytes adjacency_bytes(const Record& r, bool with_distance) {
    Bytes b;
    u32(b, r.links.size());
    for (auto& layer : r.links) {
        u32(b, layer.size());
        for (auto e : layer) {
            u32(b, e.id);
            if (with_distance) {
                f32(b, e.distance);
            }
        }
    }
    return b;
}

inline Hash leaf(const Record& r) {
    Bytes id;
    u32(id, r.id);
    auto hv = hash(vector_bytes(r.vector)), ha = hash(adjacency_bytes(r, !keccak));
    Bytes b;
    if (keccak) {
        digest(b, hash(id));
    } else {
        u32(b, r.id);
    }
    digest(b, hv);
    digest(b, ha);
    return hash(b);
}

inline Hash identifier_leaf(const Record& r) {
    Bytes b, id;
    u32(id, r.id);
    digest(b, hash(id));
    digest(b, hash(vector_bytes(r.vector)));
    return hash(b);
}

inline void write_record(Bytes& b, uint32_t local, const Record& r) {
    u32(b, local);
    u32(b, r.id);
    u32(b, r.vector.size());
    for (float v : r.vector) {
        f32(b, v);
    }
    auto a = adjacency_bytes(r, !keccak);
    b.insert(b.end(), a.begin(), a.end());
}

inline std::pair<uint32_t, Record> read_record(Reader& r, int dim, uint32_t n) {
    uint32_t local = r.u32();
    Record x;
    x.id = r.u32();
    if (local >= n || r.u32() != uint32_t(dim)) {
        throw std::runtime_error("invalid record");
    }
    for (int j = 0; j < dim; ++j) {
        x.vector.push_back(r.f32());
    }
    auto levels = r.count(4);
    if (levels < 1 || levels > 64) {
        throw std::runtime_error("invalid levels");
    }
    for (uint32_t l = 0; l < levels; ++l) {
        std::vector<Edge> layer;
        auto count = r.count(keccak ? 4 : 8);
        for (uint32_t i = 0; i < count; ++i) {
            Edge e{r.u32(), 0};
            if (!keccak) {
                e.distance = r.f32();
            }
            if (e.id >= n) {
                throw std::runtime_error("invalid edge");
            }
            layer.push_back(e);
        }
        x.links.push_back(std::move(layer));
    }
    return {local, std::move(x)};
}

struct Graph {
    std::vector<Record> records;
    uint32_t entry = 0, maxlevel = 0;

    Graph(const vann::Matrix& base, const std::vector<int>& ids, int M, int efc,
          const std::string& cache) {
        vann::Matrix sub;
        sub.dimension = base.dimension;
        sub.values.reserve(ids.size() * size_t(base.dimension));
        for (int id : ids) {
            sub.values.insert(sub.values.end(), base.row(id), base.row(id) + base.dimension);
        }
        omp_set_num_threads(build_threads);
        auto h = vann::build_hnsw(sub, M, efc, cache);
        omp_set_num_threads(1);
        entry = h.index->hnsw.entry_point;
        maxlevel = h.index->hnsw.max_level;
        records.reserve(ids.size());
        for (uint32_t id = 0; id < ids.size(); ++id) {
            Record r;
            r.id = ids[id];
            r.vector.assign(h.vector(id), h.vector(id) + base.dimension);
            for (int l = 0; l < h.index->hnsw.levels[id]; ++l) {
                std::vector<Edge> edges;
                for (int v : h.neighbors(id, l)) {
                    float d = 0;
                    if (!keccak) {
                        d = std::sqrt(vann::squared_l2(h.vector(id), h.vector(v), base.dimension));
                    }
                    edges.push_back({uint32_t(v), d});
                }
                std::sort(edges.begin(), edges.end(), [](auto a, auto b) { return a.id < b.id; });
                r.links.push_back(std::move(edges));
            }
            records.push_back(std::move(r));
        }
    }
};

struct Search {
    std::vector<std::pair<float, uint32_t>> best;
    std::vector<std::pair<uint32_t, uint32_t>> trace;
    std::set<uint32_t> touched;
};

inline Search search(const std::function<const Record&(uint32_t)>& get, uint32_t entry,
                     uint32_t maxlevel, const float* q, int dim, int k, int ef) {
    Search s;
    auto distance = [&](uint32_t id) {
        s.touched.insert(id);
        return vann::squared_l2(q, get(id).vector.data(), dim);
    };
    auto adjacent = [&](uint32_t id, uint32_t l) -> const std::vector<Edge>& {
        s.touched.insert(id);
        s.trace.emplace_back(id, l);
        const auto& r = get(id);
        if (l >= r.links.size()) {
            throw std::runtime_error("missing search layer");
        }
        return r.links[l];
    };
    uint32_t current = entry;
    float current_d = distance(current);
    for (uint32_t layer = maxlevel; layer > 0; --layer) {
        bool changed = true;
        while (changed) {
            changed = false;
            uint32_t previous = current;
            for (auto e : adjacent(previous, layer)) {
                float d = distance(e.id);
                if (std::make_pair(d, e.id) < std::make_pair(current_d, current)) {
                    current = e.id;
                    current_d = d;
                    changed = true;
                }
            }
        }
    }
    using Item = std::pair<float, uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> candidates;
    std::priority_queue<Item> best;
    std::set<uint32_t> seen;
    Item first{distance(current), current};
    candidates.push(first);
    best.push(first);
    seen.insert(current);
    ef = std::max(ef, k);
    while (!candidates.empty()) {
        auto c = candidates.top();
        candidates.pop();
        if (best.size() >= size_t(ef) && c > best.top()) {
            break;
        }
        for (auto e : adjacent(c.second, 0)) {
            if (!seen.insert(e.id).second) {
                continue;
            }
            Item p{distance(e.id), e.id};
            if (best.size() < size_t(ef) || p < best.top()) {
                candidates.push(p);
                best.push(p);
                if (best.size() > size_t(ef)) {
                    best.pop();
                }
            }
        }
    }
    while (!best.empty()) {
        s.best.push_back(best.top());
        best.pop();
    }
    std::sort(s.best.begin(), s.best.end());
    if (s.best.size() > size_t(k)) {
        s.best.resize(k);
    }
    return s;
}

inline void write_trace(Bytes& b, const Search& s) {
    u32(b, s.trace.size());
    for (auto [id, l] : s.trace) {
        u32(b, id);
        u32(b, l);
    }
}

inline std::vector<std::pair<uint32_t, uint32_t>> read_trace(Reader& r) {
    std::vector<std::pair<uint32_t, uint32_t>> t;
    auto n = r.count(8);
    for (uint32_t i = 0; i < n; ++i) {
        auto id = r.u32(), l = r.u32();
        t.emplace_back(id, l);
    }
    return t;
}

inline std::vector<int> ints(const std::string& value) {
    std::vector<int> v;
    std::stringstream s(value);
    std::string t;
    while (std::getline(s, t, ',')) {
        v.push_back(std::stoi(t));
    }
    if (v.empty()) {
        throw std::runtime_error("empty parameter list");
    }
    return v;
}

struct Config {
    std::string dataset = "data/sift", output = "results", cache = "cache", metric = "l2",
                proof = "multiproof";
    int base = 100000, queries = 100, queryoffset = 0, buildthreads = 1, M = 8, efc = 100,
        shard = 10000, bucket = 20000, seed = 42, repeats = 1;
    double width = 256;
    std::vector<int> ks{10}, efs{50}, probes{3};
};

inline Config args(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (++i == argc) {
            throw std::runtime_error("missing option value");
        }
        std::string v = argv[i];
        if (a == "--dataset") {
            c.dataset = v;
        } else if (a == "--output") {
            c.output = v;
        } else if (a == "--cache") {
            c.cache = v;
        } else if (a == "--base") {
            c.base = std::stoi(v);
        } else if (a == "--queries") {
            c.queries = std::stoi(v);
        } else if (a == "--query-offset") {
            c.queryoffset = std::stoi(v);
        } else if (a == "--build-threads") {
            c.buildthreads = std::stoi(v);
        } else if (a == "--M") {
            c.M = std::stoi(v);
        } else if (a == "--ef-construction") {
            c.efc = std::stoi(v);
        } else if (a == "--shard-size") {
            c.shard = std::stoi(v);
        } else if (a == "--bucket-size") {
            c.bucket = std::stoi(v);
        } else if (a == "--seed") {
            c.seed = std::stoi(v);
        } else if (a == "--repeats") {
            c.repeats = std::stoi(v);
        } else if (a == "--lsh-width") {
            c.width = std::stod(v);
        } else if (a == "--k") {
            c.ks = ints(v);
        } else if (a == "--ef-search") {
            c.efs = ints(v);
        } else if (a == "--probes") {
            c.probes = ints(v);
        } else if (a == "--metric") {
            c.metric = v;
        } else if (a == "--proof") {
            c.proof = v;
        } else {
            throw std::runtime_error("unknown option " + a);
        }
    }
    if (c.base < 1 || c.queries < 1 || c.queryoffset < 0 || c.buildthreads < 1 ||
        c.buildthreads > 24 || c.M < 2 || c.efc < 1 || c.shard < 1 || c.bucket < 1 ||
        c.repeats < 1 || c.width <= 0 || !std::isfinite(c.width)) {
        throw std::runtime_error("invalid configuration");
    }
    for (auto v : c.ks) {
        if (v < 1 || v > c.base) {
            throw std::runtime_error("invalid k");
        }
    }
    for (auto v : c.efs) {
        if (v < 1) {
            throw std::runtime_error("invalid ef");
        }
    }
    for (auto v : c.probes) {
        if (v < 1) {
            throw std::runtime_error("invalid probes");
        }
    }
    if (c.metric != "l2" && c.metric != "cosine") {
        throw std::runtime_error("metric must be l2 or cosine");
    }
    if (c.proof != "multiproof" && c.proof != "paths") {
        throw std::runtime_error("unknown proof mode");
    }
    std::filesystem::create_directories(c.output);
    std::filesystem::create_directories(c.cache);
    return c;
}

inline void normalize(vann::Matrix& m) {
    for (size_t i = 0; i < m.rows(); ++i) {
        double n = 0;
        for (int j = 0; j < m.dimension; ++j) {
            n += double(m.row(i)[j]) * m.row(i)[j];
        }
        if (n > 0) {
            float inv = 1 / std::sqrt(n);
            for (int j = 0; j < m.dimension; ++j) {
                m.values[i * m.dimension + j] *= inv;
            }
        }
    }
}

inline vann::Matrix read_queries(const Config& c) {
    if (c.queryoffset < 0) {
        throw std::runtime_error("negative query offset");
    }
    const size_t limit = size_t(c.queryoffset) + size_t(c.queries);
    auto q = vann::read_fvecs(c.dataset + "/sift_query.fvecs", limit);
    if (q.rows() != limit) {
        throw std::runtime_error("not enough queries for requested offset/count");
    }
    q.values.erase(q.values.begin(), q.values.begin() + size_t(c.queryoffset) * q.dimension);
    return q;
}

inline std::vector<std::vector<uint32_t>> ground_truth(const vann::Matrix& b, const vann::Matrix& q,
                                                       int k) {
    std::vector<std::vector<uint32_t>> out;
    for (size_t qi = 0; qi < q.rows(); ++qi) {
        std::vector<std::pair<float, uint32_t>> a;
        for (uint32_t id = 0; id < b.rows(); ++id) {
            a.emplace_back(vann::squared_l2(b.row(id), q.row(qi), b.dimension), id);
        }
        std::partial_sort(a.begin(), a.begin() + k, a.end());
        std::vector<uint32_t> ids;
        for (int j = 0; j < k; ++j) {
            ids.push_back(a[j].second);
        }
        out.push_back(std::move(ids));
    }
    return out;
}

inline Hash query_hash(const float* q, int d) {
    Bytes b;
    for (int j = 0; j < d; ++j) {
        f32(b, q[j]);
    }
    return hash(b);
}

inline Hash data_hash(const vann::Matrix& b) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(b.values.data());
    return vann::sha256(p, b.values.size() * sizeof(float));
}

inline void write_header(Bytes& b, const float* q, int d, int k, int ef,
                         const std::vector<uint32_t>& answer) {
    u32(b, 0x56505231);
    digest(b, query_hash(q, d));
    u32(b, k);
    u32(b, ef);
    u32(b, answer.size());
    for (auto id : answer) {
        u32(b, id);
    }
}

inline std::vector<uint32_t> read_header(Reader& r, const float* q, int d, int k, int ef) {
    if (r.u32() != 0x56505231 || r.digest() != query_hash(q, d) || r.u32() != uint32_t(k) ||
        r.u32() != uint32_t(ef)) {
        throw std::runtime_error("invalid query binding");
    }
    auto n = r.count(4);
    if (n != uint32_t(k)) {
        throw std::runtime_error("invalid result count");
    }
    std::vector<uint32_t> a;
    for (uint32_t i = 0; i < n; ++i) {
        a.push_back(r.u32());
    }
    return a;
}

inline void save(const std::string& path, const Bytes& b) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), b.size());
    if (!f) {
        throw std::runtime_error("cannot save VO");
    }
}

struct Row {
    int query, repeat, k, ef, probes;
    double search_ms, vo_ms, verify_ms, auth_ms, replay_ms;
    size_t vo_bytes, record_bytes, local_proof_bytes, route_bytes, visited, expanded, buckets;
    double recall, recall1, kth_ratio;
    bool valid;
};

inline double quantile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    return v.at(size_t(std::ceil(p * v.size()) - 1));
}

inline double mean(const std::vector<double>& v) {
    return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
}

inline void output(const Config& c, const std::vector<Row>& rows, double index_ms, double ads_ms,
                   double gt_ms, const Hash& data, const std::string& method, size_t ads_bytes) {
    std::ofstream p(c.output + "/per_query.csv");
    p << "method,query,repeat,k,ef_search,probes,search_ms,vo_generation_ms,verification_ms,"
         "authentication_ms,replay_ms,server_total_ms,end_to_end_compute_ms,vo_bytes,vo_kib,record_"
         "bytes,local_proof_bytes,route_proof_bytes,visited,expanded,buckets,recall_at_k,recall_at_"
         "1,kth_distance_ratio,verified\n";
    p << std::setprecision(10);
    for (auto& r : rows) {
        p << method << ',' << r.query << ',' << r.repeat << ',' << r.k << ',' << r.ef << ','
          << r.probes << ',' << r.search_ms << ',' << r.vo_ms << ',' << r.verify_ms << ','
          << r.auth_ms << ',' << r.replay_ms << ',' << r.search_ms + r.vo_ms << ','
          << r.search_ms + r.vo_ms + r.verify_ms << ',' << r.vo_bytes << ',' << r.vo_bytes / 1024.0
          << ',' << r.record_bytes << ',' << r.local_proof_bytes << ',' << r.route_bytes << ','
          << r.visited << ',' << r.expanded << ',' << r.buckets << ',' << r.recall << ','
          << r.recall1 << ',' << r.kth_ratio << ',' << r.valid << '\n';
    }
    std::ofstream s(c.output + "/summary.csv");
    s << "method,k,ef_search,probes,samples,search_mean_ms,search_p50_ms,search_p95_ms,vo_"
         "generation_mean_ms,verification_mean_ms,verification_p95_ms,authentication_mean_ms,"
         "replay_mean_ms,vo_mean_kib,vo_p95_kib,recall_at_k,recall_at_1,verified_fraction,index_ms,"
         "ads_ms,ground_truth_ms,ads_bytes,peak_rss_mib\n";
    std::map<std::tuple<int, int, int>, std::vector<const Row*>> groups;
    for (auto& r : rows) {
        groups[{r.k, r.ef, r.probes}].push_back(&r);
    }
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    for (auto& [key, group] : groups) {
        std::vector<double> a, b, v, h, t, bytes, rec, rec1, ok;
        for (auto* r : group) {
            a.push_back(r->search_ms);
            b.push_back(r->vo_ms);
            v.push_back(r->verify_ms);
            h.push_back(r->auth_ms);
            t.push_back(r->replay_ms);
            bytes.push_back(r->vo_bytes / 1024.0);
            rec.push_back(r->recall);
            rec1.push_back(r->recall1);
            ok.push_back(r->valid);
        }
        auto [k, ef, np] = key;
        s << method << ',' << k << ',' << ef << ',' << np << ',' << group.size() << ',' << mean(a)
          << ',' << quantile(a, .5) << ',' << quantile(a, .95) << ',' << mean(b) << ',' << mean(v)
          << ',' << quantile(v, .95) << ',' << mean(h) << ',' << mean(t) << ',' << mean(bytes)
          << ',' << quantile(bytes, .95) << ',' << mean(rec) << ',' << mean(rec1) << ',' << mean(ok)
          << ',' << index_ms << ',' << ads_ms << ',' << gt_ms << ',' << ads_bytes << ','
          << usage.ru_maxrss / 1024.0 << '\n';
        std::cout << method << " k=" << k << " ef=" << ef << " probes=" << np
                  << " recall=" << mean(rec) << " VO=" << mean(bytes) << " KiB search=" << mean(a)
                  << "ms verify=" << mean(v) << "ms\n";
    }
    std::ofstream m(c.output + "/manifest.json");
    m << "{\n\"build_threads\":" << c.buildthreads
      << ",\n\"query_threads\":1,\n\"query_offset\":" << c.queryoffset << ",\n\"method\":\""
      << method << "\",\"base\":" << c.base << ",\"queries\":" << c.queries << ",\"M\":" << c.M
      << ",\"ef_construction\":" << c.efc << ",\"shard_size\":" << c.shard
      << ",\"bucket_size\":" << c.bucket << ",\"lsh_width\":" << c.width << ",\"seed\":" << c.seed
      << ",\"repeats\":" << c.repeats << ",\"metric\":\"" << c.metric << "\",\"proof\":\""
      << c.proof << "\",\"hash\":\"" << (keccak ? "Keccak-256" : "SHA-256")
      << "\",\"dataset_sha256\":\"" << hex(data)
      << "\",\"timing\":\"steady_clock; serialized VO decoding included in verification; transport "
         "excluded\"\n}\n";
}

inline void quality(Row& r, const std::vector<uint32_t>& a, const std::vector<uint32_t>& gt,
                    const vann::Matrix& b, const float* q) {
    std::set<uint32_t> truth(gt.begin(), gt.begin() + r.k);
    int hits = 0;
    for (auto id : a) {
        hits += truth.count(id);
    }
    r.recall = double(hits) / r.k;
    r.recall1 = !a.empty() && a[0] == gt[0];
    float exact = vann::squared_l2(q, b.row(gt[r.k - 1]), b.dimension),
          returned = vann::squared_l2(q, b.row(a.back()), b.dimension);
    r.kth_ratio = exact > 0 ? std::sqrt(returned / exact) : (returned == 0 ? 1 : INFINITY);
}

inline void require(bool ok, const std::string& why) {
    if (!ok) {
        throw std::runtime_error(why);
    }
}
} // namespace paper_repro

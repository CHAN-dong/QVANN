#include "common.h"
using namespace paper_repro;

// Eq. (1): Gaussian p-stable projections followed by floor((a.x+b)/w).
// The paper does not define bit encoding; use eight biased 8-bit coordinates,
// interleaved into a 64-bit Morton key. This choice is recorded in README.
struct LSH {
    int dim;
    double width;
    std::vector<std::vector<double>> a;
    std::vector<double> b;

    LSH(int dim, double w, int seed) : dim(dim), width(w) {
        std::mt19937 rng(seed);
        std::normal_distribution<double> normal;
        std::uniform_real_distribution<double> offset(0, w);
        for (int i = 0; i < 8; ++i) {
            std::vector<double> p(dim);
            for (auto& x : p) {
                x = normal(rng);
            }
            a.push_back(std::move(p));
            b.push_back(offset(rng));
        }
    }

    uint64_t code(const float* x) const {
        uint8_t coordinates[8];
        for (int i = 0; i < 8; ++i) {
            double p = b[i];
            for (int j = 0; j < dim; ++j) {
                p += a[i][j] * x[j];
            }
            double q = std::floor(p / width);
            coordinates[i] = uint8_t(std::clamp(q + 128.0, 0.0, 255.0));
        }
        uint64_t code = 0;
        for (int bit = 7; bit >= 0; --bit) {
            for (int j = 0; j < 8; ++j) {
                code = (code << 1) | ((coordinates[j] >> bit) & 1);
            }
        }
        return code;
    }

    void save(const std::string& path) const {
        std::ofstream f(path);
        f << std::setprecision(17) << "dim " << dim << " width " << width
          << " projections 8 coordinate_bits 8\n";
        for (int i = 0; i < 8; ++i) {
            f << b[i];
            for (double x : a[i]) {
                f << ' ' << x;
            }
            f << '\n';
        }
    }
};

struct Descriptor {
    uint32_t id, n, entry, maxlevel;
    uint64_t low, high;
    Hash root;
};

inline void write_descriptor(Bytes& b, const Descriptor& d) {
    u32(b, d.id);
    u32(b, d.n);
    u32(b, d.entry);
    u32(b, d.maxlevel);
    u64(b, d.low);
    u64(b, d.high);
    digest(b, d.root);
}

inline Descriptor read_descriptor(Reader& r) {
    Descriptor d;
    d.id = r.u32();
    d.n = r.u32();
    d.entry = r.u32();
    d.maxlevel = r.u32();
    d.low = r.u64();
    d.high = r.u64();
    d.root = r.digest();
    require(d.n && d.entry < d.n && d.maxlevel < 64 && d.low <= d.high, "bad bucket descriptor");
    return d;
}

inline std::string key(const Descriptor& d) {
    std::string s;
    for (int i = 63; i >= 0; --i) {
        s += char('0' + ((d.low >> i) & 1));
    }
    for (int i = 31; i >= 0; --i) {
        s += char('0' + ((d.id >> i) & 1));
    }
    return s;
}

struct Trie {
    std::string prefix;
    int leaf_id = -1;
    std::unique_ptr<Trie> left, right;
    Hash root;

    Trie(const std::vector<Descriptor>& ds, int begin, int end) {
        require(begin < end, "empty trie");
        auto first = key(ds[begin]), last = key(ds[end - 1]);
        size_t common = 0;
        while (common < first.size() && first[common] == last[common]) {
            ++common;
        }
        prefix = first.substr(0, common);
        Bytes b;
        string(b, prefix);
        if (end - begin == 1) {
            leaf_id = begin;
            write_descriptor(b, ds[begin]);
        } else {
            int split = begin;
            while (split < end && key(ds[split])[common] == '0') {
                ++split;
            }
            require(split > begin && split < end, "invalid trie split");
            left = std::make_unique<Trie>(ds, begin, split);
            right = std::make_unique<Trie>(ds, split, end);
            digest(b, left->root);
            digest(b, right->root);
        }
        root = hash(b);
    }

    bool contains(const std::set<uint32_t>& selected) const {
        if (leaf_id >= 0) {
            return selected.count(leaf_id);
        }
        return left->contains(selected) || right->contains(selected);
    }

    void proof(Bytes& b, const std::vector<Descriptor>& ds,
               const std::set<uint32_t>& selected) const {
        if (!contains(selected)) {
            u32(b, 0);
            digest(b, root);
            return;
        }
        if (leaf_id >= 0) {
            u32(b, 2);
            string(b, prefix);
            write_descriptor(b, ds[leaf_id]);
            return;
        }
        u32(b, 1);
        string(b, prefix);
        left->proof(b, ds, selected);
        right->proof(b, ds, selected);
    }

    static Hash reconstruct(Reader& r, std::map<uint32_t, Descriptor>& ds, int depth = 0) {
        require(depth <= 96, "trie proof too deep");
        auto kind = r.u32();
        if (kind == 0) {
            return r.digest();
        }
        require(kind == 1 || kind == 2, "bad trie kind");
        auto prefix = r.string();
        require(prefix.size() <= 96 && prefix.find_first_not_of("01") == std::string::npos,
                "bad trie prefix");
        Bytes b;
        string(b, prefix);
        if (kind == 2) {
            auto d = read_descriptor(r);
            require(prefix == key(d) && ds.emplace(d.id, d).second, "bad/duplicate trie leaf");
            write_descriptor(b, d);
        } else {
            digest(b, reconstruct(r, ds, depth + 1));
            digest(b, reconstruct(r, ds, depth + 1));
        }
        return hash(b);
    }
};

inline std::vector<uint32_t> select_buckets(uint32_t main, uint32_t n, int probes) {
    std::vector<uint32_t> result;
    for (int radius = 0; result.size() < size_t(std::min(probes, int(n))); ++radius) {
        if (!radius) {
            result.push_back(main);
        } else {
            if (main >= uint32_t(radius)) {
                result.push_back(main - radius);
            }
            if (result.size() < size_t(std::min(probes, int(n))) && main + radius < n) {
                result.push_back(main + radius);
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

struct Bucket {
    std::unique_ptr<Graph> graph;
    std::unique_ptr<Tree> tree;
};

struct Client {
    int dim, n, probes;
    uint32_t buckets;
    const LSH& lsh;
    Hash root;
};

struct Response {
    uint32_t id;
    Search search;
};

struct Packet {
    Bytes wire;
    size_t records = 0, local = 0, route = 0, vector_offset = 0, adj_offset = 0, route_offset = 0,
           trace_offset = 0, count_offset = 0, first_begin = 0, first_end = 0;
};

Packet generate(const std::vector<Bucket>& bs, const std::vector<Descriptor>& ds, const Trie& trie,
                uint32_t main, const std::vector<Response>& responses, const float* q, int dim,
                int k, int ef, bool paths) {
    Packet p;
    std::vector<std::pair<float, uint32_t>> all;
    std::set<uint32_t> selection{main};
    if (main + 1 < ds.size()) {
        selection.insert(main + 1);
    }
    for (auto& r : responses) {
        selection.insert(r.id);
        for (auto [d, local] : r.search.best) {
            all.emplace_back(d, bs[r.id].graph->records[local].id);
        }
    }
    std::sort(all.begin(), all.end());
    require(all.size() >= size_t(k), "too few candidates");
    std::vector<uint32_t> answer;
    for (int j = 0; j < k; ++j) {
        answer.push_back(all[j].second);
    }
    write_header(p.wire, q, dim, k, ef, answer);
    u32(p.wire, main);
    Bytes route;
    trie.proof(route, ds, selection);
    u32(p.wire, route.size());
    p.route_offset = p.wire.size();
    p.wire.insert(p.wire.end(), route.begin(), route.end());
    p.route = route.size();
    u32(p.wire, responses.size());
    for (auto& r : responses) {
        u32(p.wire, r.id);
        size_t start = p.wire.size();
        u32(p.wire, r.search.touched.size());
        size_t recbegin = p.wire.size();
        for (auto local : r.search.touched) {
            size_t begin = p.wire.size();
            write_record(p.wire, local, bs[r.id].graph->records[local]);
            if (!p.vector_offset) {
                p.count_offset = start;
                p.first_begin = begin;
                p.first_end = p.wire.size();
                p.vector_offset = begin + 12;
                p.adj_offset = begin + 12 + dim * 4 + 8;
            }
        }
        p.records += p.wire.size() - recbegin;
        if (!p.trace_offset) {
            p.trace_offset = p.wire.size() + 4;
        }
        write_trace(p.wire, r.search);
        start = p.wire.size();
        write_proof(p.wire, bs[r.id].tree->prove(r.search.touched, paths));
        p.local += p.wire.size() - start;
    }
    return p;
}

bool verify(const Bytes& wire, const Client& c, const float* q, int k, int ef,
            double* auth = nullptr, double* replay = nullptr) {
    try {
        auto start = Clock::now();
        Reader r{wire};
        auto answer = read_header(r, q, c.dim, k, ef);
        auto main = r.u32();
        require(main < c.buckets, "bad main bucket");
        auto len = r.u32();
        r.need(len);
        Bytes route(r.b.begin() + r.p, r.b.begin() + r.p + len);
        r.p += len;
        Reader rr{route};
        std::map<uint32_t, Descriptor> ds;
        require(Trie::reconstruct(rr, ds) == c.root, "trie authentication failed");
        rr.end();
        require(ds.count(main), "missing main descriptor");
        uint64_t code = c.lsh.code(q);
        require((main == 0 || code >= ds.at(main).low), "wrong predecessor bucket");
        if (main + 1 < c.buckets) {
            require(ds.count(main + 1) && code < ds.at(main + 1).low,
                    "wrong upper bucket boundary");
        }
        auto expected = select_buckets(main, c.buckets, c.probes);
        auto groups = r.count(12);
        require(groups == expected.size(), "omitted neighbor bucket");

        struct Group {
            uint32_t id;
            std::map<uint32_t, Record> records;
            std::vector<std::pair<uint32_t, uint32_t>> trace;
        };

        std::vector<Group> decoded;
        std::set<uint32_t> bucket_ids;
        for (uint32_t i = 0; i < groups; ++i) {
            Group g;
            g.id = r.u32();
            require(ds.count(g.id) && std::binary_search(expected.begin(), expected.end(), g.id) &&
                        bucket_ids.insert(g.id).second,
                    "unexpected bucket");
            auto& d = ds.at(g.id);
            require(d.id < c.buckets, "invalid bucket ID");
            auto n = r.count(20);
            std::map<uint32_t, Hash> leaves;
            std::set<uint32_t> global_ids;
            for (uint32_t j = 0; j < n; ++j) {
                auto [local, x] = read_record(r, c.dim, d.n);
                require(x.id < uint32_t(c.n) && global_ids.insert(x.id).second, "bad vector ID");
                leaves[local] = leaf(x);
                require(g.records.emplace(local, std::move(x)).second, "duplicate local ID");
            }
            g.trace = read_trace(r);
            auto p = read_proof(r);
            require(Tree::verify(d.root, d.n, leaves, p), "bucket authentication failed");
            decoded.push_back(std::move(g));
        }
        r.end();
        if (auth) {
            *auth = ms(start);
        }
        start = Clock::now();
        std::vector<std::pair<float, uint32_t>> candidates;
        for (auto& g : decoded) {
            auto& d = ds.at(g.id);
            auto s = search([&](uint32_t id) -> const Record& { return g.records.at(id); }, d.entry,
                            d.maxlevel, q, c.dim, k, ef);
            require(s.trace == g.trace && s.touched.size() == g.records.size(), "trace mismatch");
            for (auto [distance, id] : s.best) {
                candidates.emplace_back(distance, g.records.at(id).id);
            }
        }
        std::sort(candidates.begin(), candidates.end());
        require(candidates.size() >= size_t(k), "too few replay results");
        std::set<uint32_t> unique;
        for (int i = 0; i < k; ++i) {
            require(answer[i] == candidates[i].second && unique.insert(answer[i]).second,
                    "top-k mismatch");
        }
        if (replay) {
            *replay = ms(start);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void attacks(const Packet& p, const Client& c, const float* q, int k, int ef,
             const std::string& output) {
    std::ofstream f(output + "/tamper_tests.csv");
    f << "attack,rejected\n";
    auto test = [&](std::string name, Bytes wire) {
        bool rejected = !verify(wire, c, q, k, ef);
        f << name << ',' << rejected << '\n';
        require(rejected, "tamper self-check failed: " + name);
    };
    Bytes x = p.wire;
    x[p.vector_offset] ^= 1;
    test("vector_bit_flip", x);
    x = p.wire;
    x[p.adj_offset] ^= 1;
    test("adjacency_bit_flip", x);
    x = p.wire;
    x[48] ^= 1;
    test("result_id_change", x);
    x = p.wire;
    x.back() ^= 1;
    test("local_proof_bit_flip", x);
    x = p.wire;
    x[p.route_offset + p.route - 1] ^= 1;
    test("trie_proof_bit_flip", x);
    x = p.wire;
    x[48 + k * 4] ^= 1;
    test("wrong_main_bucket", x);
    x = p.wire;
    x[p.trace_offset] ^= 1;
    test("jump_trace_change", x);
    x = p.wire;
    x.pop_back();
    test("truncated_vo", x);
    x = p.wire;
    Reader rr{p.wire};
    rr.p = p.count_offset;
    auto count = rr.u32();
    x.erase(x.begin() + p.first_begin, x.begin() + p.first_end);
    for (int j = 0; j < 4; ++j) {
        x[p.count_offset + j] = uint8_t((count - 1) >> (8 * j));
    }
    test("omitted_record", x);
    Client stale = c;
    stale.root[0] ^= 1;
    bool rejected = !verify(p.wire, stale, q, k, ef);
    f << "wrong_trusted_root," << rejected << '\n';
    require(rejected, "wrong root check failed");
    x = p.wire;
    x[4] ^= 1;
    test("wrong_query_binding", x);
}

int main(int argc, char** argv) try {
    keccak = false;
    require(hex(hash({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "SHA-256 known-answer test failed");
    Config c = args(argc, argv);
    build_threads = c.buildthreads;
    auto base = vann::read_fvecs(c.dataset + "/sift_base.fvecs", c.base), queries = read_queries(c);
    require(base.rows() == size_t(c.base) && queries.rows() == size_t(c.queries) &&
                base.dimension == queries.dimension,
            "dataset too short/dimension mismatch");
    if (c.metric == "cosine") {
        normalize(base);
        normalize(queries);
    }
    auto data = data_hash(base);
    auto start = Clock::now();
    auto gt = ground_truth(base, queries, *std::max_element(c.ks.begin(), c.ks.end()));
    double gt_ms = ms(start);
    start = Clock::now();
    LSH lsh(base.dimension, c.width, c.seed);
    std::vector<std::pair<uint64_t, int>> order;
    for (int id = 0; id < c.base; ++id) {
        order.emplace_back(lsh.code(base.row(id)), id);
    }
    std::sort(order.begin(), order.end());
    std::vector<Bucket> buckets;
    std::vector<Descriptor> descriptors;
    double ads_ms = 0;
    size_t ads_bytes = 0;
    for (int begin = 0; begin < c.base; begin += c.bucket) {
        int end = std::min(begin + c.bucket, c.base);
        std::vector<int> ids;
        Bytes idbytes;
        for (int j = begin; j < end; ++j) {
            ids.push_back(order[j].second);
            u32(idbytes, order[j].second);
        }
        std::string cache = c.cache + "/" + hex(data).substr(0, 16) + "_" +
                            hex(hash(idbytes)).substr(0, 16) + "_M" + std::to_string(c.M) + "_efc" +
                            std::to_string(c.efc) + ".index";
        Bucket bucket;
        bucket.graph = std::make_unique<Graph>(base, ids, c.M, c.efc, cache);
        auto adsstart = Clock::now();
        std::vector<Hash> leaves;
        for (auto& r : bucket.graph->records) {
            leaves.push_back(leaf(r));
        }
        bucket.tree = std::make_unique<Tree>(std::move(leaves));
        for (auto& level : bucket.tree->levels) {
            ads_bytes += level.size() * 32;
        }
        ads_ms += ms(adsstart);
        descriptors.push_back({uint32_t(buckets.size()), uint32_t(ids.size()), bucket.graph->entry,
                               bucket.graph->maxlevel, order[begin].first, order[end - 1].first,
                               bucket.tree->root()});
        buckets.push_back(std::move(bucket));
    }
    double index_ms = ms(start) - ads_ms;
    start = Clock::now();
    Trie trie(descriptors, 0, descriptors.size());
    ads_ms += ms(start);
    lsh.save(c.output + "/lsh_parameters.txt");
    std::ofstream roots(c.output + "/trusted_roots.txt");
    roots << "trie " << hex(trie.root) << '\n';
    for (auto& d : descriptors) {
        roots << d.id << ' ' << d.n << ' ' << d.low << ' ' << d.high << ' ' << hex(d.root) << '\n';
    }
    std::vector<Row> rows;
    bool tested = false;
    for (int k : c.ks) {
        for (int ef : c.efs) {
            for (int probes : c.probes) {
                for (int repeat = 0; repeat < c.repeats; ++repeat) {
                    for (int qi = 0; qi < c.queries; ++qi) {
                        const float* q = queries.row(qi);
                        Row row{};
                        row.query = qi + c.queryoffset;
                        row.repeat = repeat;
                        row.k = k;
                        row.ef = std::max(ef, k);
                        row.probes = std::min(probes, int(buckets.size()));
                        start = Clock::now();
                        auto code = lsh.code(q);
                        auto it = std::upper_bound(
                            descriptors.begin(), descriptors.end(), code,
                            [](uint64_t value, const Descriptor& d) { return value < d.low; });
                        uint32_t main =
                            it == descriptors.begin() ? 0 : uint32_t(it - descriptors.begin() - 1);
                        auto selected = select_buckets(main, buckets.size(), probes);
                        std::vector<Response> responses;
                        std::vector<std::pair<float, uint32_t>> candidates;
                        for (auto id : selected) {
                            auto& g = *buckets[id].graph;
                            Response r{id, search(
                                               [&](uint32_t local) -> const Record& {
                                                   return g.records.at(local);
                                               },
                                               g.entry, g.maxlevel, q, base.dimension, k, row.ef)};
                            row.visited += r.search.touched.size();
                            row.expanded += r.search.trace.size();
                            for (auto [d, local] : r.search.best) {
                                candidates.emplace_back(d, g.records[local].id);
                            }
                            responses.push_back(std::move(r));
                        }
                        std::sort(candidates.begin(), candidates.end());
                        require(candidates.size() >= size_t(k),
                                "too few candidates; increase probes/bucket size");
                        row.search_ms = ms(start);
                        start = Clock::now();
                        auto p = generate(buckets, descriptors, trie, main, responses, q,
                                          base.dimension, k, row.ef, c.proof == "paths");
                        row.vo_ms = ms(start);
                        Client client{base.dimension,           c.base, row.probes,
                                      uint32_t(buckets.size()), lsh,    trie.root};
                        start = Clock::now();
                        row.valid =
                            verify(p.wire, client, q, k, row.ef, &row.auth_ms, &row.replay_ms);
                        row.verify_ms = ms(start);
                        require(row.valid, "honest verification failed");
                        row.vo_bytes = p.wire.size();
                        row.record_bytes = p.records;
                        row.local_proof_bytes = p.local;
                        row.route_bytes = p.route;
                        row.buckets = selected.size();
                        std::vector<uint32_t> answer;
                        for (int j = 0; j < k; ++j) {
                            answer.push_back(candidates[j].second);
                        }
                        quality(row, answer, gt[qi], base, q);
                        rows.push_back(row);
                        if (!tested) {
                            save(c.output + "/query0.vo", p.wire);
                            attacks(p, client, q, k, row.ef, c.output);
                            tested = true;
                        }
                    }
                }
            }
        }
    }
    output(c, rows, index_ms, ads_ms, gt_ms, data, "VLAH-algorithmic", ads_bytes);
    std::cout << "PASS honest verification, 11 tamper cases. Results: " << c.output << '\n';
    return 0;
} catch (const std::exception& e) {

    std::cerr << "error: " << e.what() << '\n';
    return 1;
}

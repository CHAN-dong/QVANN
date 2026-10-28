#include "common.h"
using namespace paper_repro;

struct Anchor {
    int dim;
    uint32_t n, entry, maxlevel, shard;
    std::vector<Hash> nodes, identifiers;
};

struct ADS {
    std::vector<Tree> nodes, identifiers;
    Anchor anchor;
    size_t bytes = 0;

    ADS(const Graph& g, uint32_t shard, int dim)
        : anchor{dim, uint32_t(g.records.size()), g.entry, g.maxlevel, shard, {}, {}} {
        for (uint32_t begin = 0; begin < g.records.size(); begin += shard) {
            std::vector<Hash> a, b;
            for (uint32_t id = begin; id < std::min(begin + shard, uint32_t(g.records.size()));
                 ++id) {
                a.push_back(leaf(g.records[id]));
                b.push_back(identifier_leaf(g.records[id]));
            }
            nodes.emplace_back(std::move(a));
            identifiers.emplace_back(std::move(b));
            anchor.nodes.push_back(nodes.back().root());
            anchor.identifiers.push_back(identifiers.back().root());
        }
        for (auto& collection : {&nodes, &identifiers}) {
            for (auto& t : *collection) {
                for (auto& l : t.levels) {
                    bytes += l.size() * 32;
                }
            }
        }
    }
};

struct Packet {
    Bytes wire;
    size_t record_bytes = 0, proof_bytes = 0, vector_offset = 0, adj_offset = 0, first_begin = 0,
           first_end = 0, count_offset = 0;
};

Packet generate(const Graph& g, const ADS& ads, const Search& s, const float* q, int dim, int k,
                int ef, bool paths) {
    Packet p;
    std::vector<uint32_t> answer;
    for (auto [d, id] : s.best) {
        answer.push_back(g.records[id].id);
    }
    write_header(p.wire, q, dim, k, ef, answer);
    p.count_offset = p.wire.size();
    u32(p.wire, s.touched.size());
    size_t begin = p.wire.size();
    for (auto id : s.touched) {
        size_t start = p.wire.size();
        write_record(p.wire, id, g.records[id]);
        if (!p.vector_offset) {
            p.first_begin = start;
            p.first_end = p.wire.size();
            p.vector_offset = start + 12;
            p.adj_offset = start + 12 + dim * 4 + 8;
        }
    }
    p.record_bytes = p.wire.size() - begin;
    write_trace(p.wire, s);
    std::map<uint32_t, std::set<uint32_t>> targets, ids;
    for (auto id : s.touched) {
        targets[id / ads.anchor.shard].insert(id % ads.anchor.shard);
    }
    for (auto [d, id] : s.best) {
        ids[id / ads.anchor.shard].insert(id % ads.anchor.shard);
    }
    u32(p.wire, targets.size());
    begin = p.wire.size();
    for (auto& [shard, t] : targets) {
        u32(p.wire, shard);
        write_proof(p.wire, ads.nodes[shard].prove(t, paths));
        write_proof(p.wire,
                    ids[shard].empty() ? Proof{} : ads.identifiers[shard].prove(ids[shard], paths));
    }
    p.proof_bytes = p.wire.size() - begin;
    return p;
}

bool verify(const Bytes& wire, const Anchor& a, const float* q, int k, int ef,
            double* auth = nullptr, double* replay = nullptr) {
    try {
        auto begin = Clock::now();
        Reader r{wire};
        auto answer = read_header(r, q, a.dim, k, ef);
        std::map<uint32_t, Record> records;
        auto count = r.count(20);
        for (uint32_t i = 0; i < count; ++i) {
            auto [local, x] = read_record(r, a.dim, a.n);
            require(local == x.id, "identifier/position mismatch");
            require(records.emplace(local, std::move(x)).second, "duplicate node");
        }
        auto trace = read_trace(r);
        std::map<uint32_t, std::map<uint32_t, Hash>> targets, ids;
        for (auto& [local, x] : records) {
            targets[local / a.shard][local % a.shard] = leaf(x);
        }
        std::set<uint32_t> unique_answer;
        for (auto id : answer) {
            require(unique_answer.insert(id).second, "duplicate answer");
            require(records.count(id), "missing answer");
            ids[id / a.shard][id % a.shard] = identifier_leaf(records.at(id));
        }
        count = r.count(12);
        require(count == targets.size(), "missing shard");
        std::set<uint32_t> shards;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t shard = r.u32();
            require(shard < a.nodes.size() && shards.insert(shard).second && targets.count(shard),
                    "bad shard");
            auto np = read_proof(r), ip = read_proof(r);
            uint32_t n = std::min(a.shard, a.n - shard * a.shard);
            require(Tree::verify(a.nodes[shard], n, targets[shard], np),
                    "node authentication failed");
            if (!ids[shard].empty()) {
                require(Tree::verify(a.identifiers[shard], n, ids[shard], ip),
                        "identifier authentication failed");
            } else {
                require(ip.empty(), "unexpected identifier proof");
            }
        }
        r.end();
        if (auth) {
            *auth = ms(begin);
        }
        begin = Clock::now();
        auto getter = [&](uint32_t id) -> const Record& { return records.at(id); };
        auto local = search(getter, a.entry, a.maxlevel, q, a.dim, k, ef);
        std::vector<uint32_t> actual;
        for (auto [d, id] : local.best) {
            actual.push_back(records.at(id).id);
        }
        require(actual == answer && trace == local.trace, "search replay mismatch");
        require(local.touched.size() == records.size(), "extraneous/missing disclosure");
        if (replay) {
            *replay = ms(begin);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void attacks(const Packet& p, const Anchor& a, const float* q, int k, int ef,
             const std::string& output) {
    std::ofstream f(output + "/tamper_tests.csv");
    f << "attack,rejected\n";
    auto test = [&](std::string name, Bytes wire) {
        bool rejected = !verify(wire, a, q, k, ef);
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
    test("proof_bit_flip", x);
    x = p.wire;
    x.pop_back();
    test("truncated_vo", x);
    x = p.wire;
    uint32_t count = uint32_t(p.first_end > p.first_begin);
    (void)count;
    Reader rr{p.wire};
    rr.p = p.count_offset;
    auto records = rr.u32();
    x.erase(x.begin() + p.first_begin, x.begin() + p.first_end);
    for (int j = 0; j < 4; ++j) {
        x[p.count_offset + j] = uint8_t((records - 1) >> (8 * j));
    }
    test("omitted_record", x);
    Anchor stale = a;
    for (auto& h : stale.nodes) {
        h[0] ^= 1;
    }
    bool rejected = !verify(p.wire, stale, q, k, ef);
    f << "wrong_trusted_roots," << rejected << '\n';
    require(rejected, "stale root check failed");
    x = p.wire;
    x[4] ^= 1;
    test("wrong_query_binding", x);
}

int main(int argc, char** argv) try {
    keccak = true;
    require(hex(hash({})) == "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470",
            "Keccak known-answer test failed");
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
    int maxk = *std::max_element(c.ks.begin(), c.ks.end());
    auto start = Clock::now();
    auto gt = ground_truth(base, queries, maxk);
    double gt_ms = ms(start);
    std::vector<int> ids(c.base);
    std::iota(ids.begin(), ids.end(), 0);
    std::string cache = c.cache + "/" + hex(data).substr(0, 16) + "_M" + std::to_string(c.M) +
                        "_efc" + std::to_string(c.efc) + ".index";
    bool cached = std::filesystem::exists(cache);
    start = Clock::now();
    Graph graph(base, ids, c.M, c.efc, cache);
    double index_ms = ms(start);
    start = Clock::now();
    ADS ads(graph, c.shard, base.dimension);
    double ads_ms = ms(start);
    std::ofstream roots(c.output + "/trusted_roots.txt");
    roots << "entry " << graph.entry << " maxlevel " << graph.maxlevel << " dim " << base.dimension
          << " n " << base.rows() << " shard_size " << c.shard << '\n';
    for (size_t i = 0; i < ads.nodes.size(); ++i) {
        roots << i << ' ' << hex(ads.anchor.nodes[i]) << ' ' << hex(ads.anchor.identifiers[i])
              << '\n';
    }
    std::vector<Row> rows;
    bool tested = false;
    for (int k : c.ks) {
        for (int ef : c.efs) {
            for (int repeat = 0; repeat < c.repeats; ++repeat) {
                for (int qi = 0; qi < c.queries; ++qi) {
                    Row row{};
                    row.query = qi + c.queryoffset;
                    row.repeat = repeat;
                    row.k = k;
                    row.ef = std::max(k, ef);
                    row.probes = 1;
                    const float* q = queries.row(qi);
                    start = Clock::now();
                    auto s =
                        search([&](uint32_t id) -> const Record& { return graph.records.at(id); },
                               graph.entry, graph.maxlevel, q, base.dimension, k, row.ef);
                    row.search_ms = ms(start);
                    start = Clock::now();
                    auto p =
                        generate(graph, ads, s, q, base.dimension, k, row.ef, c.proof == "paths");
                    row.vo_ms = ms(start);
                    start = Clock::now();
                    row.valid =
                        verify(p.wire, ads.anchor, q, k, row.ef, &row.auth_ms, &row.replay_ms);
                    row.verify_ms = ms(start);
                    require(row.valid, "honest query rejected");
                    row.vo_bytes = p.wire.size();
                    row.record_bytes = p.record_bytes;
                    row.local_proof_bytes = p.proof_bytes;
                    row.visited = s.touched.size();
                    row.expanded = s.trace.size();
                    std::set<uint32_t> accessed_shards;
                    for (auto id : s.touched) {
                        accessed_shards.insert(id / c.shard);
                    }
                    row.buckets = accessed_shards.size();
                    std::vector<uint32_t> answer;
                    for (auto [d, id] : s.best) {
                        answer.push_back(graph.records[id].id);
                    }
                    quality(row, answer, gt[qi], base, q);
                    rows.push_back(row);
                    if (!tested) {
                        save(c.output + "/query0.vo", p.wire);
                        attacks(p, ads.anchor, q, k, row.ef, c.output);
                        tested = true;
                    }
                }
            }
        }
    }
    output(c, rows, index_ms, ads_ms, gt_ms, data,
           c.shard >= c.base ? "ANNProof-U-algorithmic" : "ANNProof-algorithmic", ads.bytes);
    std::ofstream(c.output + "/index_state.txt")
        << (cached ? "cached index load" : "new index construction") << '\n';
    std::cout << "PASS honest verification, 8 tamper cases. Results: " << c.output << '\n';
    return 0;
} catch (const std::exception& e) {

    std::cerr << "error: " << e.what() << '\n';
    return 1;
}

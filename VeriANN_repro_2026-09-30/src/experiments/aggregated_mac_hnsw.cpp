#define main relaxed_hnsw_embedded_main
#include "relaxed_hnsw_harvest.cpp"
#undef main

#include <openssl/hmac.h>
#include <openssl/rand.h>

namespace {
using Tag = std::array<std::uint8_t, 16>;

class MacSQ6 {
public:
    explicit MacSQ6(const vann::HnswIndex& h)
        : d_(h.dimension()), lo_(d_, INFINITY), span_(d_), code_(h.size() * d_) {
        std::vector<float> hi(d_, -INFINITY);
        for (std::size_t id = 0; id < h.size(); ++id) for (int j = 0; j < d_; ++j) {
            lo_[j] = std::min(lo_[j], h.vector(id)[j]);
            hi[j] = std::max(hi[j], h.vector(id)[j]);
        }
        for (int j = 0; j < d_; ++j) span_[j] = hi[j] - lo_[j];
        for (std::size_t id = 0; id < h.size(); ++id) for (int j = 0; j < d_; ++j) {
            double u = span_[j] > 0 ? (h.vector(id)[j] - lo_[j]) / span_[j] : 0;
            code_[id * d_ + j] = std::uint8_t(std::clamp(int(std::floor(u * 64)), 0, 63));
        }
    }
    std::vector<std::uint8_t> record(int id) const {
        std::vector<std::uint8_t> out;
        const std::string domain = "vann/sq6/v1";
        out.insert(out.end(), domain.begin(), domain.end());
        vann::append_bytes(out, std::uint32_t(id));
        std::uint32_t acc = 0; int used = 0;
        for (int j = 0; j < d_; ++j) {
            acc |= std::uint32_t(code_[std::size_t(id) * d_ + j]) << used;
            used += 6;
            while (used >= 8) { out.push_back(std::uint8_t(acc)); acc >>= 8; used -= 8; }
        }
        if (used) out.push_back(std::uint8_t(acc));
        return out;
    }
private:
    int d_; std::vector<float> lo_, span_; std::vector<std::uint8_t> code_;
};

std::vector<std::uint8_t> raw_record(const vann::HnswIndex& h, int id) {
    std::vector<std::uint8_t> out;
    const std::string domain = "vann/raw/v1";
    out.insert(out.end(), domain.begin(), domain.end());
    vann::append_bytes(out, std::uint32_t(id));
    const auto* p = reinterpret_cast<const std::uint8_t*>(h.vector(id));
    out.insert(out.end(), p, p + h.dimension() * sizeof(float));
    return out;
}

std::vector<std::uint8_t> adjacency_record(const vann::HnswIndex& h, int id, int level) {
    std::vector<std::uint8_t> out;
    const std::string domain = "vann/adjacency/v1";
    out.insert(out.end(), domain.begin(), domain.end());
    vann::append_bytes(out, std::uint32_t(id));
    vann::append_bytes(out, std::uint32_t(level));
    const auto neighbors = h.neighbors(id, level);
    vann::append_bytes(out, std::uint32_t(neighbors.size()));
    for (int x : neighbors) vann::append_bytes(out, std::uint32_t(x));
    return out;
}

Tag mac(const std::array<std::uint8_t, 32>& key, const std::vector<std::uint8_t>& record) {
    unsigned int length = 0; std::array<std::uint8_t, 32> full{};
    HMAC(EVP_sha256(), key.data(), int(key.size()), record.data(), record.size(), full.data(), &length);
    Tag out{}; std::copy_n(full.begin(), out.size(), out.begin()); return out;
}

Tag coefficient(const Tag& nonce, std::uint8_t domain, int id, int level = -1) {
    std::vector<std::uint8_t> input(nonce.begin(), nonce.end());
    input.push_back(domain); vann::append_bytes(input, std::uint32_t(id));
    vann::append_bytes(input, std::uint32_t(level));
    const vann::Hash h = vann::sha256(input.data(), input.size());
    Tag out{}; std::copy_n(h.begin(), out.size(), out.begin()); return out;
}

Tag gf_mul(const Tag& x, const Tag& y) {
    Tag z{}, v = y;
    for (int bit = 0; bit < 128; ++bit) {
        if ((x[bit / 8] >> (7 - bit % 8)) & 1U)
            for (int j = 0; j < 16; ++j) z[j] ^= v[j];
        const bool lsb = (v[15] & 1U) != 0;
        for (int j = 15; j > 0; --j) v[j] = std::uint8_t((v[j] >> 1) | (v[j - 1] << 7));
        v[0] >>= 1; if (lsb) v[0] ^= 0xe1U;
    }
    return z;
}

void aggregate(Tag& total, const Tag& tag, const Tag& nonce,
               std::uint8_t domain, int id, int level = -1) {
    Tag value = gf_mul(coefficient(nonce, domain, id, level), tag);
    for (int j = 0; j < 16; ++j) total[j] ^= value[j];
}

struct Timings { double sq_ms=0, adjacency_ms=0, raw_ms=0, server_ms=0, client_ms=0; };
}

int main() try {
    using namespace vann;
    Config c; c.queries = 100;
    Matrix base = read_fvecs(c.dataset + "/sift_base.fvecs", c.base);
    Matrix queries = read_fvecs(c.dataset + "/sift_query.fvecs", c.queries);
    HnswIndex h = build_hnsw(base, c.M, c.efc, c.cache);
    base.values.clear(); base.values.shrink_to_fit();
    NestedSQ routing_sq(h); MacSQ6 records(h);
    std::array<std::uint8_t, 32> key{};
    if (RAND_bytes(key.data(), int(key.size())) != 1) throw std::runtime_error("RAND_bytes failed for MAC key");

    Timings time;
    std::vector<Tag> sq_tags(h.size()), raw_tags(h.size());
    auto begin = std::chrono::steady_clock::now();
    for (std::size_t id = 0; id < h.size(); ++id) sq_tags[id] = mac(key, records.record(id));
    time.sq_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    begin = std::chrono::steady_clock::now();
    for (std::size_t id = 0; id < h.size(); ++id) raw_tags[id] = mac(key, raw_record(h, id));
    time.raw_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::vector<std::vector<Tag>> adjacency_tags(h.size()); std::size_t adjacency_tag_count = 0;
    begin = std::chrono::steady_clock::now();
    for (std::size_t id = 0; id < h.size(); ++id) {
        const int levels = h.index->hnsw.levels[id]; adjacency_tags[id].resize(levels);
        for (int level = 0; level < levels; ++level) {
            adjacency_tags[id][level] = mac(key, adjacency_record(h, id, level)); ++adjacency_tag_count;
        }
    }
    time.adjacency_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();

    const auto gt = ground_truth(h, queries, c.k);
    double hits=0, disclosed=0, expanded=0, raw=0, opened=0; std::uint64_t failures=0;
    for (int qi = 0; qi < c.queries; ++qi) {
        Trace trace = search(h, routing_sq, queries.row(qi), c.k, 6, 5, 12, c.upper_steps);
        disclosed += trace.discovered.size(); expanded += trace.expanded.size();
        raw += trace.raw.size(); opened += trace.opened.size();
        std::unordered_set<int> answer(trace.answer.begin(), trace.answer.end());
        for (int id : gt[qi]) hits += answer.count(id);
        Tag nonce{};
        if (RAND_bytes(nonce.data(), int(nonce.size())) != 1)
            throw std::runtime_error("RAND_bytes failed for query nonce");
        Tag server_sq{}, server_adj{}, server_raw{};
        begin = std::chrono::steady_clock::now();
        for (int id : trace.discovered) aggregate(server_sq, sq_tags[id], nonce, 1, id);
        for (auto x : trace.opened) aggregate(server_adj, adjacency_tags[x.local_id][x.level], nonce, 2, x.local_id, x.level);
        for (int id : trace.raw) aggregate(server_raw, raw_tags[id], nonce, 3, id);
        time.server_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        Tag client_sq{}, client_adj{}, client_raw{};
        begin = std::chrono::steady_clock::now();
        for (int id : trace.discovered) aggregate(client_sq, mac(key, records.record(id)), nonce, 1, id);
        for (auto x : trace.opened) aggregate(client_adj, mac(key, adjacency_record(h, x.local_id, x.level)), nonce, 2, x.local_id, x.level);
        for (int id : trace.raw) aggregate(client_raw, mac(key, raw_record(h, id)), nonce, 3, id);
        time.client_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        failures += server_sq != client_sq || server_adj != client_adj || server_raw != client_raw;
    }
    // Baseline payload is independently measured by the existing separated accounting.
    const double payload_kib = 78.1153 + 11.37 + 4.89957 + 4.25835;
    const double auth_bytes = 3 * sizeof(Tag), total_kib = payload_kib + auth_bytes / 1024.0;
    const double nq = c.queries;
    std::cout << std::fixed << std::setprecision(6)
              << "precomputed_sq6_tags=" << sq_tags.size() << '\n'
              << "precomputed_adjacency_tags=" << adjacency_tag_count << '\n'
              << "precomputed_raw_tags=" << raw_tags.size() << '\n'
              << "avg_query_sq6_tags=" << disclosed/nq << '\n'
              << "avg_query_adjacency_tags=" << opened/nq << '\n'
              << "avg_query_raw_tags=" << raw/nq << '\n'
              << "recall_at_10=" << hits/(nq*c.k) << '\n'
              << "avg_disclosed=" << disclosed/nq << '\n'
              << "avg_expanded=" << expanded/nq << '\n'
              << "aggregate_authentication_bytes=" << auth_bytes << '\n'
              << "total_authentication_bytes=" << auth_bytes << '\n'
              << "payload_kib=" << payload_kib << '\n'
              << "total_vo_kib=" << total_kib << '\n'
              << "sq6_tag_pregeneration_ms=" << time.sq_ms << '\n'
              << "adjacency_tag_pregeneration_ms=" << time.adjacency_ms << '\n'
              << "raw_tag_pregeneration_ms=" << time.raw_ms << '\n'
              << "total_tag_pregeneration_ms=" << time.sq_ms+time.adjacency_ms+time.raw_ms << '\n'
              << "avg_server_aggregation_ms=" << time.server_ms/nq << '\n'
              << "avg_client_verification_ms=" << time.client_ms/nq << '\n'
              << "tag_storage_bytes_per_object=" << 16.0*(2.0+double(adjacency_tag_count)/h.size()) << '\n'
              << "verification_failures=" << failures << '\n';
    if (failures) throw std::runtime_error("aggregate MAC verification failed");
    return 0;
} catch (const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 1; }

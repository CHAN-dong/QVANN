#include "verify/auth.h"
#include "index/merkle.h"
#include "optimize/expansion/record.h"

namespace qvann {
Evidence authenticate(const Bytes& wire, const Digest& a, const float* q, int k, int E) {
    Reader r{wire};
    Evidence evidence;
    evidence.answer = read_header(r, q, a.dim, k, E);
    auto& codes = evidence.codes;
    auto& deltas = evidence.deltas;
    auto& raw = evidence.raw;
    auto& links = evidence.links;
    std::map<Id, Hash> route_leaves, delta_leaves, raw_leaves;
    auto id_read = [&]() {
        Id id = r.u32();
        require(id < a.n, "invalid object id");
        return id;
    };
    auto blob = [&](size_t size) {
        r.need(size);
        Bytes b(wire.begin() + r.p, wire.begin() + r.p + size);
        r.p += size;
        return b;
    };
    Id count = r.count(4 + a.dim / 2);
    for (Id i = 0; i < count; ++i) {
        Id id = id_read();
        require(codes.emplace(id, blob(a.dim / 2)).second, "duplicate SQ4");
    }
    require(codes.at(a.entry) == a.entry_sq, "entry code changed");
    count = r.count(16);
    for (Id i = 0; i < count; ++i) {
        Id id = id_read(), level = r.u32(), pos = r.u32();
        require(level <= a.maxlevel && pos < a.route_count, "invalid route record");
        std::vector<Id> ns;
        Id degree = r.count(4);
        for (Id j = 0; j < degree; ++j) {
            ns.push_back(id_read());
        }
        require(std::is_sorted(ns.begin(), ns.end()) &&
                    std::adjacent_find(ns.begin(), ns.end()) == ns.end(),
                "noncanonical adjacency");
        require(links.emplace(Key{id, level}, ns).second, "duplicate opened adjacency");
        auto h = expansion_leaf(a.snapshot, id, level, ns, a.dim,
                                [&](Id nb) { return codes.at(nb).data(); });
        require(route_leaves.emplace(pos, h).second, "duplicate route position");
    }
    count = r.count(8 + a.dim / 4);
    for (Id i = 0; i < count; ++i) {
        Id id = id_read(), pos = r.u32();
        auto b = blob(a.dim / 4);
        require(codes.count(id) && deltas.emplace(id, b).second, "invalid delta record");
        require(pos < a.n &&
                    delta_leaves
                        .emplace(pos, refinement_leaf("record/delta46/v1", id, b.data(), b.size()))
                        .second,
                "invalid delta position");
    }
    count = r.count(8 + a.dim * 4);
    for (Id i = 0; i < count; ++i) {
        Id id = id_read(), pos = r.u32();
        Bytes b;
        std::vector<float> v;
        for (int j = 0; j < a.dim; ++j) {
            float x = r.f32();
            v.push_back(x);
            f32(b, x);
        }
        require(deltas.count(id) && raw.emplace(id, v).second, "invalid raw record");
        require(
            pos < a.n &&
                raw_leaves.emplace(pos, refinement_leaf("record/raw/v1", id, b.data(), b.size()))
                    .second,
            "invalid raw position");
    }
    require(check_proof(r, a.route, a.route_count, route_leaves), "route authentication failed");
    require(check_proof(r, a.delta, a.n, delta_leaves), "delta authentication failed");
    require(check_proof(r, a.raw, a.n, raw_leaves), "raw authentication failed");
    r.end();

    return evidence;
}
} // namespace qvann

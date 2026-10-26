#include "query/proof.h"
#include "index/graph.h"
#include "optimize/refinement/refine.h"

namespace qvann {
VerificationObject make_proof(const Graph& h, const Lookup& codec, const Index& ads, const Trace& t,
                              const std::vector<Id>& answer, float tau, const float* q, int k,
                              int E) {
    VerificationObject out;
    out.answer = answer;
    auto& b = out.wire;
    write_header(b, q, codec.dim, k, E, answer);
    u32(b, t.disclosed.size());
    for (Id id : t.disclosed) {
        u32(b, id);
        if (!out.sq_offset) {
            out.sq_offset = b.size();
        }
        auto s = codec.sq(id);
        b.insert(b.end(), s, s + codec.dim / 2);
    }
    u32(b, t.opened.size());
    std::set<Id> route_targets, delta_targets, raw_targets;
    for (auto [id, l] : t.opened) {
        Id pos = ads.route_base[id] + l;
        route_targets.insert(pos);
        u32(b, id);
        u32(b, l);
        u32(b, pos);
        auto ns = neighbors(h, id, l);
        u32(b, ns.size());
        for (Id nb : ns) {
            if (!out.adj_offset) {
                out.adj_offset = b.size();
            }
            u32(b, nb);
        }
    }
    std::vector<Id> refined, raw;
    for (Id id : t.disclosed) {
        if (codec.lower(q, codec.sq(id)) <= tau) {
            refined.push_back(id);
            if (codec.lower(q, codec.sq(id), codec.delta(id)) <= tau ||
                std::find(answer.begin(), answer.end(), id) != answer.end()) {
                raw.push_back(id);
            }
        }
    }
    u32(b, refined.size());
    for (Id id : refined) {
        u32(b, id);
        u32(b, ads.position[id]);
        delta_targets.insert(ads.position[id]);
        auto s = codec.delta(id);
        if (!out.delta_offset) {
            out.delta_offset = b.size();
        }
        b.insert(b.end(), s, s + codec.dim / 4);
    }
    u32(b, raw.size());
    for (Id id : raw) {
        u32(b, id);
        u32(b, ads.position[id]);
        raw_targets.insert(ads.position[id]);
        if (!out.raw_offset) {
            out.raw_offset = b.size();
        }
        for (int j = 0; j < codec.dim; ++j) {
            f32(b, h.vector(id)[j]);
        }
    }
    out.refined = refined.size();
    out.raw = raw.size();
    out.records = b.size();
    auto before = b.size();
    proof_write(b, *ads.route, route_targets);
    out.route_proof = b.size() - before;
    proof_write(b, *ads.delta, delta_targets);
    proof_write(b, *ads.raw, raw_targets);
    out.proofs = b.size() - before;
    return out;
}
} // namespace qvann

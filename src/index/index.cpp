#include "index/index.h"
#include "index/graph.h"
#include "optimize/expansion/ordering.h"
#include "optimize/expansion/record.h"

namespace qvann {
Index::Index(const Graph& h, const Lookup& codec, const Hash& snapshot) {
    const size_t n = h.size();
    route_base.resize(n + 1);
    for (size_t id = 0; id < n; ++id) {
        route_base[id + 1] = route_base[id] + h.index->hnsw.levels[id];
    }
    std::vector<Hash> leaves(route_base.back());
    for (Id id = 0; id < n; ++id) {
        for (int l = 0; l < h.index->hnsw.levels[id]; ++l) {
            leaves[route_base[id] + l] =
                expansion_leaf(snapshot, id, l, neighbors(h, id, l), codec.dim,
                               [&](Id nb) { return codec.sq(nb); });
        }
    }
    route = std::make_unique<MerkleTree>(std::move(leaves));
    int components = 0;
    auto order = bfs_order(h, components);
    position.resize(n);
    leaves.resize(n);
    std::vector<Hash> raw_leaves(n);
    for (Id pos = 0; pos < n; ++pos) {
        Id id = order[pos];
        position[id] = pos;
        leaves[pos] = refinement_leaf("record/delta46/v1", id, codec.delta(id), codec.dim / 4);
        raw_leaves[pos] = refinement_leaf(
            "record/raw/v1", id, reinterpret_cast<const uint8_t*>(h.vector(id)), codec.dim * 4);
    }
    delta = std::make_unique<MerkleTree>(std::move(leaves));
    raw = std::make_unique<MerkleTree>(std::move(raw_leaves));
    anchor = {Id(n),
              Id(h.index->hnsw.entry_point),
              Id(h.index->hnsw.max_level),
              route_base.back(),
              codec.dim,
              snapshot,
              route->root(),
              delta->root(),
              raw->root(),
              Bytes(codec.sq(h.index->hnsw.entry_point),
                    codec.sq(h.index->hnsw.entry_point) + codec.dim / 2)};
    std::cout << "[Index] BFS components=" << components << " route_leaves=" << route_base.back()
              << std::endl;
}
} // namespace qvann

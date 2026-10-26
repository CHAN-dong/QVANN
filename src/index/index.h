#pragma once

#include "common/digest.h"
#include "index/merkle.h"
#include "optimize/traversal/lookup.h"

namespace qvann {

struct Graph;

struct Index {
    std::vector<Id> route_base, position;
    std::unique_ptr<MerkleTree> route, delta, raw;
    Digest anchor{};
    Index(const Graph& graph, const Lookup& codec, const Hash& snapshot);
};

} // namespace qvann

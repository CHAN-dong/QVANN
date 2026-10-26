#pragma once

#include "index/graph.h"
#include "optimize/traversal/routing.h"

namespace qvann {

Trace search(const Graph& graph, const Lookup& codec, const float* query, int budget, Id entry,
             Id maxlevel);

} // namespace qvann

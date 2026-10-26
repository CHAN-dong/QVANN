#include "query/search.h"

namespace qvann {

Trace search(const Graph& graph, const Lookup& codec, const float* query, int budget, Id entry,
             Id maxlevel) {
    return route(
        codec, query, budget, entry, maxlevel, [&](Id id) { return codec.sq(id); },
        [&](Id id, Id level) { return neighbors(graph, id, level); });
}

} // namespace qvann

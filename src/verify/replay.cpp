#include "verify/replay.h"
#include "optimize/traversal/routing.h"

namespace qvann {

Trace replay(const Evidence& evidence, const Digest& a, const Lookup& codec, const float* q,
             int E) {
    const auto& codes = evidence.codes;
    const auto& links = evidence.links;
    auto trace = route(
        codec, q, E, a.entry, a.maxlevel, [&](Id id) { return codes.at(id).data(); },
        [&](Id id, Id level) { return links.at({id, level}); });
    require(trace.disclosed.size() == codes.size() && trace.opened.size() == links.size(),
            "disclosure/trace mismatch");
    for (Id id : trace.disclosed) {
        require(codes.count(id), "missing discovered node");
    }

    return trace;
}
} // namespace qvann

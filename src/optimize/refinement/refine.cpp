#include "optimize/refinement/refine.h"

namespace qvann {

Refinement select_refinement(const Lookup& codec, const float* q, const std::vector<Id>& candidates,
                             const std::vector<Id>& answer, float tau) {
    std::vector<Id> refined, raw;
    for (Id id : candidates) {
        if (codec.lower(q, codec.sq(id)) <= tau) {
            refined.push_back(id);
            if (codec.lower(q, codec.sq(id), codec.delta(id)) <= tau ||
                std::find(answer.begin(), answer.end(), id) != answer.end()) {
                raw.push_back(id);
            }
        }
    }

    return {std::move(refined), std::move(raw)};
}
} // namespace qvann

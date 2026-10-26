#pragma once

#include "optimize/traversal/lookup.h"

namespace qvann {

struct Refinement {
    std::vector<Id> delta, raw;
};

Refinement select_refinement(const Lookup& codec, const float* query,
                             const std::vector<Id>& candidates, const std::vector<Id>& answer,
                             float threshold);

} // namespace qvann

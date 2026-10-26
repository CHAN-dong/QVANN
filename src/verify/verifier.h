#pragma once

#include "common/digest.h"
#include "optimize/traversal/lookup.h"

namespace qvann {

bool verify(const Bytes& wire, const Digest& digest, const Lookup& codec, const float* query, int k,
            int budget, double* auth_ms = nullptr, double* replay_ms = nullptr);

} // namespace qvann

#pragma once

#include "optimize/traversal/lookup.h"
#include "verify/auth.h"

namespace qvann {

Trace replay(const Evidence& evidence, const Digest& digest, const Lookup& codec,
             const float* query, int budget);

} // namespace qvann

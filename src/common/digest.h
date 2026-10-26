#pragma once

#include "common/types.h"

namespace qvann {
struct Digest {
    Id n, entry, maxlevel, route_count;
    int dim;
    Hash snapshot, route, delta, raw;
    Bytes entry_sq;
};
} // namespace qvann

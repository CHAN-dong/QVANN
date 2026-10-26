#pragma once

#include "common/types.h"

namespace qvann {
struct VerificationObject {
    Bytes wire;
    std::vector<Id> answer;
    size_t refined = 0, raw = 0, records = 0, proofs = 0, route_proof = 0, sq_offset = 0,
           delta_offset = 0, raw_offset = 0, adj_offset = 0;
};
} // namespace qvann

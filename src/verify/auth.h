#pragma once

#include "common/codec.h"
#include "common/digest.h"

namespace qvann {

struct Evidence {
    std::vector<Id> answer;
    std::map<Id, Bytes> codes, deltas;
    std::map<Id, std::vector<float>> raw;
    std::map<Key, std::vector<Id>> links;
};

Evidence authenticate(const Bytes& wire, const Digest& digest, const float* query, int k,
                      int budget);

} // namespace qvann

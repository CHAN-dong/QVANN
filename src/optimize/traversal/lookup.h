#pragma once

#include "index/quantizer.h"

namespace qvann {

struct Lookup : Quantizer {
    using Quantizer::Quantizer;
    Lookup(const Lookup& other, bool metadata_only);
    mutable std::vector<float> last_query, score4, lower4, lower6;
    void prepare(const float* query) const;
    float score(const float* query, const uint8_t* sq4) const;
    float lower(const float* query, const uint8_t* sq4, const uint8_t* delta = nullptr) const;
};

} // namespace qvann

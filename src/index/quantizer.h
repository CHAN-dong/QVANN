#pragma once

#include "common/types.h"

namespace qvann {

struct Graph;

struct Quantizer {
    int dim;
    std::vector<float> lo, span;
    Bytes high, low;
    Quantizer(const Quantizer& other, bool metadata_only);
    explicit Quantizer(const Graph& graph);
    const uint8_t* sq(Id id) const;
    const uint8_t* delta(Id id) const;
    float score(const float* query, const uint8_t* sq4) const;
    float lower(const float* query, const uint8_t* sq4, const uint8_t* delta = nullptr) const;
};

} // namespace qvann

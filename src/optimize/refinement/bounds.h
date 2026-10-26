#pragma once

#include "index/quantizer.h"

namespace qvann {

// A null delta selects SQ4; a two-bit delta selects SQ6.
float lower_bound(const Quantizer& quantizer, const float* query, const uint8_t* sq4,
                  const uint8_t* delta = nullptr);

} // namespace qvann

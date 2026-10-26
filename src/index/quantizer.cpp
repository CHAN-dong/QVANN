#include "index/quantizer.h"
#include "optimize/refinement/bounds.h"

namespace qvann {
float Quantizer::lower(const float* query, const uint8_t* sq4, const uint8_t* delta) const {
    return lower_bound(*this, query, sq4, delta);
}

Quantizer::Quantizer(const Quantizer& other, bool)
    : dim(other.dim), lo(other.lo), span(other.span) {}

const uint8_t* Quantizer::sq(Id id) const {
    return high.data() + size_t(id) * (dim / 2);
}

const uint8_t* Quantizer::delta(Id id) const {
    return low.data() + size_t(id) * (dim / 4);
}

float Quantizer::score(const float* q, const uint8_t* s) const {
    float total = 0;
    for (int j = 0; j < dim; ++j) {
        int code = (s[j / 2] >> ((j % 2) * 4)) & 15;
        float x = lo[j] + span[j] * ((code + .5F) / 16), z = q[j] - x;
        total += z * z;
    }
    return total;
}
} // namespace qvann

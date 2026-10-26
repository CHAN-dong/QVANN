#include "optimize/refinement/bounds.h"

namespace qvann {
float lower_bound(const Quantizer& quantizer, const float* q, const uint8_t* s, const uint8_t* d) {
    const int dim = quantizer.dim;
    const auto& lo = quantizer.lo;
    const auto& span = quantizer.span;
    int bins = d ? 64 : 16;
    float total = 0;
    for (int j = 0; j < dim; ++j) {
        int code = (s[j / 2] >> ((j % 2) * 4)) & 15;
        if (d) {
            code = (code << 2) | ((d[j / 4] >> ((j % 4) * 2)) & 3);
        }
        float a = std::nextafter(lo[j] + span[j] * (float(code) / bins), -INFINITY);
        float b = std::nextafter(lo[j] + span[j] * (float(code + 1) / bins), INFINITY);
        float z = q[j] < a ? a - q[j] : (q[j] > b ? q[j] - b : 0);
        total += z * z;
    }
    return std::nextafter(total, 0.F);
}
} // namespace qvann

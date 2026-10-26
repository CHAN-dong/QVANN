#include "optimize/traversal/lookup.h"

namespace qvann {
Lookup::Lookup(const Lookup& other, bool) : Quantizer(other, true) {}

void Lookup::prepare(const float* q) const {
    if (last_query.size() == size_t(dim) &&
        std::memcmp(last_query.data(), q, size_t(dim) * sizeof(float)) == 0) {
        return;
    }
    score4.resize(size_t(dim) * 16);
    lower4.resize(size_t(dim) * 16);
    lower6.resize(size_t(dim) * 64);
    for (int j = 0; j < dim; ++j) {
        for (int code = 0; code < 16; ++code) {
            float x = lo[j] + span[j] * ((code + .5F) / 16), z = q[j] - x;
            score4[size_t(j) * 16 + code] = z * z;
        }
        for (int bits : {4, 6}) {
            int bins = 1 << bits;
            auto& table = bits == 4 ? lower4 : lower6;
            for (int code = 0; code < bins; ++code) {
                float a = std::nextafter(lo[j] + span[j] * (float(code) / bins), -INFINITY);
                float b = std::nextafter(lo[j] + span[j] * (float(code + 1) / bins), INFINITY);
                float z = q[j] < a ? a - q[j] : (q[j] > b ? q[j] - b : 0);
                table[size_t(j) * bins + code] = z * z;
            }
        }
    }
    last_query.assign(q, q + dim);
}

float Lookup::score(const float*, const uint8_t* s) const {
    float total = 0;
    for (int j = 0; j < dim; ++j) {
        int code = (s[j / 2] >> ((j % 2) * 4)) & 15;
        total += score4[size_t(j) * 16 + code];
    }
    return total;
}

float Lookup::lower(const float*, const uint8_t* s, const uint8_t* d) const {
    float total = 0;
    if (d) {
        for (int j = 0; j < dim; ++j) {
            int code =
                (((s[j / 2] >> ((j % 2) * 4)) & 15) << 2) | ((d[j / 4] >> ((j % 4) * 2)) & 3);
            total += lower6[size_t(j) * 64 + code];
        }
    } else {
        for (int j = 0; j < dim; ++j) {
            int code = (s[j / 2] >> ((j % 2) * 4)) & 15;
            total += lower4[size_t(j) * 16 + code];
        }
    }
    return std::nextafter(total, 0.F);
}
} // namespace qvann

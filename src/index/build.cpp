#include "index/graph.h"
#include "index/quantizer.h"

namespace qvann {
Quantizer::Quantizer(const Graph& h)
    : dim(h.dimension()), lo(dim, INFINITY), span(dim), high(h.size() * size_t(dim / 2)),
      low(h.size() * size_t(dim / 4)) {
    require(dim % 4 == 0, "dimension must be divisible by four");
    std::vector<float> hi(dim, -INFINITY);
    for (size_t id = 0; id < h.size(); ++id) {
        for (int j = 0; j < dim; ++j) {
            lo[j] = std::min(lo[j], h.vector(id)[j]);
            hi[j] = std::max(hi[j], h.vector(id)[j]);
        }
    }
    for (int j = 0; j < dim; ++j) {
        span[j] = hi[j] - lo[j];
    }
    for (size_t id = 0; id < h.size(); ++id) {
        for (int j = 0; j < dim; ++j) {
            double u = span[j] > 0 ? (h.vector(id)[j] - lo[j]) / span[j] : 0;
            int code = std::clamp(int(std::floor(u * 64)), 0, 63);
            high[id * (dim / 2) + j / 2] |= uint8_t((code >> 2) << ((j % 2) * 4));
            low[id * (dim / 4) + j / 4] |= uint8_t((code & 3) << ((j % 4) * 2));
        }
    }
}
} // namespace qvann

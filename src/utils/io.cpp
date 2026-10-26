#include "utils/io.h"

namespace qvann {
std::size_t Matrix::rows() const {
    return dimension == 0 ? 0 : values.size() / static_cast<std::size_t>(dimension);
}

const float* Matrix::row(std::size_t i) const {
    return values.data() + i * static_cast<std::size_t>(dimension);
}

Matrix read_fvecs(const std::string& path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + path);
    }
    Matrix matrix;
    while (limit == 0 || matrix.rows() < limit) {
        std::int32_t dimension = 0;
        if (!input.read(reinterpret_cast<char*>(&dimension), sizeof(dimension))) {
            break;
        }
        if (dimension <= 0 || (matrix.dimension != 0 && matrix.dimension != dimension)) {
            throw std::runtime_error("invalid fvecs dimension in " + path);
        }
        matrix.dimension = dimension;
        const std::size_t old_size = matrix.values.size();
        matrix.values.resize(old_size + static_cast<std::size_t>(dimension));
        if (!input.read(reinterpret_cast<char*>(matrix.values.data() + old_size),
                        sizeof(float) * static_cast<std::size_t>(dimension))) {
            throw std::runtime_error("truncated fvecs file " + path);
        }
        for (size_t i = old_size; i < matrix.values.size(); ++i) {
            if (!std::isfinite(matrix.values[i])) {
                throw std::runtime_error("nonfinite coordinate in " + path);
            }
        }
    }
    if (matrix.rows() == 0) {
        throw std::runtime_error("empty fvecs file " + path);
    }
    return matrix;
}

float squared_l2(const float* a, const float* b, int dimension) {
    float distance = 0.0F;
    for (int j = 0; j < dimension; ++j) {
        const float delta = a[j] - b[j];
        distance += delta * delta;
    }
    return distance;
}

void save(const std::string& path, const Bytes& b) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), b.size());
    if (!f) {
        throw std::runtime_error("cannot save VO");
    }
}
} // namespace qvann

#pragma once

#include "index/graph.h"
#include <omp.h>
#include <random>

namespace qvann::test {
inline Matrix vectors(size_t count = 512, int dimension = 16) {
    Matrix matrix;
    matrix.dimension = dimension;
    std::mt19937 rng(20261026);
    std::normal_distribution<float> sample;
    matrix.values.resize(count * dimension);
    for (auto& value : matrix.values) {
        value = sample(rng);
    }
    // Exercise a zero-span dimension as well as negative and positive coordinates.
    for (size_t i = 0; i < count; ++i) {
        matrix.values[i * dimension] = 0;
    }
    return matrix;
}

inline Graph graph(const Matrix& matrix) {
    omp_set_num_threads(1);
    return build_graph(matrix, 8, 64);
}
} // namespace qvann::test

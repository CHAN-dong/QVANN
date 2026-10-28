#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <openssl/sha.h>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "LearnedEarlyTer-20SIGMOD/IndexHNSW.h"
#include "LearnedEarlyTer-20SIGMOD/index_io.h"

namespace vann {
using Hash = std::array<std::uint8_t, SHA256_DIGEST_LENGTH>;

inline Hash sha256(const void* data, std::size_t bytes) {
    Hash out{};
    SHA256(static_cast<const unsigned char*>(data), bytes, out.data());
    return out;
}

struct Matrix {
    int dimension = 0;
    std::vector<float> values;

    std::size_t rows() const {
        return dimension == 0 ? 0 : values.size() / static_cast<std::size_t>(dimension);
    }

    const float* row(std::size_t i) const {
        return values.data() + i * static_cast<std::size_t>(dimension);
    }
};

inline Matrix read_fvecs(const std::string& path, std::size_t limit = 0) {
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
    }
    if (matrix.rows() == 0) {
        throw std::runtime_error("empty fvecs file " + path);
    }
    return matrix;
}

inline float squared_l2(const float* a, const float* b, int dimension) {
    float distance = 0.0F;
    for (int j = 0; j < dimension; ++j) {
        const float delta = a[j] - b[j];
        distance += delta * delta;
    }
    return distance;
}

struct HnswIndex {
    std::unique_ptr<faiss::IndexHNSWFlat> index;
    std::vector<int> local_to_global;

    int dimension() const {
        return index->d;
    }

    std::size_t size() const {
        return static_cast<std::size_t>(index->ntotal);
    }

    const float* vector(int local_id) const {
        const auto* flat = dynamic_cast<const faiss::IndexFlat*>(index->storage);
        if (flat == nullptr) {
            throw std::runtime_error("HNSW storage is not IndexFlat");
        }
        return flat->xb.data() + static_cast<std::size_t>(local_id) * index->d;
    }

    int global_id(int local_id) const {
        return local_to_global.empty() ? local_id
                                       : local_to_global.at(static_cast<std::size_t>(local_id));
    }

    int local_id_from_global(int global_id_value) const {
        if (local_to_global.empty()) {
            return global_id_value;
        }
        const auto found =
            std::lower_bound(local_to_global.begin(), local_to_global.end(), global_id_value);
        if (found == local_to_global.end() || *found != global_id_value) {
            return -1;
        }
        return static_cast<int>(found - local_to_global.begin());
    }

    std::vector<int> neighbors(int local_id, int level) const {
        std::size_t begin = 0;
        std::size_t end = 0;
        index->hnsw.neighbor_range(local_id, level, &begin, &end);
        std::vector<int> result;
        for (std::size_t p = begin; p < end; ++p) {
            const int neighbor = index->hnsw.neighbors[p];
            if (neighbor >= 0) {
                result.push_back(neighbor);
            }
        }
        return result;
    }
};

inline std::unique_ptr<faiss::IndexHNSWFlat>
load_hnsw(const std::string& cache_path, int expected_dimension, std::size_t expected_size) {
    if (cache_path.empty() || !std::filesystem::exists(cache_path)) {
        return nullptr;
    }
    std::unique_ptr<faiss::Index> loaded(faiss::read_index(cache_path.c_str()));
    auto* hnsw = dynamic_cast<faiss::IndexHNSWFlat*>(loaded.release());
    if (hnsw == nullptr || hnsw->d != expected_dimension ||
        static_cast<std::size_t>(hnsw->ntotal) != expected_size) {
        delete hnsw;
        throw std::runtime_error("cached HNSW shape mismatch: " + cache_path);
    }
    return std::unique_ptr<faiss::IndexHNSWFlat>(hnsw);
}

inline HnswIndex build_hnsw(const Matrix& vectors, int M, int ef_construction,
                            const std::string& cache_path = {},
                            std::vector<int> local_to_global = {}) {
    HnswIndex result;
    result.index = load_hnsw(cache_path, vectors.dimension, vectors.rows());
    if (!result.index) {
        result.index = std::make_unique<faiss::IndexHNSWFlat>(vectors.dimension, M);
        // The vendored research Faiss fork leaves this field uninitialized.
        result.index->hnsw.lselect = faiss::Random;
        result.index->hnsw.priors = nullptr;
        result.index->hnsw.efConstruction = ef_construction;
        result.index->add(static_cast<faiss::Index::idx_t>(vectors.rows()), vectors.values.data());
        if (!cache_path.empty()) {
            const std::filesystem::path cache(cache_path);
            if (!cache.parent_path().empty()) {
                std::filesystem::create_directories(cache.parent_path());
            }
            faiss::write_index(result.index.get(), cache_path.c_str());
        }
    }
    if (!local_to_global.empty() && local_to_global.size() != vectors.rows()) {
        throw std::runtime_error("local/global HNSW mapping size mismatch");
    }
    result.local_to_global = std::move(local_to_global);
    return result;
}

} // namespace vann

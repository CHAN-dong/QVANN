#include "index/graph.h"
#include "LearnedEarlyTer-20SIGMOD/index_io.h"

namespace qvann {
int Graph::dimension() const {
    return index->d;
}

std::size_t Graph::size() const {
    return static_cast<std::size_t>(index->ntotal);
}

const float* Graph::vector(int local_id) const {
    const auto* flat = dynamic_cast<const faiss::IndexFlat*>(index->storage);
    if (flat == nullptr) {
        throw std::runtime_error("HNSW storage is not IndexFlat");
    }
    return flat->xb.data() + static_cast<std::size_t>(local_id) * index->d;
}

std::vector<int> Graph::neighbors(int local_id, int level) const {
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

std::unique_ptr<faiss::IndexHNSWFlat> load_hnsw(const std::string& cache_path,
                                                int expected_dimension, std::size_t expected_size) {
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

Graph build_graph(const Matrix& vectors, int M, int ef_construction,
                  const std::string& cache_path) {
    Graph result;
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

    return result;
}

std::vector<Id> neighbors(const Graph& h, Id id, Id level) {
    auto ns = h.neighbors(id, level);
    std::sort(ns.begin(), ns.end());
    return {ns.begin(), ns.end()};
}
} // namespace qvann

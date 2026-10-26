#pragma once

#include "LearnedEarlyTer-20SIGMOD/IndexHNSW.h"
#include "common/types.h"

namespace qvann {

struct Graph {
    std::unique_ptr<faiss::IndexHNSWFlat> index;
    int dimension() const;
    std::size_t size() const;
    const float* vector(int id) const;
    std::vector<int> neighbors(int id, int level) const;
};

Graph build_graph(const Matrix& vectors, int M, int ef_construction,
                  const std::string& cache_path = {});
std::vector<Id> neighbors(const Graph& graph, Id id, Id level);

} // namespace qvann

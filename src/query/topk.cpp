#include "query/topk.h"
#include "index/graph.h"

namespace qvann {

TopK topk(const Graph& graph, const float* query, const std::vector<Id>& candidates, int k) {
    std::vector<std::pair<float, Id>> exact;
    for (Id id : candidates) {
        exact.emplace_back(squared_l2(query, graph.vector(id), graph.dimension()), id);
    }
    require(exact.size() >= size_t(k), "candidate set smaller than k; raise E");
    std::partial_sort(exact.begin(), exact.begin() + k, exact.end());
    TopK result;
    for (int i = 0; i < k; ++i) {
        result.answer.push_back(exact[i].second);
    }
    result.threshold = exact[k - 1].first;
    return result;
}

std::vector<std::vector<uint32_t>> ground_truth(const Matrix& b, const Matrix& q, int k) {
    std::vector<std::vector<uint32_t>> out;
    for (size_t qi = 0; qi < q.rows(); ++qi) {
        std::vector<std::pair<float, uint32_t>> a;
        for (uint32_t id = 0; id < b.rows(); ++id) {
            a.emplace_back(squared_l2(b.row(id), q.row(qi), b.dimension), id);
        }
        std::partial_sort(a.begin(), a.begin() + k, a.end());
        std::vector<uint32_t> ids;
        for (int j = 0; j < k; ++j) {
            ids.push_back(a[j].second);
        }
        out.push_back(std::move(ids));
    }
    return out;
}
} // namespace qvann

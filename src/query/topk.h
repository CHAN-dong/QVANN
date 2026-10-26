#pragma once

#include "common/types.h"

namespace qvann {

struct Graph;

struct TopK {
    std::vector<Id> answer;
    float threshold;
};

TopK topk(const Graph& graph, const float* query, const std::vector<Id>& candidates, int k);
std::vector<std::vector<Id>> ground_truth(const Matrix& base, const Matrix& queries, int k);

} // namespace qvann

#pragma once

#include "common/types.h"

namespace qvann {

struct Graph;
std::vector<int> bfs_order(const Graph& graph, int& components);

} // namespace qvann

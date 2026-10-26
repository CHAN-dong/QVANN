#include "optimize/expansion/ordering.h"
#include "index/graph.h"

namespace qvann {
std::vector<int> bfs_order(const Graph& h, int& components) {
    const std::size_t n = h.size();
    std::vector<int> order;
    order.reserve(n);
    std::vector<std::uint8_t> seen(n, 0);
    std::deque<int> queue;
    components = 0;

    auto visit_component = [&](int start) {
        ++components;
        seen[static_cast<std::size_t>(start)] = 1;
        queue.push_back(start);
        while (!queue.empty()) {
            const int v = queue.front();
            queue.pop_front();
            order.push_back(v);
            auto neighbors = h.neighbors(v, 0);
            std::sort(neighbors.begin(), neighbors.end());
            neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
            for (int u : neighbors) {
                if (!seen[static_cast<std::size_t>(u)]) {
                    seen[static_cast<std::size_t>(u)] = 1;
                    queue.push_back(u);
                }
            }
        }
    };

    visit_component(h.index->hnsw.entry_point);
    for (std::size_t id = 0; id < n; ++id) {
        if (!seen[id]) {
            visit_component(static_cast<int>(id));
        }
    }
    return order;
}
} // namespace qvann

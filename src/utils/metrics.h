#pragma once

#include "utils/config.h"

namespace qvann {
struct Row {
    int query, repeat, k, budget;
    double search_ms, vo_ms, verify_ms, auth_ms, replay_ms;
    size_t vo_bytes, record_bytes, local_proof_bytes, route_bytes, visited, expanded;
    double recall, recall1, kth_ratio;
    bool valid;
};

void output(const Config& config, const std::vector<Row>& rows, double index_ms, double ads_ms,
            double gt_ms, const Hash& snapshot, const std::string& method, size_t ads_bytes);
} // namespace qvann

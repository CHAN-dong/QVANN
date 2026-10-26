#pragma once

#include "common/types.h"

namespace qvann {
struct Config {
    std::string dataset = "../dataset/sift", output = "results", cache = "cache", metric = "l2",
                proof = "multiproof";
    int base = 100000, queries = 100, query_offset = 0, build_threads = 1, M = 8,
        ef_construction = 100, repeats = 1;
    std::vector<int> result_sizes{10}, budgets{50};
};

Config args(int argc, char** argv);
Matrix read_queries(const Config& config);
} // namespace qvann

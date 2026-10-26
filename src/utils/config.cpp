#include "utils/config.h"
#include "utils/io.h"

namespace qvann {
std::vector<int> ints(const std::string& value) {
    std::vector<int> v;
    std::stringstream s(value);
    std::string t;
    while (std::getline(s, t, ',')) {
        v.push_back(std::stoi(t));
    }
    if (v.empty()) {
        throw std::runtime_error("empty parameter list");
    }
    return v;
}

Config args(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (++i == argc) {
            throw std::runtime_error("missing option value");
        }
        std::string v = argv[i];
        if (a == "--dataset") {
            c.dataset = v;
        } else if (a == "--output") {
            c.output = v;
        } else if (a == "--cache") {
            c.cache = v;
        } else if (a == "--base") {
            c.base = std::stoi(v);
        } else if (a == "--queries") {
            c.queries = std::stoi(v);
        } else if (a == "--query-offset") {
            c.query_offset = std::stoi(v);
        } else if (a == "--build-threads") {
            c.build_threads = std::stoi(v);
        } else if (a == "--M") {
            c.M = std::stoi(v);
        } else if (a == "--ef-construction") {
            c.ef_construction = std::stoi(v);
        } else if (a == "--repeats") {
            c.repeats = std::stoi(v);
        } else if (a == "--k") {
            c.result_sizes = ints(v);
        } else if (a == "--ef-search" || a == "--budget") {
            c.budgets = ints(v);
        } else if (a == "--metric") {
            c.metric = v;
        } else if (a == "--proof") {
            c.proof = v;
        } else {
            throw std::runtime_error("unknown option " + a);
        }
    }
    if (c.base < 1 || c.queries < 1 || c.query_offset < 0 || c.build_threads < 1 ||
        c.build_threads > 24 || c.M < 2 || c.ef_construction < 1 || c.repeats < 1) {
        throw std::runtime_error("invalid configuration");
    }
    for (auto v : c.result_sizes) {
        if (v < 1 || v > c.base) {
            throw std::runtime_error("invalid k");
        }
    }
    for (auto v : c.budgets) {
        if (v < 1) {
            throw std::runtime_error("invalid ef");
        }
    }
    if (c.metric != "l2" || c.proof != "multiproof") {
        throw std::runtime_error("QVANN supports squared L2 and multiproofs");
    }
    std::filesystem::create_directories(c.output);
    std::filesystem::create_directories(c.cache);
    return c;
}

Matrix read_queries(const Config& c) {
    if (c.query_offset < 0) {
        throw std::runtime_error("negative query offset");
    }
    const size_t limit = size_t(c.query_offset) + size_t(c.queries);
    auto q = read_fvecs(c.dataset + "/sift_query.fvecs", limit);
    if (q.rows() != limit) {
        throw std::runtime_error("not enough queries for requested offset/count");
    }
    q.values.erase(q.values.begin(), q.values.begin() + size_t(c.query_offset) * q.dimension);
    return q;
}
} // namespace qvann

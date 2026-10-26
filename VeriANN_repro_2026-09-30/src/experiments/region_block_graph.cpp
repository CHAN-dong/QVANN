#define IVF_GRANULARITY_NO_MAIN
#include "ivf_granularity.cpp"
#undef IVF_GRANULARITY_NO_MAIN

#include <chrono>
#include <sstream>
#include <unordered_set>

// Verification-native ANN prototype: graph vertices are balanced vector blocks,
// routing uses authenticated low-precision summaries, and a block is one subtree.

struct RBCfg {
    std::string data = "../dataset/sift";
    std::string output = "region_block_graph_sift100k_b64.csv";
    int base = 100000;
    int queries = 100;
    int k = 10;
    int block_size = 64;
    int cluster_iters = 5;
    int degree = 16;
    int entries = 8;
    std::vector<int> expansions{16, 32, 64, 96, 128, 192};
    std::vector<int> probes{8, 12, 16, 24, 32, 48};
};

static std::vector<int> rb_ints(const std::string& text) {
    std::vector<int> values;
    std::stringstream input(text);
    std::string part;
    while (std::getline(input, part, ',')) values.push_back(std::stoi(part));
    return values;
}

static RBCfg rb_args(int argc, char** argv) {
    RBCfg config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() {
            if (++i == argc) throw std::runtime_error("missing value after " + arg);
            return std::string(argv[i]);
        };
        if (arg == "--dataset") config.data = value();
        else if (arg == "--output") config.output = value();
        else if (arg == "--base") config.base = std::stoi(value());
        else if (arg == "--queries") config.queries = std::stoi(value());
        else if (arg == "--k") config.k = std::stoi(value());
        else if (arg == "--block-size") config.block_size = std::stoi(value());
        else if (arg == "--cluster-iters") config.cluster_iters = std::stoi(value());
        else if (arg == "--degree") config.degree = std::stoi(value());
        else if (arg == "--entries") config.entries = std::stoi(value());
        else if (arg == "--expansions") config.expansions = rb_ints(value());
        else if (arg == "--probes") config.probes = rb_ints(value());
        else if (arg == "--help") {
            std::cout << "Usage: region_block_graph [--block-size 64] [--degree 16] "
                         "[--entries 8] [--expansions 16,32,...] [--probes 8,16,...]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown option " + arg);
        }
    }
    if (config.base < config.k || config.queries < 1 || config.block_size < 2 ||
        config.degree < 2 || config.entries < 1 || config.expansions.empty() ||
        config.probes.empty()) {
        throw std::runtime_error("invalid configuration");
    }
    return config;
}

static void rb_append(std::vector<uint8_t>& bytes, const void* data, size_t size) {
    const auto* first = static_cast<const uint8_t*>(data);
    bytes.insert(bytes.end(), first, first + size);
}
template <class T>
static void rb_append(std::vector<uint8_t>& bytes, const T& value) {
    rb_append(bytes, &value, sizeof(value));
}
static void rb_append(std::vector<uint8_t>& bytes, const Hash& hash) {
    bytes.insert(bytes.end(), hash.begin(), hash.end());
}

class RBNestedSQ {
public:
    explicit RBNestedSQ(const Matrix& data)
        : dimension_(data.d),
          lo_(data.d, std::numeric_limits<float>::infinity()),
          span_(data.d),
          code_(data.rows() * static_cast<size_t>(data.d)) {
        std::vector<float> hi(data.d, -std::numeric_limits<float>::infinity());
        for (size_t id = 0; id < data.rows(); ++id) {
            for (int j = 0; j < data.d; ++j) {
                lo_[j] = std::min(lo_[j], data[id][j]);
                hi[j] = std::max(hi[j], data[id][j]);
            }
        }
        for (int j = 0; j < data.d; ++j) span_[j] = hi[j] - lo_[j];
        for (size_t id = 0; id < data.rows(); ++id) {
            for (int j = 0; j < data.d; ++j) {
                int code = 0;
                if (span_[j] > 0) {
                    code = std::clamp(static_cast<int>(
                        std::floor((data[id][j] - lo_[j]) / span_[j] * 64.0)), 0, 63);
                }
                code_[id * static_cast<size_t>(dimension_) + j] =
                    static_cast<uint8_t>(code);
            }
        }
    }

    float score2(const float* query, int id, int bits) const {
        const int bins = 1 << bits;
        const int shift = 6 - bits;
        float sum = 0;
        for (int j = 0; j < dimension_; ++j) {
            const int value =
                code_[static_cast<size_t>(id) * dimension_ + j] >> shift;
            const float decoded =
                lo_[j] + span_[j] * (value + 0.5F) / bins;
            const float delta = query[j] - decoded;
            sum += delta * delta;
        }
        return sum;
    }

    float lower2(const float* query, int id, int bits) const {
        const int bins = 1 << bits;
        const int shift = 6 - bits;
        float sum = 0;
        for (int j = 0; j < dimension_; ++j) {
            const int code =
                code_[static_cast<size_t>(id) * dimension_ + j] >> shift;
            const float low = std::nextafter(
                lo_[j] + span_[j] * code / bins,
                -std::numeric_limits<float>::infinity());
            const float high = std::nextafter(
                lo_[j] + span_[j] * (code + 1) / bins,
                std::numeric_limits<float>::infinity());
            const float delta = query[j] < low ? low - query[j]
                               : query[j] > high ? query[j] - high : 0;
            sum += delta * delta;
        }
        return std::nextafter(sum, 0.0F);
    }

    std::vector<uint8_t> pack4(int id) const {
        std::vector<uint8_t> packed((dimension_ + 1) / 2);
        for (int j = 0; j < dimension_; ++j) {
            packed[j / 2] |= static_cast<uint8_t>(
                (code_[static_cast<size_t>(id) * dimension_ + j] >> 2)
                << ((j & 1) * 4));
        }
        return packed;
    }

    std::vector<uint8_t> delta2(int id) const {
        std::vector<uint8_t> packed((dimension_ + 3) / 4);
        for (int j = 0; j < dimension_; ++j) {
            packed[j / 4] |= static_cast<uint8_t>(
                (code_[static_cast<size_t>(id) * dimension_ + j] & 3)
                << ((j & 3) * 2));
        }
        return packed;
    }

private:
    int dimension_;
    std::vector<float> lo_;
    std::vector<float> span_;
    std::vector<uint8_t> code_;
};

struct RBSummary {
    int dimension;
    int bits;
    std::vector<float> lo;
    std::vector<float> step;
    std::vector<float> error;
    std::vector<float> radius;
    std::vector<uint8_t> code;

    RBSummary(const Matrix& data,
              const std::vector<std::vector<int>>& regions,
              const std::vector<std::vector<float>>& centers,
              int quantization_bits = 4)
        : dimension(data.d),
          bits(quantization_bits),
          lo(data.d, std::numeric_limits<float>::infinity()),
          step(data.d),
          error(centers.size()),
          radius(centers.size()),
          code(centers.size() * static_cast<size_t>(data.d)) {
        std::vector<float> hi(data.d, -std::numeric_limits<float>::infinity());
        for (const auto& center : centers) {
            for (int j = 0; j < data.d; ++j) {
                lo[j] = std::min(lo[j], center[j]);
                hi[j] = std::max(hi[j], center[j]);
            }
        }
        if (bits < 1 || bits > 8) {
            throw std::runtime_error("summary quantization bits must be in [1,8]");
        }
        const int maximum_code = (1 << bits) - 1;
        for (int j = 0; j < data.d; ++j) {
            step[j] = (hi[j] - lo[j]) / maximum_code;
        }
        for (size_t region = 0; region < centers.size(); ++region) {
            double error2 = 0;
            double radius2 = 0;
            for (int j = 0; j < data.d; ++j) {
                const int value = step[j] > 0 ? std::clamp(static_cast<int>(
                    std::lround((centers[region][j] - lo[j]) / step[j])),
                    0, maximum_code) : 0;
                code[region * static_cast<size_t>(data.d) + j] =
                    static_cast<uint8_t>(value);
                const double delta = centers[region][j] - (lo[j] + value * step[j]);
                error2 += delta * delta;
            }
            for (int id : regions[region]) {
                radius2 = std::max(radius2, static_cast<double>(
                    l2(centers[region].data(), data[id], data.d)));
            }
            error[region] = outward(std::sqrt(error2));
            radius[region] = outward(std::sqrt(radius2));
        }
    }

    static float outward(double value) {
        float result = static_cast<float>(value);
        if (static_cast<double>(result) < value) {
            result = std::nextafter(result, std::numeric_limits<float>::infinity());
        }
        return result;
    }

    float score(const float* query, int id, bool lower_bound) const {
        double distance2 = 0;
        for (int j = 0; j < dimension; ++j) {
            const double decoded =
                lo[j] + code[static_cast<size_t>(id) * dimension + j] * step[j];
            const double delta = query[j] - decoded;
            distance2 += delta * delta;
        }
        if (!lower_bound) return static_cast<float>(distance2);
        const double bound =
            std::max(0.0, std::sqrt(distance2) - error[id] - radius[id]);
        return std::nextafter(static_cast<float>(bound * bound), 0.0F);
    }

    std::vector<uint8_t> packed(int id) const {
        std::vector<uint8_t> result((dimension * bits + 7) / 8);
        for (int j = 0; j < dimension; ++j) {
            const int offset = j * bits;
            const uint16_t value =
                code[static_cast<size_t>(id) * dimension + j];
            result[offset / 8] |= static_cast<uint8_t>(value << (offset & 7));
            if ((offset & 7) + bits > 8) {
                result[offset / 8 + 1] |=
                    static_cast<uint8_t>(value >> (8 - (offset & 7)));
            }
        }
        return result;
    }

    float decoded(int id, int coordinate) const {
        return lo[coordinate] +
            code[static_cast<size_t>(id) * dimension + coordinate] *
            step[coordinate];
    }
};

static std::vector<std::vector<int>> rb_graph(
    const std::vector<std::vector<float>>& centers, int degree) {
    const int count = static_cast<int>(centers.size());
    const int dimension = static_cast<int>(centers.front().size());
    std::vector<std::vector<int>> graph(count);
    for (int i = 0; i < count; ++i) {
        std::vector<std::pair<float, int>> nearest;
        nearest.reserve(count - 1);
        for (int j = 0; j < count; ++j) {
            if (i != j) {
                nearest.push_back(
                    {l2(centers[i].data(), centers[j].data(), dimension), j});
            }
        }
        const int take = std::min<int>(degree, nearest.size());
        std::partial_sort(nearest.begin(), nearest.begin() + take, nearest.end(),
            [](const auto& a, const auto& b) {
                return a.first < b.first ||
                       (a.first == b.first && a.second < b.second);
            });
        for (int j = 0; j < take; ++j) graph[i].push_back(nearest[j].second);
    }
    return graph;
}

static std::vector<int> rb_entries(
    const std::vector<std::vector<float>>& centers, int count) {
    count = std::min<int>(count, centers.size());
    std::vector<int> entries{0};
    while (static_cast<int>(entries.size()) < count) {
        int selected = -1;
        float farthest = -1;
        for (int i = 0; i < static_cast<int>(centers.size()); ++i) {
            if (std::find(entries.begin(), entries.end(), i) != entries.end()) continue;
            float nearest = std::numeric_limits<float>::infinity();
            for (int entry : entries) {
                nearest = std::min(nearest, l2(centers[i].data(),
                    centers[entry].data(), centers[i].size()));
            }
            if (nearest > farthest) {
                farthest = nearest;
                selected = i;
            }
        }
        entries.push_back(selected);
    }
    return entries;
}

static Hash rb_object_leaf(const Matrix& data, const RBNestedSQ& sq, int id) {
    std::vector<uint8_t> exact;
    const std::string exact_domain = "rb/exact/v1";
    exact.insert(exact.end(), exact_domain.begin(), exact_domain.end());
    rb_append(exact, id);
    const Hash vector_hash = sha(data[id], data.d * sizeof(float));
    rb_append(exact, vector_hash);
    const Hash exact_hash = sha(exact.data(), exact.size());

    std::vector<uint8_t> refinement;
    const std::string refinement_domain = "rb/sq6/v1";
    refinement.insert(refinement.end(), refinement_domain.begin(), refinement_domain.end());
    const auto delta = sq.delta2(id);
    refinement.insert(refinement.end(), delta.begin(), delta.end());
    rb_append(refinement, exact_hash);
    const Hash refinement_hash = sha(refinement.data(), refinement.size());

    std::vector<uint8_t> base;
    const std::string base_domain = "rb/sq4/v1";
    base.insert(base.end(), base_domain.begin(), base_domain.end());
    rb_append(base, id);
    const auto code = sq.pack4(id);
    base.insert(base.end(), code.begin(), code.end());
    rb_append(base, refinement_hash);
    return sha(base.data(), base.size());
}

static Hash rb_region_leaf(int id, const RBSummary& summary, int count,
                           const Hash& local_root,
                           const std::vector<int>& adjacency) {
    std::vector<uint8_t> bytes;
    const std::string domain = "rb/region/v1";
    bytes.insert(bytes.end(), domain.begin(), domain.end());
    rb_append(bytes, id);
    rb_append(bytes, count);
    rb_append(bytes, summary.radius[id]);
    rb_append(bytes, summary.error[id]);
    const auto code = summary.packed(id);
    bytes.insert(bytes.end(), code.begin(), code.end());
    rb_append(bytes, local_root);
    const Hash adjacency_hash =
        sha(adjacency.data(), adjacency.size() * sizeof(int));
    rb_append(bytes, adjacency_hash);
    return sha(bytes.data(), bytes.size());
}

struct RBTrace {
    std::vector<int> disclosed;
    std::vector<int> expanded;
    std::vector<int> selected;
};
struct RBItem {
    float score;
    int id;
};
struct RBGreater {
    bool operator()(const RBItem& a, const RBItem& b) const {
        return a.score > b.score || (a.score == b.score && a.id > b.id);
    }
};

static RBTrace rb_route(const RBSummary& summary,
                        const std::vector<std::vector<int>>& graph,
                        const std::vector<int>& entries, const float* query,
                        int budget, int probes, bool lower_bound) {
    RBTrace trace;
    std::vector<uint8_t> seen(graph.size()), done(graph.size());
    std::priority_queue<RBItem, std::vector<RBItem>, RBGreater> frontier;
    std::vector<RBItem> candidates;
    auto discover = [&](int id) {
        if (seen[id]) return;
        seen[id] = 1;
        const RBItem item{summary.score(query, id, lower_bound), id};
        frontier.push(item);
        candidates.push_back(item);
        trace.disclosed.push_back(id);
    };
    for (int entry : entries) discover(entry);
    while (static_cast<int>(trace.expanded.size()) < budget && !frontier.empty()) {
        const RBItem current = frontier.top();
        frontier.pop();
        if (done[current.id]) continue;
        done[current.id] = 1;
        trace.expanded.push_back(current.id);
        for (int neighbor : graph[current.id]) discover(neighbor);
    }
    if (static_cast<int>(candidates.size()) < probes) {
        throw std::runtime_error("routing budget disclosed fewer regions than nprobe");
    }
    std::partial_sort(candidates.begin(), candidates.begin() + probes, candidates.end(),
        [](const auto& a, const auto& b) {
            return a.score < b.score ||
                   (a.score == b.score && a.id < b.id);
        });
    for (int i = 0; i < probes; ++i) trace.selected.push_back(candidates[i].id);
    std::sort(trace.disclosed.begin(), trace.disclosed.end());
    std::sort(trace.expanded.begin(), trace.expanded.end());
    std::sort(trace.selected.begin(), trace.selected.end());
    return trace;
}

static RBTrace rb_exhaustive(const RBSummary& summary, size_t count,
                             const float* query, int probes, bool lower_bound) {
    std::vector<RBItem> order(count);
    for (size_t i = 0; i < count; ++i) {
        order[i] = {summary.score(query, i, lower_bound), static_cast<int>(i)};
    }
    std::partial_sort(order.begin(), order.begin() + probes, order.end(),
        [](const auto& a, const auto& b) {
            return a.score < b.score ||
                   (a.score == b.score && a.id < b.id);
        });
    RBTrace trace;
    trace.disclosed.resize(count);
    std::iota(trace.disclosed.begin(), trace.disclosed.end(), 0);
    for (int i = 0; i < probes; ++i) trace.selected.push_back(order[i].id);
    std::sort(trace.selected.begin(), trace.selected.end());
    return trace;
}

struct RBMeasure {
    double hits = 0;
    double disclosed = 0;
    double expanded = 0;
    double selected = 0;
    double visited = 0;
    double refined = 0;
    double exact = 0;
    double metadata = 0;
    double adjacency = 0;
    double hidden_adjacency = 0;
    double hidden_blocks = 0;
    double proof = 0;
    double object_base = 0;
    double object_refinement = 0;
    double exact_bytes = 0;
    // Rebilling of the same query trace under five independent aligned trees.
    double separated_summary_proof = 0;
    double separated_adjacency_proof = 0;
    double separated_sq4_proof = 0;
    double separated_sq6_proof = 0;
    double separated_raw_proof = 0;
    double separated_sq4_payload = 0;
    double separated_sq6_payload = 0;
    double separated_raw_payload = 0;
    double separated_sq4_objects = 0;
    double separated_sq6_objects = 0;
    double separated_raw_objects = 0;
    double milliseconds = 0;
    uint64_t violations = 0;
};

static RBMeasure rb_evaluate(
    bool graph_mode, bool lower_bound, int budget, int probes,
    const Matrix& data, const Matrix& queries,
    const std::vector<std::vector<int>>& regions,
    const std::vector<std::vector<int>>& graph,
    const std::vector<int>& entries, const RBSummary& summary,
    const RBNestedSQ& sq, const std::vector<std::vector<int>>& ground_truth,
    const std::vector<RBTrace>* supplied_traces = nullptr,
    bool portal_residual = false) {
    RBMeasure total;
    std::vector<uint8_t> selected(regions.size()), expanded(regions.size());
    // One canonical position per object: blocks are contiguous and retain
    // their existing in-block order. All three object trees share this order.
    std::vector<int> object_position(data.rows(), -1);
    int next_position = 0;
    for (const auto& region : regions) {
        for (int id : region) {
            if (id < 0 || static_cast<size_t>(id) >= data.rows() ||
                object_position[id] >= 0) {
                throw std::runtime_error("invalid block-aware object ordering");
            }
            object_position[id] = next_position++;
        }
    }
    if (next_position != static_cast<int>(data.rows())) {
        throw std::runtime_error("incomplete block-aware object ordering");
    }
    auto proof_bytes = [](size_t count, const std::vector<int>& opened) {
        return opened.empty() ? 0.0 : proof_count(count, opened) * 32.0;
    };
    if (supplied_traces != nullptr && supplied_traces->size() != queries.rows())
        throw std::runtime_error("supplied block trace count mismatch");
    for (size_t qi = 0; qi < queries.rows(); ++qi) {
        const auto begin = std::chrono::steady_clock::now();
        const RBTrace trace = supplied_traces != nullptr ? supplied_traces->at(qi) : graph_mode
            ? rb_route(summary, graph, entries, queries[qi], budget, probes, lower_bound)
            : rb_exhaustive(summary, regions.size(), queries[qi], probes, lower_bound);
        std::fill(selected.begin(), selected.end(), 0);
        std::fill(expanded.begin(), expanded.end(), 0);
        for (int region : trace.selected) selected[region] = 1;
        for (int region : trace.expanded) expanded[region] = 1;

        std::priority_queue<std::pair<float, int>> top;
        for (int region : trace.selected) {
            for (int id : regions[region]) {
                const float distance = l2(queries[qi], data[id], data.d);
                if (static_cast<int>(top.size()) < static_cast<int>(ground_truth[qi].size())) {
                    top.push({distance, id});
                } else if (distance < top.top().first ||
                           (distance == top.top().first && id < top.top().second)) {
                    top.pop();
                    top.push({distance, id});
                }
            }
        }
        if (top.size() != ground_truth[qi].size()) {
            throw std::runtime_error("too few block candidates");
        }
        const float threshold = top.top().first;
        std::unordered_set<int> answer;
        while (!top.empty()) {
            answer.insert(top.top().second);
            top.pop();
        }
        for (int id : ground_truth[qi]) total.hits += answer.count(id);

        double refined = 0;
        double exact = 0;
        double visited = 0;
        std::vector<int> sq4_only;
        std::vector<int> sq6_only;
        std::vector<int> raw_only;
        for (int region : trace.selected) {
            visited += regions[region].size();
            std::vector<float> residual_query;
            const float* bound_query = queries[qi];
            if (portal_residual) {
                residual_query.resize(data.d);
                for (int j = 0; j < data.d; ++j) {
                    residual_query[j] =
                        queries[qi][j] - summary.decoded(region, j);
                }
                bound_query = residual_query.data();
            }
            for (int id : regions[region]) {
                const float truth = l2(queries[qi], data[id], data.d);
                const float lower4 = sq.lower2(bound_query, id, 4);
                total.violations += lower4 > truth;
                if (lower4 <= threshold) {
                    ++refined;
                    const float lower6 = sq.lower2(bound_query, id, 6);
                    total.violations += lower6 > truth;
                    if (lower6 <= threshold) {
                        ++exact;
                        raw_only.push_back(object_position[id]);
                    } else {
                        sq6_only.push_back(object_position[id]);
                    }
                } else {
                    sq4_only.push_back(object_position[id]);
                }
            }
        }

        total.disclosed += trace.disclosed.size();
        total.expanded += trace.expanded.size();
        total.selected += trace.selected.size();
        total.visited += visited;
        total.refined += refined;
        total.exact += exact;
        total.metadata += trace.disclosed.size() *
            (16.0 + (summary.dimension * summary.bits + 7) / 8.0);
        for (int region : trace.disclosed) {
            if (expanded[region]) {
                total.adjacency += sizeof(uint32_t) +
                    graph[region].size() * sizeof(uint32_t);
            } else {
                total.hidden_adjacency += 32;
            }
            if (!selected[region]) total.hidden_blocks += 32;
        }
        const double summary_proof = proof_bytes(regions.size(), trace.disclosed);
        total.proof += summary_proof;
        total.object_base += visited * 100.0;
        total.object_refinement += refined * 64.0;
        total.exact_bytes += exact * data.d * sizeof(float);

        total.separated_summary_proof += summary_proof;
        total.separated_adjacency_proof +=
            proof_bytes(regions.size(), trace.expanded);
        total.separated_sq4_proof += proof_bytes(data.rows(), sq4_only);
        total.separated_sq6_proof += proof_bytes(data.rows(), sq6_only);
        total.separated_raw_proof += proof_bytes(data.rows(), raw_only);
        // Each independent leaf contains the ID and one complete representation.
        // Full SQ6 implies SQ4; raw implies both SQ4 and SQ6.
        const double sq4_record = sizeof(uint32_t) + (data.d * 4 + 7) / 8;
        const double sq6_record = sizeof(uint32_t) + (data.d * 6 + 7) / 8;
        const double raw_record = sizeof(uint32_t) + data.d * sizeof(float);
        total.separated_sq4_payload += sq4_only.size() * sq4_record;
        total.separated_sq6_payload += sq6_only.size() * sq6_record;
        total.separated_raw_payload += raw_only.size() * raw_record;
        total.separated_sq4_objects += sq4_only.size();
        total.separated_sq6_objects += sq6_only.size();
        total.separated_raw_objects += raw_only.size();
        total.milliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin).count();
    }
    return total;
}

#ifndef REGION_BLOCK_GRAPH_NO_MAIN
int main(int argc, char** argv) try {
    const RBCfg config = rb_args(argc, argv);
    std::cerr << "Loading data...\n";
    const Matrix data = fvecs(config.data + "/sift_base.fvecs", config.base);
    const Matrix queries = fvecs(config.data + "/sift_query.fvecs", config.queries);
    std::cerr << "Computing exact subset ground truth...\n";
    const auto ground_truth = exact_gt(data, queries, config.k);

    const int region_count =
        (config.base + config.block_size - 1) / config.block_size;
    std::cerr << "Balanced region clustering, regions=" << region_count << "...\n";
    const auto regions =
        hkmeans_balanced(data, region_count, config.cluster_iters);
    const auto center_vectors = centers(data, regions);
    size_t min_size = data.rows();
    size_t max_size = 0;
    for (const auto& region : regions) {
        min_size = std::min(min_size, region.size());
        max_size = std::max(max_size, region.size());
    }

    std::cerr << "Region sizes " << min_size << ".." << max_size
              << ", building exact kNN graph...\n";
    const auto graph = rb_graph(center_vectors, config.degree);
    const auto entries = rb_entries(center_vectors, config.entries);
    const RBNestedSQ object_sq(data);
    const RBSummary summary(data, regions, center_vectors);

    std::cerr << "Building real local/global SHA-256 commitments...\n";
    std::vector<Hash> local_roots(regions.size());
    for (size_t region = 0; region < regions.size(); ++region) {
        std::vector<Hash> leaves;
        leaves.reserve(regions[region].size());
        for (int id : regions[region]) {
            leaves.push_back(rb_object_leaf(data, object_sq, id));
        }
        local_roots[region] = merkle(std::move(leaves));
    }
    std::vector<Hash> region_leaves(regions.size());
    for (size_t region = 0; region < regions.size(); ++region) {
        region_leaves[region] = rb_region_leaf(
            region, summary, regions[region].size(), local_roots[region], graph[region]);
    }
    const Hash root = merkle(region_leaves);
    auto altered = region_leaves;
    altered.front().front() ^= 1;
    if (merkle(altered) == root) {
        throw std::runtime_error("Merkle tamper self-test failed");
    }

    std::ofstream output(config.output);
    if (!output) throw std::runtime_error("cannot write " + config.output);
    output << std::setprecision(12)
           << "mode,score,base,queries,k,target_block_size,regions,min_block,max_block,"
              "degree,entries,expansion_budget,nprobe,recall,avg_disclosed_regions,"
              "avg_expanded_regions,avg_selected_regions,avg_visited_vectors,"
              "avg_refined_vectors,avg_exact_vectors,region_metadata_bytes,"
              "adjacency_bytes,hidden_adjacency_hash_bytes,hidden_block_root_bytes,"
              "global_merkle_bytes,object_base_bytes,object_refinement_bytes,"
              "exact_vector_bytes,routing_vo_bytes,block_vo_bytes,total_vo_bytes,"
              "reduction_vs_merkle_hnsw,query_ms,bound_violations\n";

    auto run = [&](bool graph_mode, bool lower_bound, int budget, int probes) {
        const std::string mode = graph_mode ? "graph" : "exhaustive";
        const std::string score = lower_bound ? "lower-bound" : "sq4-center";
        std::cerr << "Running " << mode << '/' << score << " expand=" << budget
                  << " probe=" << probes << "...\n";
        const RBMeasure value = rb_evaluate(
            graph_mode, lower_bound, budget, probes, data, queries, regions,
            graph, entries, summary, object_sq, ground_truth);
        const double nq = queries.rows();
        const double routing = 20 + config.entries * 4 +
            (value.metadata + value.adjacency + value.hidden_adjacency +
             value.hidden_blocks + value.proof) / nq;
        const double block =
            (value.object_base + value.object_refinement + value.exact_bytes) / nq;
        const double total = routing + block;
        const double recall = value.hits / (nq * config.k);
        constexpr double hnsw_baseline = 378653.95;
        output << mode << ',' << score << ',' << config.base << ','
               << config.queries << ',' << config.k << ',' << config.block_size << ','
               << regions.size() << ',' << min_size << ',' << max_size << ','
               << config.degree << ',' << config.entries << ',' << budget << ','
               << probes << ',' << recall << ',' << value.disclosed / nq << ','
               << value.expanded / nq << ',' << value.selected / nq << ','
               << value.visited / nq << ',' << value.refined / nq << ','
               << value.exact / nq << ',' << value.metadata / nq << ','
               << value.adjacency / nq << ',' << value.hidden_adjacency / nq << ','
               << value.hidden_blocks / nq << ',' << value.proof / nq << ','
               << value.object_base / nq << ',' << value.object_refinement / nq << ','
               << value.exact_bytes / nq << ',' << routing << ',' << block << ','
               << total << ',' << 1 - total / hnsw_baseline << ','
               << value.milliseconds / nq << ',' << value.violations << '\n';
        std::cout << mode << '/' << score << " e=" << budget << " p=" << probes
                  << " recall=" << std::fixed << std::setprecision(4) << recall
                  << " regions=" << std::setprecision(1) << value.disclosed / nq
                  << " vectors=" << value.visited / nq
                  << " exact=" << value.exact / nq
                  << " route=" << routing / 1024 << " KiB"
                  << " block=" << block / 1024 << " KiB"
                  << " total=" << total / 1024 << " KiB"
                  << " vs-HNSW=" << std::setprecision(2)
                  << (1 - total / hnsw_baseline) * 100 << "%"
                  << " violations=" << value.violations << '\n';
    };

    for (bool lower_bound : {false, true}) {
        for (int probes : config.probes) run(false, lower_bound, 0, probes);
    }
    for (bool lower_bound : {false, true}) {
        for (int expansions : config.expansions) {
            for (int probes : config.probes) {
                run(true, lower_bound, expansions, probes);
            }
        }
    }
    std::cerr << "Wrote " << config.output << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
}
#endif

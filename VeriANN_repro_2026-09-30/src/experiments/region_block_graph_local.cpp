#define main region_block_graph_base_main
#include "region_block_graph.cpp"
#undef main

struct RBLCfg {
    std::string data = "../dataset/sift";
    std::string output = "region_block_graph_local_sift100k.csv";
    int base = 100000;
    int queries = 100;
    int k = 10;
    int block_size = 64;
    int local_parts = 8;
    int cluster_iters = 5;
    int degree = 16;
    int entries = 8;
    std::vector<int> expansions{64, 128, 256};
    std::vector<int> probes{64, 96, 128, 192};
    std::vector<int> local_probes{2, 3, 4, 6, 8};
};

static RBLCfg rbl_args(int argc, char** argv) {
    RBLCfg config;
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
        else if (arg == "--local-parts") config.local_parts = std::stoi(value());
        else if (arg == "--cluster-iters") config.cluster_iters = std::stoi(value());
        else if (arg == "--degree") config.degree = std::stoi(value());
        else if (arg == "--entries") config.entries = std::stoi(value());
        else if (arg == "--expansions") config.expansions = rb_ints(value());
        else if (arg == "--probes") config.probes = rb_ints(value());
        else if (arg == "--local-probes") config.local_probes = rb_ints(value());
        else throw std::runtime_error("unknown option " + arg);
    }
    if (config.base < config.k || config.queries < 1 || config.block_size < 2 ||
        config.local_parts < 2 || config.degree < 2 || config.entries < 1) {
        throw std::runtime_error("invalid local-index configuration");
    }
    return config;
}

static std::vector<std::vector<int>> rbl_partition(
    const Matrix& data, const std::vector<int>& ids, int count, int iterations) {
    count = std::min<int>(count, ids.size());
    std::vector<std::vector<int>> blocks{ids};
    while (static_cast<int>(blocks.size()) < count) {
        auto largest = std::max_element(blocks.begin(), blocks.end(),
            [](const auto& a, const auto& b) { return a.size() < b.size(); });
        auto halves = split2_balanced(data, *largest, iterations);
        *largest = std::move(halves.first);
        blocks.push_back(std::move(halves.second));
    }
    for (auto& block : blocks) std::sort(block.begin(), block.end());
    return blocks;
}

static Hash rbl_subblock_leaf(int id, const RBSummary& summary, int count,
                              const Hash& object_root) {
    std::vector<uint8_t> bytes;
    const std::string domain = "rb/subblock/v1";
    bytes.insert(bytes.end(), domain.begin(), domain.end());
    rb_append(bytes, id);
    rb_append(bytes, count);
    rb_append(bytes, summary.radius[id]);
    rb_append(bytes, summary.error[id]);
    const auto code = summary.packed(id);
    bytes.insert(bytes.end(), code.begin(), code.end());
    rb_append(bytes, object_root);
    return sha(bytes.data(), bytes.size());
}

struct RBLMeasure {
    double hits = 0;
    double disclosed_regions = 0;
    double expanded_regions = 0;
    double selected_regions = 0;
    double selected_subblocks = 0;
    double visited_vectors = 0;
    double refined_vectors = 0;
    double exact_vectors = 0;
    double region_metadata = 0;
    double adjacency = 0;
    double hidden_adjacency = 0;
    double hidden_region_blocks = 0;
    double global_proof = 0;
    double local_metadata = 0;
    double hidden_object_roots = 0;
    double object_base = 0;
    double object_refinement = 0;
    double exact_bytes = 0;
    double milliseconds = 0;
    uint64_t violations = 0;
};

static RBLMeasure rbl_evaluate(
    int expansion_budget, int probes, int local_probe,
    const Matrix& data, const Matrix& queries,
    const std::vector<std::vector<int>>& regions,
    const std::vector<std::vector<int>>& graph,
    const std::vector<int>& entries, const RBSummary& region_summary,
    const std::vector<std::vector<int>>& region_subblocks,
    const std::vector<std::vector<int>>& subblocks,
    const RBSummary& subblock_summary, const RBNestedSQ& object_sq,
    const std::vector<std::vector<int>>& ground_truth) {
    RBLMeasure total;
    std::vector<uint8_t> selected_region(regions.size()), expanded(regions.size());
    for (size_t qi = 0; qi < queries.rows(); ++qi) {
        const auto begin = std::chrono::steady_clock::now();
        const RBTrace trace = rb_route(
            region_summary, graph, entries, queries[qi], expansion_budget, probes, false);
        std::fill(selected_region.begin(), selected_region.end(), 0);
        std::fill(expanded.begin(), expanded.end(), 0);
        for (int region : trace.selected) selected_region[region] = 1;
        for (int region : trace.expanded) expanded[region] = 1;

        std::vector<int> selected_subblocks;
        for (int region : trace.selected) {
            const auto& children = region_subblocks[region];
            const int take = std::min<int>(local_probe, children.size());
            std::vector<RBItem> order;
            order.reserve(children.size());
            for (int child : children) {
                order.push_back(
                    {subblock_summary.score(queries[qi], child, false), child});
            }
            std::partial_sort(order.begin(), order.begin() + take, order.end(),
                [](const auto& a, const auto& b) {
                    return a.score < b.score ||
                           (a.score == b.score && a.id < b.id);
                });
            for (int i = 0; i < take; ++i) selected_subblocks.push_back(order[i].id);
        }

        std::priority_queue<std::pair<float, int>> top;
        for (int child : selected_subblocks) {
            for (int id : subblocks[child]) {
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
            throw std::runtime_error("too few local-index candidates");
        }
        const float threshold = top.top().first;
        std::unordered_set<int> answer;
        while (!top.empty()) {
            answer.insert(top.top().second);
            top.pop();
        }
        for (int id : ground_truth[qi]) total.hits += answer.count(id);

        double visited = 0;
        double refined = 0;
        double exact = 0;
        for (int child : selected_subblocks) {
            visited += subblocks[child].size();
            for (int id : subblocks[child]) {
                const float truth = l2(queries[qi], data[id], data.d);
                const float lower4 = object_sq.lower2(queries[qi], id, 4);
                total.violations += lower4 > truth;
                if (lower4 <= threshold) {
                    ++refined;
                    const float lower6 = object_sq.lower2(queries[qi], id, 6);
                    total.violations += lower6 > truth;
                    if (lower6 <= threshold) ++exact;
                }
            }
        }

        total.disclosed_regions += trace.disclosed.size();
        total.expanded_regions += trace.expanded.size();
        total.selected_regions += trace.selected.size();
        total.selected_subblocks += selected_subblocks.size();
        total.visited_vectors += visited;
        total.refined_vectors += refined;
        total.exact_vectors += exact;
        total.region_metadata += trace.disclosed.size() * 80.0;
        for (int region : trace.disclosed) {
            if (expanded[region]) {
                total.adjacency += sizeof(uint32_t) +
                    graph[region].size() * sizeof(uint32_t);
            } else {
                total.hidden_adjacency += 32;
            }
            if (!selected_region[region]) total.hidden_region_blocks += 32;
        }
        total.global_proof += proof_count(regions.size(), trace.disclosed) * 32.0;
        for (int region : trace.selected) {
            total.local_metadata += region_subblocks[region].size() * 80.0;
            total.hidden_object_roots +=
                (region_subblocks[region].size() -
                 std::min<int>(local_probe, region_subblocks[region].size())) * 32.0;
        }
        total.object_base += visited * 100.0;
        total.object_refinement += refined * 64.0;
        total.exact_bytes += exact * data.d * sizeof(float);
        total.milliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin).count();
    }
    return total;
}

int main(int argc, char** argv) try {
    const RBLCfg config = rbl_args(argc, argv);
    std::cerr << "Loading data and exact ground truth...\n";
    const Matrix data = fvecs(config.data + "/sift_base.fvecs", config.base);
    const Matrix queries = fvecs(config.data + "/sift_query.fvecs", config.queries);
    const auto ground_truth = exact_gt(data, queries, config.k);

    const int region_count =
        (config.base + config.block_size - 1) / config.block_size;
    std::cerr << "Building " << region_count << " balanced regions...\n";
    const auto regions =
        hkmeans_balanced(data, region_count, config.cluster_iters);
    const auto region_centers = centers(data, regions);
    const auto graph = rb_graph(region_centers, config.degree);
    const auto entries = rb_entries(region_centers, config.entries);
    const RBSummary region_summary(data, regions, region_centers);

    std::cerr << "Building authenticated local sub-block indexes...\n";
    std::vector<std::vector<int>> region_subblocks(regions.size());
    std::vector<std::vector<int>> subblocks;
    for (size_t region = 0; region < regions.size(); ++region) {
        auto local = rbl_partition(
            data, regions[region], config.local_parts, config.cluster_iters);
        for (auto& child : local) {
            region_subblocks[region].push_back(subblocks.size());
            subblocks.push_back(std::move(child));
        }
    }
    const auto subblock_centers = centers(data, subblocks);
    const RBSummary subblock_summary(data, subblocks, subblock_centers);
    const RBNestedSQ object_sq(data);

    std::vector<Hash> object_roots(subblocks.size());
    std::vector<Hash> subblock_leaves(subblocks.size());
    for (size_t child = 0; child < subblocks.size(); ++child) {
        std::vector<Hash> leaves;
        for (int id : subblocks[child]) {
            leaves.push_back(rb_object_leaf(data, object_sq, id));
        }
        object_roots[child] = merkle(std::move(leaves));
        subblock_leaves[child] = rbl_subblock_leaf(
            child, subblock_summary, subblocks[child].size(), object_roots[child]);
    }
    std::vector<Hash> local_roots(regions.size());
    for (size_t region = 0; region < regions.size(); ++region) {
        std::vector<Hash> leaves;
        for (int child : region_subblocks[region]) {
            leaves.push_back(subblock_leaves[child]);
        }
        local_roots[region] = merkle(std::move(leaves));
    }
    std::vector<Hash> region_leaves(regions.size());
    for (size_t region = 0; region < regions.size(); ++region) {
        region_leaves[region] = rb_region_leaf(
            region, region_summary, regions[region].size(),
            local_roots[region], graph[region]);
    }
    const Hash root = merkle(region_leaves);
    auto altered = region_leaves;
    altered.front().front() ^= 1;
    if (merkle(altered) == root) {
        throw std::runtime_error("local-index Merkle tamper self-test failed");
    }

    std::ofstream output(config.output);
    if (!output) throw std::runtime_error("cannot write " + config.output);
    output << std::setprecision(12)
           << "base,queries,k,target_block_size,local_parts,regions,degree,entries,"
              "expansion_budget,nprobe,local_probe,recall,avg_disclosed_regions,"
              "avg_expanded_regions,avg_selected_regions,avg_selected_subblocks,"
              "avg_visited_vectors,avg_refined_vectors,avg_exact_vectors,"
              "region_metadata_bytes,adjacency_bytes,hidden_adjacency_hash_bytes,"
              "hidden_region_block_root_bytes,global_merkle_bytes,local_metadata_bytes,"
              "hidden_object_root_bytes,object_base_bytes,object_refinement_bytes,"
              "exact_vector_bytes,routing_vo_bytes,local_index_vo_bytes,"
              "object_vo_bytes,total_vo_bytes,reduction_vs_merkle_hnsw,query_ms,"
              "bound_violations\n";

    for (int expansion : config.expansions) {
        for (int probes : config.probes) {
            for (int local_probe : config.local_probes) {
                if (local_probe > config.local_parts) continue;
                std::cerr << "Running expand=" << expansion << " regions=" << probes
                          << " local=" << local_probe << "...\n";
                const RBLMeasure value = rbl_evaluate(
                    expansion, probes, local_probe, data, queries, regions, graph,
                    entries, region_summary, region_subblocks, subblocks,
                    subblock_summary, object_sq, ground_truth);
                const double nq = queries.rows();
                const double routing = 20 + config.entries * 4 +
                    (value.region_metadata + value.adjacency +
                     value.hidden_adjacency + value.hidden_region_blocks +
                     value.global_proof) / nq;
                const double local_index =
                    (value.local_metadata + value.hidden_object_roots) / nq;
                const double object =
                    (value.object_base + value.object_refinement +
                     value.exact_bytes) / nq;
                const double total = routing + local_index + object;
                const double recall = value.hits / (nq * config.k);
                constexpr double hnsw_baseline = 378653.95;
                output << config.base << ',' << config.queries << ',' << config.k
                       << ',' << config.block_size << ',' << config.local_parts
                       << ',' << regions.size() << ',' << config.degree << ','
                       << config.entries << ',' << expansion << ',' << probes
                       << ',' << local_probe << ',' << recall << ','
                       << value.disclosed_regions / nq << ','
                       << value.expanded_regions / nq << ','
                       << value.selected_regions / nq << ','
                       << value.selected_subblocks / nq << ','
                       << value.visited_vectors / nq << ','
                       << value.refined_vectors / nq << ','
                       << value.exact_vectors / nq << ','
                       << value.region_metadata / nq << ','
                       << value.adjacency / nq << ','
                       << value.hidden_adjacency / nq << ','
                       << value.hidden_region_blocks / nq << ','
                       << value.global_proof / nq << ','
                       << value.local_metadata / nq << ','
                       << value.hidden_object_roots / nq << ','
                       << value.object_base / nq << ','
                       << value.object_refinement / nq << ','
                       << value.exact_bytes / nq << ',' << routing << ','
                       << local_index << ',' << object << ',' << total << ','
                       << 1 - total / hnsw_baseline << ','
                       << value.milliseconds / nq << ',' << value.violations << '\n';
                std::cout << "e=" << expansion << " p=" << probes
                          << " lp=" << local_probe << " recall=" << std::fixed
                          << std::setprecision(4) << recall << " vectors="
                          << std::setprecision(1) << value.visited_vectors / nq
                          << " exact=" << value.exact_vectors / nq << " route="
                          << routing / 1024 << " KiB local=" << local_index / 1024
                          << " KiB object=" << object / 1024 << " KiB total="
                          << total / 1024 << " KiB vs-HNSW="
                          << std::setprecision(2)
                          << (1 - total / hnsw_baseline) * 100 << "% violations="
                          << value.violations << '\n';
            }
        }
    }
    std::cerr << "Wrote " << config.output << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
}

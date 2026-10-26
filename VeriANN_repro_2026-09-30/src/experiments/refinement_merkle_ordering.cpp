#define EXPANSION_CENTRIC_NO_MAIN
#include "expansion_centric_merkle.cpp"

namespace {

struct OrderingExperiment {
    std::string name;
    std::vector<int> order;
    std::vector<int> position;
    std::unique_ptr<vann::MerkleTree> delta_tree;
    std::unique_ptr<vann::MerkleTree> raw_tree;
};

struct OrderingAggregate {
    double hits = 0;
    double expanded = 0;
    double disclosed = 0;
    double delta_targets = 0;
    double raw_targets = 0;
    double refinement_targets = 0;
    double refinement_siblings = 0;
    double delta_proof = 0;
    double raw_proof = 0;
    double route_proof = 0;
    double whole_proof = 0;
    double total_vo = 0;
};

std::vector<int> node_id_order(std::size_t n) {
    std::vector<int> order(n);
    for (std::size_t i = 0; i < n; ++i) order[i] = static_cast<int>(i);
    return order;
}

std::vector<int> deterministic_bfs_order(const vann::HnswIndex& h, int& components) {
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
        if (!seen[id]) visit_component(static_cast<int>(id));
    }
    return order;
}

std::vector<int> sq4_code_order(const AuthSQ& sq, std::size_t n) {
    std::vector<std::vector<std::uint8_t>> codes(n);
    std::vector<int> order(n);
    for (std::size_t id = 0; id < n; ++id) {
        codes[id] = sq.sq4(static_cast<int>(id));
        order[id] = static_cast<int>(id);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (codes[static_cast<std::size_t>(a)] != codes[static_cast<std::size_t>(b)])
            return codes[static_cast<std::size_t>(a)] < codes[static_cast<std::size_t>(b)];
        return a < b;
    });
    return order;
}

void build_refinement_trees(OrderingExperiment& experiment, const vann::HnswIndex& h,
                            const AuthSQ& sq) {
    const std::size_t n = h.size();
    experiment.position.resize(n);
    std::vector<vann::Hash> delta_leaves(n), raw_leaves(n);
    for (std::size_t position = 0; position < n; ++position) {
        const int id = experiment.order[position];
        experiment.position[static_cast<std::size_t>(id)] = static_cast<int>(position);
        delta_leaves[position] = leaf_delta(sq, id);
        raw_leaves[position] = leaf_raw(h, id);
    }
    experiment.delta_tree = std::make_unique<vann::MerkleTree>(std::move(delta_leaves));
    experiment.raw_tree = std::make_unique<vann::MerkleTree>(std::move(raw_leaves));
}

}  // namespace

#ifndef REFINEMENT_ORDERING_NO_MAIN
int main() try {
    using namespace vann;
    Config config;
    config.queries = 100;
    Matrix base = read_fvecs(config.dataset + "/sift_base.fvecs", config.base);
    Matrix queries = read_fvecs(config.dataset + "/sift_query.fvecs", config.queries);
    HnswIndex hnsw = build_hnsw(base, config.M, config.efc, config.cache);
    base.values.clear();
    base.values.shrink_to_fit();
    ESQ route_sq(hnsw);
    AuthSQ auth_sq(hnsw);
    auto ground_truth_ids = ground_truth(hnsw, queries, config.k);

    const std::size_t n = hnsw.size();
    std::vector<std::size_t> adjacency_base(n + 1, 0);
    for (std::size_t id = 0; id < n; ++id)
        adjacency_base[id + 1] = adjacency_base[id] + hnsw.index->hnsw.levels[id];
    const std::size_t adjacency_records = adjacency_base.back();
    const std::string snapshot_name = "sift100k-static-snapshot-v1";
    const Hash snapshot = sha256(snapshot_name.data(), snapshot_name.size());

    std::cerr << "Building factorized route tree and refinement ordering trees...\n";
    std::vector<Hash> route_leaves(adjacency_records);
    for (std::size_t id = 0; id < n; ++id) {
        for (int level = 0; level < hnsw.index->hnsw.levels[id]; ++level) {
            const std::size_t position = adjacency_base[id] + static_cast<std::size_t>(level);
            route_leaves[position] =
                leaf_er_direct(hnsw, auth_sq, snapshot, static_cast<int>(id), level);
        }
    }
    MerkleTree route_tree(std::move(route_leaves));

    int bfs_components = 0;
    std::vector<OrderingExperiment> experiments;
    experiments.push_back({"Node-ID", node_id_order(n)});
    experiments.push_back({"HNSW-BFS", deterministic_bfs_order(hnsw, bfs_components)});
    experiments.push_back({"SQ4-code", sq4_code_order(auth_sq, n)});
    for (auto& experiment : experiments) build_refinement_trees(experiment, hnsw, auth_sq);

    std::vector<OrderingAggregate> aggregates(experiments.size());
    std::ofstream per_query("refinement_merkle_ordering_per_query.csv");
    per_query << "query,method,recall_at_10,expanded,disclosed,delta_targets,raw_targets,"
                 "refinement_unique_leaf_targets,refinement_sibling_hashes,delta_proof_bytes,"
                 "raw_proof_bytes,total_refinement_proof_bytes,route_proof_bytes,"
                 "whole_query_proof_bytes,total_vo_bytes\n";

    for (int query_id = 0; query_id < config.queries; ++query_id) {
        ETrace trace = run_e(hnsw, route_sq, queries.row(query_id), config.k, 64, "sq4");
        std::unordered_set<int> answer(trace.answer.begin(), trace.answer.end());
        double hits = 0;
        for (int id : ground_truth_ids[query_id]) hits += answer.count(id);

        std::unordered_map<int, std::vector<std::uint8_t>> sq4_cache;
        for (int id : trace.disclosed) sq4_cache.emplace(id, auth_sq.sq4(id));
        std::vector<int> route_targets;
        for (const auto& opened : trace.opened) {
            const std::size_t position =
                adjacency_base[static_cast<std::size_t>(opened.local_id)] +
                static_cast<std::size_t>(opened.level);
            if (leaf_er_direct_cached(hnsw, sq4_cache, snapshot, opened.local_id, opened.level) !=
                route_tree.leaf(position))
                throw std::runtime_error("route ER reconstruction mismatch");
            route_targets.push_back(static_cast<int>(position));
        }
        route_targets = unique(std::move(route_targets));
        const MerkleProof route_proof = route_tree.prove(route_targets);
        verify_merkle_selection(route_tree, route_targets, route_proof);

        const double payload =
            trace.sq4 + trace.delta + trace.raw_bytes + trace.adjacency + trace.metadata;
        for (std::size_t method = 0; method < experiments.size(); ++method) {
            auto& experiment = experiments[method];
            auto& aggregate = aggregates[method];
            std::vector<int> delta_targets, raw_targets;
            delta_targets.reserve(trace.refined.size());
            raw_targets.reserve(trace.raw.size());
            for (int id : trace.refined)
                delta_targets.push_back(experiment.position[static_cast<std::size_t>(id)]);
            for (int id : trace.raw)
                raw_targets.push_back(experiment.position[static_cast<std::size_t>(id)]);
            delta_targets = unique(std::move(delta_targets));
            raw_targets = unique(std::move(raw_targets));

            const MerkleProof delta_proof = experiment.delta_tree->prove(delta_targets);
            const MerkleProof raw_proof = experiment.raw_tree->prove(raw_targets);
            verify_merkle_selection(*experiment.delta_tree, delta_targets, delta_proof);
            verify_merkle_selection(*experiment.raw_tree, raw_targets, raw_proof);

            const double refinement_siblings =
                delta_proof.nodes.size() + raw_proof.nodes.size();
            const double refinement_proof = delta_proof.bytes() + raw_proof.bytes();
            const double whole_proof = route_proof.bytes() + refinement_proof;
            const double total_vo = payload + whole_proof;
            aggregate.hits += hits;
            aggregate.expanded += trace.expanded.size();
            aggregate.disclosed += trace.disclosed.size();
            aggregate.delta_targets += delta_targets.size();
            aggregate.raw_targets += raw_targets.size();
            aggregate.refinement_targets += delta_targets.size() + raw_targets.size();
            aggregate.refinement_siblings += refinement_siblings;
            aggregate.delta_proof += delta_proof.bytes();
            aggregate.raw_proof += raw_proof.bytes();
            aggregate.route_proof += route_proof.bytes();
            aggregate.whole_proof += whole_proof;
            aggregate.total_vo += total_vo;

            per_query << query_id << ',' << experiment.name << ',' << hits / config.k << ','
                      << trace.expanded.size() << ',' << trace.disclosed.size() << ','
                      << delta_targets.size() << ',' << raw_targets.size() << ','
                      << delta_targets.size() + raw_targets.size() << ','
                      << refinement_siblings << ',' << delta_proof.bytes() << ','
                      << raw_proof.bytes() << ',' << refinement_proof << ','
                      << route_proof.bytes() << ',' << whole_proof << ',' << total_vo << '\n';
        }
    }

    const double query_count = config.queries;
    const double baseline_refinement =
        aggregates[0].delta_proof + aggregates[0].raw_proof;
    const double baseline_total = aggregates[0].total_vo;
    std::ofstream summary("refinement_merkle_ordering_q100.csv");
    summary << "ordering,recall_at_10,avg_expanded,avg_disclosed,delta_leaf_targets,"
               "raw_leaf_targets,refinement_unique_leaf_targets,"
               "refinement_multiproof_sibling_hashes,delta_proof_kib,raw_proof_kib,"
               "total_refinement_merkle_proof_kib,route_proof_kib,"
               "whole_query_merkle_proof_kib,total_vo_kib,"
               "refinement_proof_reduction_percent,total_vo_reduction_percent\n";
    for (std::size_t method = 0; method < experiments.size(); ++method) {
        const auto& aggregate = aggregates[method];
        const double refinement = aggregate.delta_proof + aggregate.raw_proof;
        const double refinement_reduction =
            100.0 * (baseline_refinement - refinement) / baseline_refinement;
        const double total_reduction =
            100.0 * (baseline_total - aggregate.total_vo) / baseline_total;
        summary << experiments[method].name << ',' << aggregate.hits / (query_count * config.k)
                << ',' << aggregate.expanded / query_count << ','
                << aggregate.disclosed / query_count << ','
                << aggregate.delta_targets / query_count << ','
                << aggregate.raw_targets / query_count << ','
                << aggregate.refinement_targets / query_count << ','
                << aggregate.refinement_siblings / query_count << ','
                << aggregate.delta_proof / query_count / 1024.0 << ','
                << aggregate.raw_proof / query_count / 1024.0 << ','
                << refinement / query_count / 1024.0 << ','
                << aggregate.route_proof / query_count / 1024.0 << ','
                << aggregate.whole_proof / query_count / 1024.0 << ','
                << aggregate.total_vo / query_count / 1024.0 << ','
                << refinement_reduction << ',' << total_reduction << '\n';
        std::cout << experiments[method].name << " refinement=" << std::fixed
                  << std::setprecision(4) << refinement / query_count / 1024.0
                  << " KiB whole_proof=" << aggregate.whole_proof / query_count / 1024.0
                  << " KiB total=" << aggregate.total_vo / query_count / 1024.0
                  << " KiB refinement_reduction=" << refinement_reduction << "%\n";
    }
    std::cerr << "BFS components=" << bfs_components
              << ", wrote refinement_merkle_ordering_q100.csv\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
}
#endif

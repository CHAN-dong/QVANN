#define REGION_BLOCK_GRAPH_NO_MAIN
#include "region_block_graph.cpp"
#undef REGION_BLOCK_GRAPH_NO_MAIN

#include "../verifiable_ann_common.hpp"

#include <deque>

struct HBConfig {
    std::string data = "../dataset/sift";
    std::string cache = "../merkle_hnsw/sift100k_M16_efc100.index";
    std::string output = "hnsw_block_graph.csv";
    std::string edge_order = "support";
    std::string routing_mode = "flat";
    std::string summary_mode = "center";
    std::string graph_source = "contracted";
    std::string block_order = "links";
    std::string block_cache;
    int base = 100000;
    int queries = 100;
    int k = 10;
    int block_size = 8;
    int M = 16;
    int ef_construction = 100;
    int degree = 0;
    int block_M = 8;
    int portal_bits = 4;
    int portal_divisor = 4;
    bool portal_residual = false;
    int entry_count = 1;
    std::vector<int> expansions{16, 32, 64, 128};
    std::vector<int> probes{16, 32, 64, 128, 256};
};

static HBConfig hb_args(int argc, char** argv) {
    HBConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() {
            if (++i == argc) throw std::runtime_error("missing value after " + arg);
            return std::string(argv[i]);
        };
        if (arg == "--dataset") config.data = value();
        else if (arg == "--cache") config.cache = value();
        else if (arg == "--routing") config.routing_mode = value();
        else if (arg == "--summary") config.summary_mode = value();
        else if (arg == "--graph-source") config.graph_source = value();
        else if (arg == "--block-order") config.block_order = value();
        else if (arg == "--block-cache") config.block_cache = value();
        else if (arg == "--edge-order") config.edge_order = value();
        else if (arg == "--output") config.output = value();
        else if (arg == "--base") config.base = std::stoi(value());
        else if (arg == "--queries") config.queries = std::stoi(value());
        else if (arg == "--k") config.k = std::stoi(value());
        else if (arg == "--block-size") config.block_size = std::stoi(value());
        else if (arg == "--M") config.M = std::stoi(value());
        else if (arg == "--ef-construction") config.ef_construction = std::stoi(value());
        else if (arg == "--degree") config.degree = std::stoi(value());
        else if (arg == "--block-M") config.block_M = std::stoi(value());
        else if (arg == "--portal-bits") config.portal_bits = std::stoi(value());
        else if (arg == "--portal-divisor") config.portal_divisor = std::stoi(value());
        else if (arg == "--portal-residual") config.portal_residual = std::stoi(value()) != 0;
        else if (arg == "--entries") config.entry_count = std::stoi(value());
        else if (arg == "--expansions") config.expansions = rb_ints(value());
        else if (arg == "--probes") config.probes = rb_ints(value());
        else throw std::runtime_error("unknown option " + arg);
    }
    if (config.base < config.k || config.queries < 1 || config.block_size < 2 ||
        config.M < 2 || config.degree < 0 || config.block_M < 2 ||
        config.portal_bits < 1 || config.portal_bits > 8 ||
        config.portal_divisor < 1 || config.entry_count < 1 ||
        config.expansions.empty() ||
        config.probes.empty()) {
        throw std::runtime_error("invalid HNSW block configuration");
    }
    if (config.edge_order != "support" && config.edge_order != "distance") {
        throw std::runtime_error("edge order must be support or distance");
    }
    if (config.routing_mode != "flat" && config.routing_mode != "multilevel") {
        throw std::runtime_error("routing mode must be flat or multilevel");
    }
    if (config.summary_mode != "center" && config.summary_mode != "representative") {
        throw std::runtime_error("summary mode must be center or representative");
    }
    if (config.graph_source != "contracted" && config.graph_source != "native" &&
        config.graph_source != "portal-sq4" && config.graph_source != "portal-sq6" &&
        config.graph_source != "hybrid-sq4" &&
        config.graph_source != "member-sq4" &&
        config.graph_source != "portal-block") {
        throw std::runtime_error(
            "graph source must be contracted, native, portal-sq4, portal-sq6, "
            "hybrid-sq4, member-sq4, or portal-block");
    }
    if (config.block_order != "links" && config.block_order != "overlap") {
        throw std::runtime_error("block order must be links or overlap");
    }
    return config;
}

static std::vector<int> hb_seed_order(const vann::HnswIndex& hnsw) {
    std::vector<int> order;
    std::vector<uint8_t> seen(hnsw.size());
    std::deque<int> queue;
    queue.push_back(hnsw.index->hnsw.entry_point);
    seen[queue.front()] = 1;
    while (!queue.empty()) {
        const int current = queue.front();
        queue.pop_front();
        order.push_back(current);
        const int levels = hnsw.index->hnsw.levels[current];
        for (int level = levels - 1; level >= 0; --level) {
            for (int neighbor : hnsw.neighbors(current, level)) {
                if (!seen[neighbor]) {
                    seen[neighbor] = 1;
                    queue.push_back(neighbor);
                }
            }
        }
    }
    for (size_t id = 0; id < hnsw.size(); ++id) {
        if (!seen[id]) order.push_back(id);
    }
    return order;
}

// Grow blocks through original HNSW edges. Prefer candidates with more internal
// links and shared boundary neighbors, then use vector distance as a tie-break.
static std::vector<std::vector<int>> hb_blocks(
    const Matrix& data, const vann::HnswIndex& hnsw, int block_size,
    const std::string& block_order) {
    const std::vector<int> seeds = hb_seed_order(hnsw);
    std::vector<int> owner(hnsw.size(), -1);
    std::vector<std::vector<int>> object_graph(hnsw.size());
    for (size_t id = 0; id < hnsw.size(); ++id) {
        std::unordered_set<int> adjacent;
        for (int level = 0; level < hnsw.index->hnsw.levels[id]; ++level) {
            for (int neighbor : hnsw.neighbors(id, level)) adjacent.insert(neighbor);
        }
        object_graph[id].assign(adjacent.begin(), adjacent.end());
        std::sort(object_graph[id].begin(), object_graph[id].end());
    }
    std::vector<std::vector<int>> incoming_graph(hnsw.size());
    for (size_t id = 0; id < hnsw.size(); ++id) {
        for (int neighbor : object_graph[id]) {
            incoming_graph[neighbor].push_back(static_cast<int>(id));
        }
    }
    for (auto& incoming : incoming_graph) {
        std::sort(incoming.begin(), incoming.end());
        incoming.erase(std::unique(incoming.begin(), incoming.end()), incoming.end());
    }
    std::vector<std::vector<int>> blocks;
    for (int seed : seeds) {
        if (owner[seed] >= 0) continue;
        const int block = blocks.size();
        blocks.push_back({seed});
        owner[seed] = block;
        std::unordered_set<int> frontier;
        auto add_neighbors = [&](int id) {
            for (int neighbor : object_graph[id]) {
                if (owner[neighbor] < 0) frontier.insert(neighbor);
            }
        };
        add_neighbors(seed);
        if (block_order == "overlap") {
            frontier.clear();
            for (int predecessor : incoming_graph[seed]) {
                for (int co_neighbor : object_graph[predecessor]) {
                    if (owner[co_neighbor] < 0) frontier.insert(co_neighbor);
                }
            }
            if (frontier.empty()) add_neighbors(seed);
        }
        while (static_cast<int>(blocks.back().size()) < block_size &&
               !frontier.empty()) {
            int next = -1;
            int best_links = -1;
            int best_overlap = -1;
            float best_distance = std::numeric_limits<float>::infinity();
            std::unordered_set<int> boundary;
            for (int member : blocks.back()) {
                const auto& neighborhood =
                    block_order == "overlap" ? incoming_graph[member] : object_graph[member];
                boundary.insert(neighborhood.begin(), neighborhood.end());
            }
            for (int candidate : frontier) {
                if (owner[candidate] >= 0) continue;
                int links = 0;
                for (int member : blocks.back()) {
                    links += std::binary_search(object_graph[candidate].begin(),
                                                object_graph[candidate].end(), member);
                }
                int overlap = 0;
                const auto& candidate_neighborhood =
                    block_order == "overlap" ? incoming_graph[candidate] :
                                                object_graph[candidate];
                for (int neighbor : candidate_neighborhood) {
                    overlap += boundary.count(neighbor);
                }
                const float distance = l2(data[seed], data[candidate], data.d);
                const bool better =
                    block_order == "overlap"
                    ? (overlap > best_overlap ||
                       (overlap == best_overlap && links > best_links) ||
                       (overlap == best_overlap && links == best_links &&
                        (distance < best_distance ||
                         (distance == best_distance && candidate < next))))
                    : (links > best_links ||
                       (links == best_links && overlap > best_overlap) ||
                       (links == best_links && overlap == best_overlap &&
                        (distance < best_distance ||
                         (distance == best_distance && candidate < next))));
                if (better) {
                    next = candidate;
                    best_links = links;
                    best_overlap = overlap;
                    best_distance = distance;
                }
            }
            if (next < 0) break;
            frontier.erase(next);
            owner[next] = block;
            blocks.back().push_back(next);
            add_neighbors(next);
        }
        if (static_cast<int>(blocks.back().size()) < block_size) {
            for (int fallback : seeds) {
                if (owner[fallback] < 0) {
                    owner[fallback] = block;
                    blocks.back().push_back(fallback);
                    if (static_cast<int>(blocks.back().size()) == block_size) break;
                }
            }
        }
        std::sort(blocks.back().begin(), blocks.back().end());
    }
    for (int value : owner) {
        if (value < 0) throw std::runtime_error("HNSW block assignment is incomplete");
    }
    return blocks;
}

// Contract original edges, rank them as authenticated portals, and bound the
static std::vector<int> hb_entry_blocks(const vann::HnswIndex& hnsw,
                                        const std::vector<std::vector<int>>& blocks,
                                        int count);

// outgoing degree. High HNSW layers preserve long-range navigation.
static std::vector<std::vector<int>> hb_contract_graph(
    const std::vector<std::vector<float>>& centers, const vann::HnswIndex& hnsw,
    const std::vector<std::vector<int>>& blocks, int max_degree,
    const std::string& edge_order, int portal_divisor,
    double* full_average_degree, size_t* full_maximum_degree) {
    std::vector<int> owner(hnsw.size(), -1);
    for (size_t block = 0; block < blocks.size(); ++block) {
        for (int id : blocks[block]) owner[id] = block;
    }
    struct EdgeValue { int support = 0; int highest_level = -1; };
    std::vector<std::unordered_map<int, EdgeValue>> edges(blocks.size());
    for (size_t id = 0; id < hnsw.size(); ++id) {
        const int from = owner[id];
        const int levels = hnsw.index->hnsw.levels[id];
        for (int level = 0; level < levels; ++level) {
            for (int neighbor : hnsw.neighbors(id, level)) {
                const int to = owner[neighbor];
                if (from != to) {
                    auto& edge = edges[from][to];
                    ++edge.support;
                    edge.highest_level = std::max(edge.highest_level, level);
                    auto& reverse = edges[to][from];
                    ++reverse.support;
                    reverse.highest_level = std::max(reverse.highest_level, level);
                }
            }
        }
    }
    *full_average_degree = 0;
    *full_maximum_degree = 0;
    std::vector<std::vector<int>> graph(blocks.size());
    for (size_t block = 0; block < blocks.size(); ++block) {
        *full_average_degree += edges[block].size();
        *full_maximum_degree = std::max(*full_maximum_degree, edges[block].size());
        std::vector<std::pair<int, EdgeValue>> ranked(edges[block].begin(), edges[block].end());
        auto portal_order = [&](const auto& a, const auto& b) {
            if (a.second.highest_level != b.second.highest_level)
                return a.second.highest_level > b.second.highest_level;
            if (a.second.support != b.second.support)
                return a.second.support > b.second.support;
            const float da = l2(centers[block].data(), centers[a.first].data(),
                                centers[block].size());
            const float db = l2(centers[block].data(), centers[b.first].data(),
                                centers[block].size());
            return da < db || (da == db && a.first < b.first);
        };
        auto local_order = [&](const auto& a, const auto& b) {
            const float da = l2(centers[block].data(), centers[a.first].data(),
                                centers[block].size());
            const float db = l2(centers[block].data(), centers[b.first].data(),
                                centers[block].size());
            if (edge_order == "distance") {
                if (da != db) return da < db;
                if (a.second.support != b.second.support) {
                    return a.second.support > b.second.support;
                }
            } else if (a.second.support != b.second.support) {
                return a.second.support > b.second.support;
            }
            return da < db || (da == db && a.first < b.first);
        };
        const size_t keep = max_degree == 0 ? ranked.size() :
            std::min(ranked.size(), static_cast<size_t>(max_degree));
        if (max_degree == 0) {
            std::sort(ranked.begin(), ranked.end(), local_order);
            for (const auto& edge : ranked) graph[block].push_back(edge.first);
        } else {
            std::vector<std::pair<int, EdgeValue>> portals;
            for (const auto& edge : ranked) {
                if (edge.second.highest_level > 0) portals.push_back(edge);
            }
            std::sort(portals.begin(), portals.end(), portal_order);
            const size_t portal_keep = std::min(
                portals.size(), std::max<size_t>(1, keep / portal_divisor));
            std::unordered_set<int> chosen;
            for (size_t i = 0; i < portal_keep; ++i) {
                graph[block].push_back(portals[i].first);
                chosen.insert(portals[i].first);
            }
            std::sort(ranked.begin(), ranked.end(), local_order);
            for (const auto& edge : ranked) {
                if (graph[block].size() == keep) break;
                if (chosen.insert(edge.first).second) graph[block].push_back(edge.first);
            }
        }
    }
    *full_average_degree /= blocks.size();
    return graph;
}

using HBLayerGraph = std::vector<std::vector<std::vector<int>>>;

static HBLayerGraph hb_contract_layers(
    const std::vector<std::vector<float>>& centers, const vann::HnswIndex& hnsw,
    const std::vector<std::vector<int>>& blocks, int base_degree) {
    std::vector<int> owner(hnsw.size(), -1);
    for (size_t block = 0; block < blocks.size(); ++block)
        for (int id : blocks[block]) owner[id] = block;
    int layer_count = 1;
    for (int levels : hnsw.index->hnsw.levels) layer_count = std::max(layer_count, levels);
    HBLayerGraph layers(layer_count,
        std::vector<std::vector<int>>(blocks.size()));
    for (int level = 0; level < layer_count; ++level) {
        std::vector<std::unordered_map<int, int>> support(blocks.size());
        for (size_t id = 0; id < hnsw.size(); ++id) {
            if (hnsw.index->hnsw.levels[id] <= level) continue;
            const int from = owner[id];
            for (int neighbor : hnsw.neighbors(id, level)) {
                const int to = owner[neighbor];
                if (from != to) ++support[from][to];
            }
        }
        const int cap = base_degree == 0 ? 0 :
            (level == 0 ? base_degree : std::max(2, base_degree / 4));
        for (size_t block = 0; block < blocks.size(); ++block) {
            std::vector<std::pair<int, int>> ranked(support[block].begin(),
                                                    support[block].end());
            std::sort(ranked.begin(), ranked.end(), [&](const auto& a, const auto& b) {
                const float da = l2(centers[block].data(), centers[a.first].data(),
                                    centers[block].size());
                const float db = l2(centers[block].data(), centers[b.first].data(),
                                    centers[block].size());
                if (level > 0 && a.second != b.second) return a.second > b.second;
                return da < db || (da == db && a.first < b.first);
            });
            const size_t keep = cap == 0 ? ranked.size() :
                std::min(ranked.size(), static_cast<size_t>(cap));
            for (size_t i = 0; i < keep; ++i)
                layers[level][block].push_back(ranked[i].first);
        }
    }
    return layers;
}

static std::vector<std::vector<int>> hb_flatten_layers(const HBLayerGraph& layers) {
    std::vector<std::vector<int>> graph(layers.front().size());
    for (size_t block = 0; block < graph.size(); ++block) {
        std::unordered_set<int> neighbors;
        for (const auto& layer : layers)
            neighbors.insert(layer[block].begin(), layer[block].end());
        graph[block].assign(neighbors.begin(), neighbors.end());
        std::sort(graph[block].begin(), graph[block].end());
    }
    return graph;
}

static std::vector<RBTrace> hb_multilevel_traces(
    const HBLayerGraph& layers, const RBSummary& summary,
    const std::vector<int>& entries, const Matrix& queries, int budget, int probes) {
    std::vector<RBTrace> traces(queries.rows());
    for (size_t qi = 0; qi < queries.rows(); ++qi) {
        RBTrace& trace = traces[qi];
        std::vector<uint8_t> disclosed(layers.front().size());
        std::vector<float> scores(layers.front().size());
        auto reveal = [&](int block) {
            if (!disclosed[block]) {
                disclosed[block] = 1;
                scores[block] = summary.score(queries[qi], block, false);
                trace.disclosed.push_back(block);
            }
        };
        std::vector<uint8_t> expanded(layers.front().size());
        auto open = [&](int block) {
            if (!expanded[block]) {
                expanded[block] = 1;
                trace.expanded.push_back(block);
            }
        };
        int current = entries.front();
        reveal(current);
        for (int level = static_cast<int>(layers.size()) - 1; level > 0; --level) {
            while (true) {
                open(current);
                int best = current;
                for (int neighbor : layers[level][current]) {
                    reveal(neighbor);
                    if (scores[neighbor] < scores[best] ||
                        (scores[neighbor] == scores[best] && neighbor < best)) best = neighbor;
                }
                if (best == current) break;
                current = best;
            }
        }
        std::vector<uint8_t> base_seen(layers.front().size()), base_done(layers.front().size());
        std::priority_queue<RBItem, std::vector<RBItem>, RBGreater> frontier;
        auto base_discover = [&](int block) {
            reveal(block);
            if (!base_seen[block]) {
                base_seen[block] = 1;
                frontier.push({scores[block], block});
            }
        };
        base_discover(current);
        int base_expanded = 0;
        while (base_expanded < budget && !frontier.empty()) {
            const RBItem item = frontier.top();
            frontier.pop();
            if (base_done[item.id]) continue;
            base_done[item.id] = 1;
            ++base_expanded;
            open(item.id);
            for (int neighbor : layers[0][item.id]) base_discover(neighbor);
        }
        if (static_cast<int>(trace.disclosed.size()) < probes)
            throw std::runtime_error("multilevel routing disclosed fewer blocks than topBlocks");
        std::vector<RBItem> candidates;
        candidates.reserve(trace.disclosed.size());
        for (int block : trace.disclosed) candidates.push_back({scores[block], block});
        std::partial_sort(candidates.begin(), candidates.begin() + probes, candidates.end(),
            [](const RBItem& a, const RBItem& b) {
                return a.score < b.score || (a.score == b.score && a.id < b.id);
            });
        for (int i = 0; i < probes; ++i) trace.selected.push_back(candidates[i].id);
        std::sort(trace.disclosed.begin(), trace.disclosed.end());
        std::sort(trace.expanded.begin(), trace.expanded.end());
        std::sort(trace.selected.begin(), trace.selected.end());
    }
    return traces;
}
struct HBPortalBatch {
    std::vector<RBTrace> traces;
    double disclosed_objects = 0;
    double expanded_objects = 0;
    double payload_bytes = 0;
    double auth_bytes = 0;
};

static HBPortalBatch hb_portal_traces(
    const vann::HnswIndex& hnsw, const RBNestedSQ& sq,
    const std::vector<std::vector<int>>& blocks, const Matrix& queries,
    int ef_search, int candidate_objects, int bits) {
    if (candidate_objects < 1 || candidate_objects > ef_search) {
        throw std::runtime_error("portal candidate count must be in [1, efSearch]");
    }
    std::vector<int> owner(hnsw.size(), -1);
    std::vector<int> position(hnsw.size(), -1);
    int next_position = 0;
    for (size_t block = 0; block < blocks.size(); ++block) {
        for (int id : blocks[block]) {
            owner[id] = static_cast<int>(block);
            position[id] = next_position++;
        }
    }
    HBPortalBatch batch;
    batch.traces.resize(queries.rows());
    for (size_t qi = 0; qi < queries.rows(); ++qi) {
        std::vector<uint8_t> revealed(hnsw.size());
        std::vector<int> disclosed_objects;
        std::vector<vann::OpenedAdjacency> opened;
        std::unordered_set<uint64_t> opened_keys;
        auto reveal = [&](int id) {
            if (!revealed[id]) {
                revealed[id] = 1;
                disclosed_objects.push_back(id);
            }
        };
        auto open = [&](int id, int level) {
            const uint64_t key =
                (static_cast<uint64_t>(static_cast<uint32_t>(id)) << 32U) |
                static_cast<uint32_t>(level);
            if (opened_keys.insert(key).second) opened.push_back({id, level});
            return hnsw.neighbors(id, level);
        };
        auto score = [&](int id) { return sq.score2(queries[qi], id, bits); };

        int entry = hnsw.index->hnsw.entry_point;
        reveal(entry);
        float entry_score = score(entry);
        for (int level = hnsw.index->hnsw.max_level; level > 0; --level) {
            while (true) {
                int next = entry;
                float next_score = entry_score;
                for (int neighbor : open(entry, level)) {
                    reveal(neighbor);
                    const float value = score(neighbor);
                    if (value < next_score ||
                        (value == next_score && neighbor < next)) {
                        next = neighbor;
                        next_score = value;
                    }
                }
                if (next == entry) break;
                entry = next;
                entry_score = next_score;
            }
        }

        using Item = std::pair<float, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> frontier;
        std::priority_queue<Item> best;
        std::vector<uint8_t> visited(hnsw.size());
        frontier.push({entry_score, entry});
        best.push({entry_score, entry});
        visited[entry] = 1;
        while (!frontier.empty()) {
            const Item current = frontier.top();
            frontier.pop();
            if (best.size() >= static_cast<size_t>(ef_search) &&
                best.top() < current) break;
            for (int neighbor : open(current.second, 0)) {
                if (visited[neighbor]) continue;
                visited[neighbor] = 1;
                reveal(neighbor);
                const Item value{score(neighbor), neighbor};
                if (best.size() < static_cast<size_t>(ef_search) ||
                    value < best.top()) {
                    frontier.push(value);
                    best.push(value);
                    if (best.size() > static_cast<size_t>(ef_search)) best.pop();
                }
            }
        }

        std::vector<Item> candidates;
        while (!best.empty()) {
            candidates.push_back(best.top());
            best.pop();
        }
        std::sort(candidates.begin(), candidates.end());
        if (candidates.size() > static_cast<size_t>(candidate_objects)) {
            candidates.resize(candidate_objects);
        }

        RBTrace& block_trace = batch.traces[qi];
        for (int id : disclosed_objects) block_trace.disclosed.push_back(owner[id]);
        for (const auto& item : opened) block_trace.expanded.push_back(owner[item.local_id]);
        for (const auto& item : candidates) block_trace.selected.push_back(owner[item.second]);
        auto normalize = [](std::vector<int>& ids) {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        };
        normalize(block_trace.disclosed);
        normalize(block_trace.expanded);
        normalize(block_trace.selected);
        std::sort(disclosed_objects.begin(), disclosed_objects.end());
        std::sort(opened.begin(), opened.end(), [](const auto& a, const auto& b) {
            return a.local_id < b.local_id ||
                (a.local_id == b.local_id && a.level < b.level);
        });

        std::vector<int> code_positions;
        code_positions.reserve(disclosed_objects.size());
        for (int id : disclosed_objects) code_positions.push_back(position[id]);
        std::vector<int> expanded_objects;
        for (const auto& item : opened) expanded_objects.push_back(item.local_id);
        normalize(expanded_objects);
        std::vector<int> adjacency_positions;
        adjacency_positions.reserve(expanded_objects.size());
        for (int id : expanded_objects) adjacency_positions.push_back(position[id]);

        batch.disclosed_objects += disclosed_objects.size();
        batch.expanded_objects += expanded_objects.size();
        batch.payload_bytes += 24.0 +
            disclosed_objects.size() *
                (sizeof(uint32_t) + (hnsw.dimension() * bits + 7) / 8.0);
        batch.auth_bytes +=
            proof_count(hnsw.size(), code_positions) * sizeof(Hash);
        for (int id : expanded_objects) {
            batch.payload_bytes += 2.0 * sizeof(uint32_t);
            const int levels = hnsw.index->hnsw.levels[id];
            for (int level = 0; level < levels; ++level) {
                batch.payload_bytes += sizeof(uint32_t) + sizeof(uint8_t);
                const vann::OpenedAdjacency needle{id, level};
                const bool is_open = std::binary_search(
                    opened.begin(), opened.end(), needle,
                    [](const auto& a, const auto& b) {
                        return a.local_id < b.local_id ||
                            (a.local_id == b.local_id && a.level < b.level);
                    });
                if (is_open) {
                    batch.payload_bytes +=
                        hnsw.neighbors(id, level).size() * sizeof(uint32_t);
                } else {
                    batch.auth_bytes += sizeof(Hash);
                }
            }
        }
        batch.auth_bytes +=
            proof_count(hnsw.size(), adjacency_positions) * sizeof(Hash);
    }
    return batch;
}

static std::vector<RBTrace> hb_member_block_traces(
    const RBNestedSQ& sq, const std::vector<std::vector<int>>& blocks,
    const std::vector<std::vector<int>>& graph, const std::vector<int>& entries,
    const Matrix& queries, int budget, int probes) {
    std::vector<RBTrace> traces(queries.rows());
    for (size_t qi = 0; qi < queries.rows(); ++qi) {
        RBTrace& trace = traces[qi];
        std::vector<uint8_t> seen(graph.size()), done(graph.size());
        std::vector<float> scores(graph.size());
        std::priority_queue<RBItem, std::vector<RBItem>, RBGreater> frontier;
        std::vector<RBItem> candidates;
        auto discover = [&](int block) {
            if (seen[block]) return;
            seen[block] = 1;
            float score = std::numeric_limits<float>::infinity();
            for (int id : blocks[block]) {
                score = std::min(score, sq.score2(queries[qi], id, 4));
            }
            scores[block] = score;
            frontier.push({score, block});
            candidates.push_back({score, block});
            trace.disclosed.push_back(block);
        };
        for (int entry : entries) discover(entry);
        while (static_cast<int>(trace.expanded.size()) < budget &&
               !frontier.empty()) {
            const RBItem current = frontier.top();
            frontier.pop();
            if (done[current.id]) continue;
            done[current.id] = 1;
            trace.expanded.push_back(current.id);
            for (int neighbor : graph[current.id]) discover(neighbor);
        }
        if (static_cast<int>(candidates.size()) < probes) {
            throw std::runtime_error(
                "member-aware routing disclosed fewer blocks than topBlocks");
        }
        std::partial_sort(
            candidates.begin(), candidates.begin() + probes, candidates.end(),
            [](const RBItem& a, const RBItem& b) {
                return a.score < b.score ||
                    (a.score == b.score && a.id < b.id);
            });
        for (int i = 0; i < probes; ++i) {
            trace.selected.push_back(candidates[i].id);
        }
        std::sort(trace.disclosed.begin(), trace.disclosed.end());
        std::sort(trace.expanded.begin(), trace.expanded.end());
        std::sort(trace.selected.begin(), trace.selected.end());
    }
    return traces;
}
static std::vector<RBTrace> hb_hybrid_traces(
    const vann::HnswIndex& hnsw, const RBNestedSQ& sq,
    const std::vector<std::vector<int>>& blocks, const RBSummary& summary,
    const std::vector<std::vector<int>>& graph, const Matrix& queries,
    int budget, int probes) {
    std::vector<int> owner(hnsw.size(), -1);
    for (size_t block = 0; block < blocks.size(); ++block) {
        for (int id : blocks[block]) owner[id] = static_cast<int>(block);
    }
    std::vector<RBTrace> traces(queries.rows());
    for (size_t qi = 0; qi < queries.rows(); ++qi) {
        RBTrace& trace = traces[qi];
        std::vector<uint8_t> disclosed(graph.size()), expanded(graph.size());
        std::vector<float> scores(graph.size());
        auto reveal_block = [&](int block) {
            if (!disclosed[block]) {
                disclosed[block] = 1;
                scores[block] = summary.score(queries[qi], block, false);
                trace.disclosed.push_back(block);
            }
        };
        auto open_block = [&](int block) {
            if (!expanded[block]) {
                expanded[block] = 1;
                trace.expanded.push_back(block);
            }
        };

        int portal = hnsw.index->hnsw.entry_point;
        reveal_block(owner[portal]);
        float portal_score = sq.score2(queries[qi], portal, 4);
        for (int level = hnsw.index->hnsw.max_level; level > 0; --level) {
            while (true) {
                open_block(owner[portal]);
                int next = portal;
                float next_score = portal_score;
                for (int neighbor : hnsw.neighbors(portal, level)) {
                    reveal_block(owner[neighbor]);
                    const float value = sq.score2(queries[qi], neighbor, 4);
                    if (value < next_score ||
                        (value == next_score && neighbor < next)) {
                        next = neighbor;
                        next_score = value;
                    }
                }
                if (next == portal) break;
                portal = next;
                portal_score = next_score;
            }
        }

        std::vector<uint8_t> seen(graph.size()), done(graph.size());
        std::priority_queue<RBItem, std::vector<RBItem>, RBGreater> frontier;
        auto discover = [&](int block) {
            reveal_block(block);
            if (!seen[block]) {
                seen[block] = 1;
                frontier.push({scores[block], block});
            }
        };
        discover(owner[portal]);
        int base_expanded = 0;
        while (base_expanded < budget && !frontier.empty()) {
            const RBItem current = frontier.top();
            frontier.pop();
            if (done[current.id]) continue;
            done[current.id] = 1;
            ++base_expanded;
            open_block(current.id);
            for (int neighbor : graph[current.id]) discover(neighbor);
        }
        if (static_cast<int>(trace.disclosed.size()) < probes) {
            throw std::runtime_error("hybrid routing disclosed fewer blocks than topBlocks");
        }
        std::vector<RBItem> candidates;
        candidates.reserve(trace.disclosed.size());
        for (int block : trace.disclosed) {
            candidates.push_back({scores[block], block});
        }
        std::partial_sort(
            candidates.begin(), candidates.begin() + probes, candidates.end(),
            [](const RBItem& a, const RBItem& b) {
                return a.score < b.score ||
                    (a.score == b.score && a.id < b.id);
            });
        for (int i = 0; i < probes; ++i) {
            trace.selected.push_back(candidates[i].id);
        }
        std::sort(trace.disclosed.begin(), trace.disclosed.end());
        std::sort(trace.expanded.begin(), trace.expanded.end());
        std::sort(trace.selected.begin(), trace.selected.end());
    }
    return traces;
}
int main(int argc, char** argv) try {
    const HBConfig config = hb_args(argc, argv);
    std::cerr << "Loading SIFT and exact ground truth...\n";
    const Matrix data = fvecs(config.data + "/sift_base.fvecs", config.base);
    const Matrix queries = fvecs(config.data + "/sift_query.fvecs", config.queries);
    const auto ground_truth = exact_gt(data, queries, config.k);

    vann::Matrix hnsw_data;
    hnsw_data.dimension = data.d;
    hnsw_data.values = data.x;
    std::cerr << "Loading/building original HNSW...\n";
    const vann::HnswIndex hnsw = vann::build_hnsw(
        hnsw_data, config.M, config.ef_construction, config.cache);
    std::cerr << "Aggregating HNSW nodes into blocks of "
              << config.block_size << "...\n";
    const auto blocks = hb_blocks(
        data, hnsw, config.block_size, config.block_order);
    const auto block_centers = centers(data, blocks);
    auto routing_centers = block_centers;
    std::vector<int> routing_portals(blocks.size(), -1);
    if (config.portal_residual || config.summary_mode == "representative") {
        for (size_t block = 0; block < blocks.size(); ++block) {
            int best_level = -1;
            float best_center_distance = std::numeric_limits<float>::infinity();
            for (int id : blocks[block]) {
                const int level = hnsw.index->hnsw.levels[id];
                const float center_distance =
                    l2(block_centers[block].data(), data[id], data.d);
                if (level > best_level ||
                    (level == best_level &&
                     (center_distance < best_center_distance ||
                      (center_distance == best_center_distance &&
                       id < routing_portals[block])))) {
                    routing_portals[block] = id;
                    best_level = level;
                    best_center_distance = center_distance;
                }
            }
            routing_centers[block].assign(
                data[routing_portals[block]],
                data[routing_portals[block]] + data.d);
        }
    }
    const RBSummary summary(
        data, blocks, routing_centers, config.portal_bits);
    double full_average_degree = 0;
    size_t full_maximum_degree = 0;
    std::vector<std::vector<int>> graph;
    HBLayerGraph layers;
    std::vector<int> entries;
    if (config.graph_source == "portal-block") {
        std::vector<int> owner(hnsw.size(), -1);
        for (size_t block = 0; block < blocks.size(); ++block) {
            for (int id : blocks[block]) owner[id] = static_cast<int>(block);
        }
        int layer_count = 1;
        for (int portal : routing_portals) {
            layer_count = std::max(
                layer_count, hnsw.index->hnsw.levels[portal]);
        }
        layers.assign(layer_count, std::vector<std::vector<int>>(blocks.size()));
        for (size_t block = 0; block < blocks.size(); ++block) {
            const int portal = routing_portals[block];
            for (int level = 0; level < hnsw.index->hnsw.levels[portal]; ++level) {
                auto& adjacency = layers[level][block];
                for (int neighbor : hnsw.neighbors(portal, level)) {
                    const int target = owner[neighbor];
                    if (target != static_cast<int>(block)) {
                        adjacency.push_back(target);
                    }
                }
                std::sort(adjacency.begin(), adjacency.end());
                adjacency.erase(
                    std::unique(adjacency.begin(), adjacency.end()),
                    adjacency.end());
                const size_t cap = level == 0 ? config.degree :
                    std::max(2, config.degree / config.portal_divisor);
                if (config.degree > 0 && adjacency.size() > cap) {
                    std::partial_sort(
                        adjacency.begin(), adjacency.begin() + cap,
                        adjacency.end(), [&](int a, int b) {
                            const float da = l2(
                                routing_centers[block].data(),
                                routing_centers[a].data(), data.d);
                            const float db = l2(
                                routing_centers[block].data(),
                                routing_centers[b].data(), data.d);
                            return da < db || (da == db && a < b);
                        });
                    adjacency.resize(cap);
                }
            }
        }
        graph = hb_flatten_layers(layers);
        entries = {owner[hnsw.index->hnsw.entry_point]};
        for (const auto& adjacency : graph) {
            full_average_degree += adjacency.size();
            full_maximum_degree =
                std::max(full_maximum_degree, adjacency.size());
        }
        full_average_degree /= graph.size();
    } else if (config.graph_source == "native") {
        vann::Matrix summary_vectors;
        summary_vectors.dimension = data.d;
        summary_vectors.values.resize(blocks.size() * static_cast<size_t>(data.d));
        for (size_t block = 0; block < blocks.size(); ++block) {
            for (int j = 0; j < data.d; ++j) {
                summary_vectors.values[block * static_cast<size_t>(data.d) + j] =
                    summary.lo[j] +
                    summary.code[block * static_cast<size_t>(data.d) + j] *
                    summary.step[j];
            }
        }
        std::string block_cache = config.block_cache;
        if (block_cache.empty()) {
            block_cache = "hnsw_native_blocks_b" + std::to_string(config.block_size) +
                "_" + config.summary_mode + "_M" + std::to_string(config.block_M) +
                ".index";
        }
        std::cerr << "Loading/building native block-summary HNSW M="
                  << config.block_M << "...\n";
        const auto native = vann::build_hnsw(
            summary_vectors, config.block_M, config.ef_construction, block_cache);
        int layer_count = 1;
        for (int level_count : native.index->hnsw.levels) {
            layer_count = std::max(layer_count, level_count);
        }
        layers.assign(layer_count, std::vector<std::vector<int>>(blocks.size()));
        for (size_t block = 0; block < blocks.size(); ++block) {
            for (int level = 0; level < native.index->hnsw.levels[block]; ++level) {
                layers[level][block] = native.neighbors(block, level);
            }
        }
        graph = hb_flatten_layers(layers);
        entries = {native.index->hnsw.entry_point};
        for (const auto& adjacency : graph) {
            full_average_degree += adjacency.size();
            full_maximum_degree = std::max(full_maximum_degree, adjacency.size());
        }
        full_average_degree /= graph.size();
    } else {
        graph = hb_contract_graph(
            routing_centers, hnsw, blocks, config.degree, config.edge_order,
            config.portal_divisor, &full_average_degree, &full_maximum_degree);
        if (config.routing_mode == "multilevel") {
            layers = hb_contract_layers(routing_centers, hnsw, blocks, config.degree);
            graph = hb_flatten_layers(layers);
        }
        entries = hb_entry_blocks(hnsw, blocks, config.entry_count);
    }

    double average_degree = 0;
    size_t maximum_degree = 0;
    for (const auto& adjacency : graph) {
        average_degree += adjacency.size();
        maximum_degree = std::max(maximum_degree, adjacency.size());
    }
    average_degree /= graph.size();
    std::cerr << "Blocks=" << blocks.size() << " full degree avg/max="
              << full_average_degree << '/' << full_maximum_degree
              << " bounded degree avg/max=" << average_degree << '/'
              << maximum_degree << '\n';

    Matrix codec_data = data;
    if (config.portal_residual) {
        for (size_t block = 0; block < blocks.size(); ++block) {
            for (int id : blocks[block]) {
                for (int j = 0; j < data.d; ++j) {
                    codec_data.x[static_cast<size_t>(id) * data.d + j] =
                        data[id][j] - summary.decoded(block, j);
                }
            }
        }
    }
    const RBNestedSQ object_sq(codec_data);
    std::cerr << "Building block-aligned SHA-256 commitments...\n";
    std::vector<Hash> local_roots(blocks.size());
    for (size_t block = 0; block < blocks.size(); ++block) {
        std::vector<Hash> leaves;
        for (int id : blocks[block]) {
            leaves.push_back(rb_object_leaf(codec_data, object_sq, id));
        }
        local_roots[block] = merkle(std::move(leaves));
    }
    std::vector<Hash> block_leaves(blocks.size());
    for (size_t block = 0; block < blocks.size(); ++block) {
        block_leaves[block] = rb_region_leaf(
            block, summary, blocks[block].size(), local_roots[block], graph[block]);
    }
    const Hash root = merkle(block_leaves);
    auto altered = block_leaves;
    altered.front().front() ^= 1;
    if (merkle(altered) == root) {
        throw std::runtime_error("contracted graph Merkle tamper self-test failed");
    }

    std::ofstream output(config.output);
    if (!output) throw std::runtime_error("cannot write " + config.output);
    output << std::setprecision(12)
           << "base,queries,k,M,ef_construction,merge_size,blocks,degree_cap,"
              "full_avg_block_degree,full_max_block_degree,avg_block_degree,"
              "max_block_degree,expansion_budget,top_blocks,recall,"
              "avg_disclosed_blocks,avg_expanded_blocks,avg_visited_vectors,"
              "avg_refined_vectors,avg_exact_vectors,block_metadata_bytes,"
              "merged_edge_bytes,hidden_adjacency_hash_bytes,"
              "hidden_local_root_bytes,block_merkle_bytes,object_base_bytes,"
              "object_refinement_bytes,exact_vector_bytes,routing_vo_bytes,"
              "vector_vo_bytes,total_vo_bytes,reduction_vs_merkle_hnsw,"
              "query_ms,bound_violations,"
              "nested_block_auth_bytes,nested_continuation_hash_bytes,"
              "nested_total_auth_bytes,separated_summary_proof_bytes,"
              "separated_adjacency_proof_bytes,separated_sq4_proof_bytes,"
              "separated_sq6_proof_bytes,separated_raw_proof_bytes,"
              "separated_total_auth_bytes,separated_summary_payload_bytes,"
              "separated_adjacency_payload_bytes,separated_sq4_payload_bytes,"
              "separated_sq6_payload_bytes,separated_raw_payload_bytes,"
              "separated_total_payload_bytes,separated_total_vo_bytes,"
              "separated_reduction_vs_nested,separated_sq4_objects,"
              "separated_sq6_objects,separated_raw_objects,graph_source,summary_mode,"
              "block_order,portal_bits,portal_residual,portal_divisor,"
              "portal_disclosed_objects,portal_expanded_objects,"
              "portal_payload_bytes,portal_auth_bytes\n";

    for (int expansion : config.expansions) {
        for (int probes : config.probes) {
            std::cerr << "Running merge=" << config.block_size
                      << " expand=" << expansion << " top-blocks=" << probes << "...\n";
            RBMeasure value;
            std::vector<RBTrace> supplied_traces;
            HBPortalBatch portal;
            const bool portal_mode =
                config.graph_source == "portal-sq4" ||
                config.graph_source == "portal-sq6";
            const bool hybrid_mode = config.graph_source == "hybrid-sq4";
            const bool member_mode = config.graph_source == "member-sq4";
            try {
                if (portal_mode) {
                    portal = hb_portal_traces(
                        hnsw, object_sq, blocks, queries, expansion, probes,
                        config.graph_source == "portal-sq4" ? 4 : 6);
                    supplied_traces = portal.traces;
                } else if (member_mode) {
                    supplied_traces = hb_member_block_traces(
                        object_sq, blocks, graph, entries, queries,
                        expansion, probes);
                } else if (hybrid_mode) {
                    supplied_traces = hb_hybrid_traces(
                        hnsw, object_sq, blocks, summary, graph, queries,
                        expansion, probes);
                } else if (config.routing_mode == "multilevel" ||
                           config.graph_source == "native") {
                    supplied_traces = hb_multilevel_traces(
                        layers, summary, entries, queries, expansion, probes);
                }
                value = rb_evaluate(
                    true, false, expansion, probes, data, queries, blocks, graph,
                    entries, summary, object_sq, ground_truth,
                    (portal_mode || hybrid_mode || member_mode ||
                     config.routing_mode == "multilevel" ||
                     config.graph_source == "native") ? &supplied_traces : nullptr,
                    config.portal_residual);
            } catch (const std::runtime_error& error) {
                if (std::string(error.what()).find("fewer") != std::string::npos) {
                    std::cerr << "  skipped: " << error.what() << '\n';
                    continue;
                }
                throw;
            }
            const double nq = queries.rows();
            double routing = 24 +
                (value.metadata + value.adjacency + value.hidden_adjacency +
                 value.hidden_blocks + value.proof) / nq;
            if (portal_mode) {
                routing = (portal.payload_bytes + portal.auth_bytes) / nq;
            }
            double vector =
                (value.object_base + value.object_refinement + value.exact_bytes) / nq;
            if (portal_mode) {
                vector = (value.separated_sq4_proof +
                          value.separated_sq6_proof +
                          value.separated_raw_proof +
                          value.separated_sq4_payload +
                          value.separated_sq6_payload +
                          value.separated_raw_payload) / nq;
            }
            const double total = routing + vector;
            const double recall = value.hits / (nq * config.k);
            const double nested_block_auth =
                (value.hidden_adjacency + value.hidden_blocks + value.proof) / nq;
            const double nested_continuation =
                (value.visited + value.refined) * 32.0 / nq;
            const double nested_total_auth = nested_block_auth + nested_continuation;
            const double separated_summary_proof =
                value.separated_summary_proof / nq;
            const double separated_adjacency_proof =
                value.separated_adjacency_proof / nq;
            const double separated_sq4_proof = value.separated_sq4_proof / nq;
            const double separated_sq6_proof = value.separated_sq6_proof / nq;
            const double separated_raw_proof = value.separated_raw_proof / nq;
            const double separated_auth = separated_summary_proof +
                separated_adjacency_proof + separated_sq4_proof +
                separated_sq6_proof + separated_raw_proof;
            const double separated_summary_payload = value.metadata / nq;
            const double separated_adjacency_payload = value.adjacency / nq;
            const double separated_sq4_payload = value.separated_sq4_payload / nq;
            const double separated_sq6_payload = value.separated_sq6_payload / nq;
            const double separated_raw_payload = value.separated_raw_payload / nq;
            const double separated_payload = separated_summary_payload +
                separated_adjacency_payload + separated_sq4_payload +
                separated_sq6_payload + separated_raw_payload;
            const double separated_total = 24 + separated_auth + separated_payload;
            constexpr double hnsw_baseline = 378653.95;
            output << config.base << ',' << config.queries << ',' << config.k << ','
                   << config.M << ',' << config.ef_construction << ','
                   << config.block_size << ',' << blocks.size() << ','
                   << config.degree << ',' << full_average_degree << ','
                   << full_maximum_degree << ',' << average_degree << ','
                   << maximum_degree << ','
                   << expansion << ',' << probes << ',' << recall << ','
                   << value.disclosed / nq << ',' << value.expanded / nq << ','
                   << value.visited / nq << ',' << value.refined / nq << ','
                   << value.exact / nq << ',' << value.metadata / nq << ','
                   << value.adjacency / nq << ',' << value.hidden_adjacency / nq << ','
                   << value.hidden_blocks / nq << ',' << value.proof / nq << ','
                   << value.object_base / nq << ',' << value.object_refinement / nq << ','
                   << value.exact_bytes / nq << ',' << routing << ',' << vector << ','
                   << total << ',' << 1 - total / hnsw_baseline << ','
                   << value.milliseconds / nq << ',' << value.violations << ','
                   << nested_block_auth << ',' << nested_continuation << ','
                   << nested_total_auth << ',' << separated_summary_proof << ','
                   << separated_adjacency_proof << ',' << separated_sq4_proof << ','
                   << separated_sq6_proof << ',' << separated_raw_proof << ','
                   << separated_auth << ',' << separated_summary_payload << ','
                   << separated_adjacency_payload << ',' << separated_sq4_payload << ','
                   << separated_sq6_payload << ',' << separated_raw_payload << ','
                   << separated_payload << ',' << separated_total << ','
                   << 1 - separated_total / total << ','
                   << value.separated_sq4_objects / nq << ','
                   << value.separated_sq6_objects / nq << ','
                   << value.separated_raw_objects / nq << ','
                   << config.graph_source << ',' << config.summary_mode << ','
                   << config.block_order << ',' << config.portal_bits << ','
                   << config.portal_residual << ',' << config.portal_divisor << ','
                   << portal.disclosed_objects / nq << ','
                   << portal.expanded_objects / nq << ','
                   << portal.payload_bytes / nq << ','
                   << portal.auth_bytes / nq << '\n';
            std::cout << "merge=" << config.block_size << " e=" << expansion
                      << " blocks=" << probes << " recall=" << std::fixed
                      << std::setprecision(4) << recall << " disclosed="
                      << std::setprecision(1) << value.disclosed / nq
                      << " expanded=" << value.expanded / nq
                      << " candidate-vectors=" << value.visited / nq
                      << " raw=" << value.exact / nq
                      << " route=" << routing / 1024 << " KiB"
                      << " vector=" << vector / 1024 << " KiB"
                      << " total=" << total / 1024 << " KiB"
                      << " separated-auth=" << separated_auth / 1024 << " KiB"
                      << " separated-total=" << separated_total / 1024 << " KiB"
                      << " vs-HNSW=" << std::setprecision(2)
                      << (1 - total / hnsw_baseline) * 100 << "%"
                      << " violations=" << value.violations << '\n';
        }
    }
    std::cerr << "Wrote " << config.output << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
}

// Public, query-independent portals chosen from the original HNSW hierarchy.
// Higher-level blocks are preferred because they are the long-range routing
// representatives that HNSW itself uses before entering level zero.
static std::vector<int> hb_entry_blocks(const vann::HnswIndex& hnsw,
                                        const std::vector<std::vector<int>>& blocks,
                                        int count) {
    std::vector<std::pair<int, int>> ranked;
    ranked.reserve(blocks.size());
    int original_entry_block = -1;
    for (size_t block = 0; block < blocks.size(); ++block) {
        int maximum_level = 0;
        for (int id : blocks[block]) {
            maximum_level = std::max(maximum_level, hnsw.index->hnsw.levels[id]);
            if (id == hnsw.index->hnsw.entry_point) original_entry_block = block;
        }
        ranked.push_back({-maximum_level, static_cast<int>(block)});
    }
    std::sort(ranked.begin(), ranked.end());
    std::vector<int> result;
    result.push_back(original_entry_block);
    for (const auto& item : ranked) {
        if (static_cast<int>(result.size()) == count) break;
        if (item.second != original_entry_block) result.push_back(item.second);
    }
    return result;
}

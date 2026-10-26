#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <openssl/sha.h>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "LearnedEarlyTer-20SIGMOD/IndexHNSW.h"
#include "LearnedEarlyTer-20SIGMOD/index_io.h"

namespace vann {

using Hash = std::array<std::uint8_t, SHA256_DIGEST_LENGTH>;

inline Hash sha256(const void* data, std::size_t bytes) {
    Hash out{};
    SHA256(static_cast<const unsigned char*>(data), bytes, out.data());
    return out;
}

inline Hash hash_pair(const Hash& left, const Hash& right) {
    std::array<std::uint8_t, 2 * SHA256_DIGEST_LENGTH> buffer{};
    std::memcpy(buffer.data(), left.data(), left.size());
    std::memcpy(buffer.data() + left.size(), right.data(), right.size());
    return sha256(buffer.data(), buffer.size());
}

template <typename T>
inline void append_bytes(std::vector<std::uint8_t>& out, const T& value) {
    const auto* first = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), first, first + sizeof(T));
}

inline void append_hash(std::vector<std::uint8_t>& out, const Hash& value) {
    out.insert(out.end(), value.begin(), value.end());
}

struct Matrix {
    int dimension = 0;
    std::vector<float> values;

    std::size_t rows() const {
        return dimension == 0 ? 0 : values.size() / static_cast<std::size_t>(dimension);
    }
    const float* row(std::size_t i) const {
        return values.data() + i * static_cast<std::size_t>(dimension);
    }
};

inline Matrix read_fvecs(const std::string& path, std::size_t limit = 0) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path);
    Matrix matrix;
    while (limit == 0 || matrix.rows() < limit) {
        std::int32_t dimension = 0;
        if (!input.read(reinterpret_cast<char*>(&dimension), sizeof(dimension))) break;
        if (dimension <= 0 || (matrix.dimension != 0 && matrix.dimension != dimension)) {
            throw std::runtime_error("invalid fvecs dimension in " + path);
        }
        matrix.dimension = dimension;
        const std::size_t old_size = matrix.values.size();
        matrix.values.resize(old_size + static_cast<std::size_t>(dimension));
        if (!input.read(reinterpret_cast<char*>(matrix.values.data() + old_size),
                        sizeof(float) * static_cast<std::size_t>(dimension))) {
            throw std::runtime_error("truncated fvecs file " + path);
        }
    }
    if (matrix.rows() == 0) throw std::runtime_error("empty fvecs file " + path);
    return matrix;
}

inline std::vector<std::vector<int>> read_ivecs(const std::string& path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path);
    std::vector<std::vector<int>> result;
    while (result.size() < limit) {
        std::int32_t count = 0;
        if (!input.read(reinterpret_cast<char*>(&count), sizeof(count))) break;
        if (count <= 0) throw std::runtime_error("invalid ivecs row in " + path);
        result.emplace_back(static_cast<std::size_t>(count));
        if (!input.read(reinterpret_cast<char*>(result.back().data()),
                        sizeof(int) * static_cast<std::size_t>(count))) {
            throw std::runtime_error("truncated ivecs file " + path);
        }
    }
    if (result.size() != limit) throw std::runtime_error("too few ivecs rows in " + path);
    return result;
}

inline float squared_l2(const float* a, const float* b, int dimension) {
    float distance = 0.0F;
    for (int j = 0; j < dimension; ++j) {
        const float delta = a[j] - b[j];
        distance += delta * delta;
    }
    return distance;
}

struct ProofNode {
    std::uint32_t level = 0;
    std::uint64_t position = 0;
    Hash hash{};
};

struct MerkleProof {
    std::size_t leaf_count = 0;
    std::vector<ProofNode> nodes;
    std::size_t bytes() const { return nodes.size() * sizeof(Hash); }
};

class MerkleTree {
public:
    explicit MerkleTree(std::vector<Hash> leaves) : leaf_count_(leaves.size()) {
        if (leaves.empty()) throw std::runtime_error("Merkle tree requires at least one leaf");
        std::size_t padded = 1;
        while (padded < leaves.size()) padded <<= 1U;
        leaves.resize(padded, leaves.back());
        levels_.push_back(std::move(leaves));
        while (levels_.back().size() > 1) {
            const auto& child = levels_.back();
            std::vector<Hash> parent(child.size() / 2);
            for (std::size_t i = 0; i < parent.size(); ++i) {
                parent[i] = hash_pair(child[2 * i], child[2 * i + 1]);
            }
            levels_.push_back(std::move(parent));
        }
    }

    const Hash& root() const { return levels_.back().front(); }
    const Hash& leaf(std::size_t position) const { return levels_.front().at(position); }
    std::size_t leaf_count() const { return leaf_count_; }

    MerkleProof prove(std::vector<int> positions) const {
        normalize_positions(positions, leaf_count_);
        MerkleProof proof;
        proof.leaf_count = leaf_count_;
        std::vector<std::uint8_t> selected(levels_.front().size(), 0);
        for (int position : positions) selected[static_cast<std::size_t>(position)] = 1;
        for (std::size_t level = 0; level + 1 < levels_.size(); ++level) {
            std::vector<std::uint8_t> parent(selected.size() / 2, 0);
            for (std::size_t i = 0; i < selected.size(); i += 2) {
                if (selected[i] != selected[i + 1]) {
                    const std::size_t sibling = selected[i] ? i + 1 : i;
                    proof.nodes.push_back({static_cast<std::uint32_t>(level), sibling,
                                           levels_[level][sibling]});
                }
                parent[i / 2] = static_cast<std::uint8_t>(selected[i] || selected[i + 1]);
            }
            selected.swap(parent);
        }
        return proof;
    }

    bool verify(const std::vector<int>& input_positions, const std::vector<Hash>& input_leaves,
                const MerkleProof& proof) const {
        if (input_positions.size() != input_leaves.size() || proof.leaf_count != leaf_count_) {
            return false;
        }
        std::vector<std::pair<int, Hash>> selected;
        selected.reserve(input_positions.size());
        for (std::size_t i = 0; i < input_positions.size(); ++i) {
            selected.push_back({input_positions[i], input_leaves[i]});
        }
        std::sort(selected.begin(), selected.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t i = 0; i < selected.size(); ++i) {
            if (selected[i].first < 0 || static_cast<std::size_t>(selected[i].first) >= leaf_count_ ||
                (i != 0 && selected[i - 1].first == selected[i].first)) return false;
        }

        std::unordered_map<std::uint64_t, Hash> known;
        auto key = [](std::uint32_t level, std::uint64_t position) {
            return (static_cast<std::uint64_t>(level) << 56U) ^ position;
        };
        for (const auto& item : selected) known[key(0, static_cast<std::uint64_t>(item.first))] = item.second;
        for (const auto& node : proof.nodes) {
            const auto inserted = known.emplace(key(node.level, node.position), node.hash);
            if (!inserted.second) return false;
        }

        std::size_t width = levels_.front().size();
        for (std::uint32_t level = 0; width > 1; ++level, width >>= 1U) {
            std::unordered_set<std::uint64_t> parents;
            for (std::size_t position = 0; position < width; ++position) {
                if (known.find(key(level, position)) != known.end()) parents.insert(position / 2);
            }
            for (std::uint64_t parent : parents) {
                const auto left = known.find(key(level, 2 * parent));
                const auto right = known.find(key(level, 2 * parent + 1));
                if (left != known.end() && right != known.end()) {
                    known[key(level + 1, parent)] = hash_pair(left->second, right->second);
                }
            }
        }
        const auto rebuilt = known.find(key(static_cast<std::uint32_t>(levels_.size() - 1), 0));
        return rebuilt != known.end() && rebuilt->second == root();
    }

private:
    static void normalize_positions(std::vector<int>& positions, std::size_t leaf_count) {
        std::sort(positions.begin(), positions.end());
        positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
        if (positions.empty()) throw std::runtime_error("cannot prove an empty leaf set");
        if (positions.front() < 0 || static_cast<std::size_t>(positions.back()) >= leaf_count) {
            throw std::runtime_error("Merkle proof position is outside the tree");
        }
    }

    std::size_t leaf_count_ = 0;
    std::vector<std::vector<Hash>> levels_;
};

inline Hash object_leaf(int id, const float* vector, int dimension) {
    std::vector<std::uint8_t> bytes;
    const std::string domain = "vann/object/v1";
    bytes.insert(bytes.end(), domain.begin(), domain.end());
    append_bytes(bytes, id);
    const Hash vector_hash = sha256(vector, sizeof(float) * static_cast<std::size_t>(dimension));
    append_hash(bytes, vector_hash);
    return sha256(bytes.data(), bytes.size());
}

struct IvfData {
    std::vector<std::vector<int>> lists;
    Matrix centroids;
};

inline IvfData load_ivf_assignments(const std::string& path, const Matrix& base,
                                    int requested_lists = 0) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open " + path);
    std::vector<std::pair<int, int>> assignments;
    assignments.reserve(base.rows());
    long long id = -1;
    int list = -1;
    int maximum_list = -1;
    while (input >> id >> list) {
        if (id >= 0 && static_cast<std::size_t>(id) < base.rows()) {
            assignments.push_back({static_cast<int>(id), list});
            maximum_list = std::max(maximum_list, list);
        }
    }
    const int list_count = requested_lists > 0 ? requested_lists : maximum_list + 1;
    if (list_count <= 0 || maximum_list >= list_count) throw std::runtime_error("bad IVF list count");
    IvfData ivf;
    ivf.lists.resize(static_cast<std::size_t>(list_count));
    std::vector<std::uint8_t> seen(base.rows(), 0);
    for (const auto& assignment : assignments) {
        if (assignment.second < 0 || assignment.second >= list_count || seen[assignment.first]) {
            throw std::runtime_error("invalid or duplicate IVF assignment");
        }
        seen[assignment.first] = 1;
        ivf.lists[assignment.second].push_back(assignment.first);
    }
    if (assignments.size() != base.rows()) {
        throw std::runtime_error("IVF assignments do not cover the selected base vectors");
    }
    for (auto& values : ivf.lists) std::sort(values.begin(), values.end());

    ivf.centroids.dimension = base.dimension;
    ivf.centroids.values.assign(static_cast<std::size_t>(list_count) * base.dimension, 0.0F);
    for (int list_id = 0; list_id < list_count; ++list_id) {
        float* centroid = ivf.centroids.values.data() + static_cast<std::size_t>(list_id) * base.dimension;
        for (int object_id : ivf.lists[list_id]) {
            const float* vector = base.row(static_cast<std::size_t>(object_id));
            for (int j = 0; j < base.dimension; ++j) centroid[j] += vector[j];
        }
        if (!ivf.lists[list_id].empty()) {
            const float scale = 1.0F / static_cast<float>(ivf.lists[list_id].size());
            for (int j = 0; j < base.dimension; ++j) centroid[j] *= scale;
        }
    }
    return ivf;
}

inline std::vector<int> nearest_lists(const IvfData& ivf, const float* query, int nprobe) {
    if (nprobe < 1 || nprobe > static_cast<int>(ivf.lists.size())) {
        throw std::runtime_error("nprobe is outside the IVF list count");
    }
    std::vector<int> order(ivf.lists.size());
    std::iota(order.begin(), order.end(), 0);
    std::partial_sort(order.begin(), order.begin() + nprobe, order.end(), [&](int a, int b) {
        const float da = squared_l2(query, ivf.centroids.row(a), ivf.centroids.dimension);
        const float db = squared_l2(query, ivf.centroids.row(b), ivf.centroids.dimension);
        return da < db || (da == db && a < b);
    });
    order.resize(static_cast<std::size_t>(nprobe));
    return order;
}

inline Hash centroid_leaf(const IvfData& ivf, int list_id) {
    std::vector<std::uint8_t> bytes;
    const std::string domain = "vann/centroid/v1";
    bytes.insert(bytes.end(), domain.begin(), domain.end());
    append_bytes(bytes, list_id);
    const std::uint64_t count = ivf.lists.at(static_cast<std::size_t>(list_id)).size();
    append_bytes(bytes, count);
    const Hash vector_hash = sha256(ivf.centroids.row(list_id),
                                    sizeof(float) * static_cast<std::size_t>(ivf.centroids.dimension));
    append_hash(bytes, vector_hash);
    return sha256(bytes.data(), bytes.size());
}

struct HnswIndex {
    std::unique_ptr<faiss::IndexHNSWFlat> index;
    std::vector<int> local_to_global;

    int dimension() const { return index->d; }
    std::size_t size() const { return static_cast<std::size_t>(index->ntotal); }
    const float* vector(int local_id) const {
        const auto* flat = dynamic_cast<const faiss::IndexFlat*>(index->storage);
        if (flat == nullptr) throw std::runtime_error("HNSW storage is not IndexFlat");
        return flat->xb.data() + static_cast<std::size_t>(local_id) * index->d;
    }
    int global_id(int local_id) const {
        return local_to_global.empty() ? local_id : local_to_global.at(static_cast<std::size_t>(local_id));
    }
    int local_id_from_global(int global_id_value) const {
        if (local_to_global.empty()) return global_id_value;
        const auto found = std::lower_bound(local_to_global.begin(), local_to_global.end(), global_id_value);
        if (found == local_to_global.end() || *found != global_id_value) return -1;
        return static_cast<int>(found - local_to_global.begin());
    }
    std::vector<int> neighbors(int local_id, int level) const {
        std::size_t begin = 0;
        std::size_t end = 0;
        index->hnsw.neighbor_range(local_id, level, &begin, &end);
        std::vector<int> result;
        for (std::size_t p = begin; p < end; ++p) {
            const int neighbor = index->hnsw.neighbors[p];
            if (neighbor >= 0) result.push_back(neighbor);
        }
        return result;
    }
};

inline std::unique_ptr<faiss::IndexHNSWFlat> load_hnsw(const std::string& cache_path,
                                                       int expected_dimension,
                                                       std::size_t expected_size) {
    if (cache_path.empty() || !std::filesystem::exists(cache_path)) return nullptr;
    std::unique_ptr<faiss::Index> loaded(faiss::read_index(cache_path.c_str()));
    auto* hnsw = dynamic_cast<faiss::IndexHNSWFlat*>(loaded.release());
    if (hnsw == nullptr || hnsw->d != expected_dimension ||
        static_cast<std::size_t>(hnsw->ntotal) != expected_size) {
        delete hnsw;
        throw std::runtime_error("cached HNSW shape mismatch: " + cache_path);
    }
    return std::unique_ptr<faiss::IndexHNSWFlat>(hnsw);
}

inline HnswIndex build_hnsw(const Matrix& vectors, int M, int ef_construction,
                            const std::string& cache_path = {},
                            std::vector<int> local_to_global = {}) {
    HnswIndex result;
    result.index = load_hnsw(cache_path, vectors.dimension, vectors.rows());
    if (!result.index) {
        result.index = std::make_unique<faiss::IndexHNSWFlat>(vectors.dimension, M);
        // The vendored research Faiss fork leaves this field uninitialized.
        result.index->hnsw.lselect = faiss::Random;
        result.index->hnsw.priors = nullptr;
        result.index->hnsw.efConstruction = ef_construction;
        result.index->add(static_cast<faiss::Index::idx_t>(vectors.rows()), vectors.values.data());
        if (!cache_path.empty()) {
            const std::filesystem::path cache(cache_path);
            if (!cache.parent_path().empty()) std::filesystem::create_directories(cache.parent_path());
            faiss::write_index(result.index.get(), cache_path.c_str());
        }
    }
    if (!local_to_global.empty() && local_to_global.size() != vectors.rows()) {
        throw std::runtime_error("local/global HNSW mapping size mismatch");
    }
    result.local_to_global = std::move(local_to_global);
    return result;
}

struct DistanceId {
    float distance = 0.0F;
    int id = -1;
};

inline bool better(const DistanceId& a, const DistanceId& b) {
    return a.distance < b.distance || (a.distance == b.distance && a.id < b.id);
}

struct MinDistance {
    bool operator()(const DistanceId& a, const DistanceId& b) const { return better(b, a); }
};

struct MaxDistance {
    bool operator()(const DistanceId& a, const DistanceId& b) const { return better(a, b); }
};

struct OpenedAdjacency {
    int local_id = -1;
    int level = -1;
};

struct HnswTrace {
    std::vector<int> disclosed;
    std::vector<OpenedAdjacency> opened;
    std::vector<int> answer;
};

inline HnswTrace search_hnsw(const HnswIndex& hnsw, const float* query, int ef_search, int k,
                             const HnswTrace* supplied_vo = nullptr) {
    if (hnsw.size() == 0 || k < 1 || ef_search < k) throw std::runtime_error("bad HNSW query");
    HnswTrace trace;
    std::vector<std::uint8_t> disclosed(hnsw.size(), 0);
    std::unordered_set<std::uint64_t> opened;
    std::vector<std::uint8_t> allowed_disclosed;
    std::unordered_set<std::uint64_t> allowed_opened;
    if (supplied_vo != nullptr) {
        allowed_disclosed.assign(hnsw.size(), 0);
        for (int local_id : supplied_vo->disclosed) {
            if (local_id < 0 || static_cast<std::size_t>(local_id) >= hnsw.size())
                throw std::runtime_error("VO contains an invalid HNSW node ID");
            allowed_disclosed[local_id] = 1;
        }
        for (const auto& item : supplied_vo->opened) {
            const std::uint64_t supplied_key =
                (static_cast<std::uint64_t>(static_cast<std::uint32_t>(item.local_id)) << 32U) |
                static_cast<std::uint32_t>(item.level);
            allowed_opened.insert(supplied_key);
        }
    }
    auto reveal = [&](int local_id) {
        if (supplied_vo != nullptr && !allowed_disclosed.at(static_cast<std::size_t>(local_id)))
            throw std::runtime_error("VO omitted a vector required by HNSW replay");
        if (!disclosed.at(static_cast<std::size_t>(local_id))) {
            disclosed[local_id] = 1;
            trace.disclosed.push_back(local_id);
        }
    };
    auto open = [&](int local_id, int level) {
        const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(local_id)) << 32U) |
                                  static_cast<std::uint32_t>(level);
        if (supplied_vo != nullptr && allowed_opened.count(key) == 0)
            throw std::runtime_error("VO omitted an adjacency list required by HNSW replay");
        if (opened.insert(key).second) trace.opened.push_back({local_id, level});
        return hnsw.neighbors(local_id, level);
    };

    int entry = hnsw.index->hnsw.entry_point;
    reveal(entry);
    float entry_distance = squared_l2(query, hnsw.vector(entry), hnsw.dimension());
    for (int level = hnsw.index->hnsw.max_level; level > 0; --level) {
        while (true) {
            int next = entry;
            float next_distance = entry_distance;
            for (int neighbor : open(entry, level)) {
                reveal(neighbor);
                const float distance = squared_l2(query, hnsw.vector(neighbor), hnsw.dimension());
                if (distance < next_distance || (distance == next_distance && neighbor < next)) {
                    next = neighbor;
                    next_distance = distance;
                }
            }
            if (next == entry) break;
            entry = next;
            entry_distance = next_distance;
        }
    }

    std::priority_queue<DistanceId, std::vector<DistanceId>, MinDistance> candidates;
    std::priority_queue<DistanceId, std::vector<DistanceId>, MaxDistance> best;
    std::vector<std::uint8_t> visited(hnsw.size(), 0);
    candidates.push({entry_distance, entry});
    best.push({entry_distance, entry});
    visited[entry] = 1;
    while (!candidates.empty()) {
        const DistanceId current = candidates.top();
        candidates.pop();
        if (best.size() >= static_cast<std::size_t>(ef_search) && better(best.top(), current)) break;
        for (int neighbor : open(current.id, 0)) {
            if (visited[neighbor]) continue;
            visited[neighbor] = 1;
            reveal(neighbor);
            const DistanceId value{squared_l2(query, hnsw.vector(neighbor), hnsw.dimension()), neighbor};
            if (best.size() < static_cast<std::size_t>(ef_search) || better(value, best.top())) {
                candidates.push(value);
                best.push(value);
                if (best.size() > static_cast<std::size_t>(ef_search)) best.pop();
            }
        }
    }

    std::vector<DistanceId> ordered;
    while (!best.empty()) {
        ordered.push_back(best.top());
        best.pop();
    }
    std::sort(ordered.begin(), ordered.end(), better);
    if (ordered.size() > static_cast<std::size_t>(k)) ordered.resize(static_cast<std::size_t>(k));
    for (const auto& value : ordered) trace.answer.push_back(value.id);
    std::sort(trace.disclosed.begin(), trace.disclosed.end());
    std::sort(trace.opened.begin(), trace.opened.end(), [](const auto& a, const auto& b) {
        return a.local_id < b.local_id || (a.local_id == b.local_id && a.level < b.level);
    });
    return trace;
}

inline void verify_hnsw_route(const HnswIndex& hnsw, const float* query, int ef_search, int k,
                              const HnswTrace& supplied_vo) {
    const HnswTrace replay = search_hnsw(hnsw, query, ef_search, k, &supplied_vo);
    bool same_opened = replay.opened.size() == supplied_vo.opened.size();
    for (std::size_t i = 0; same_opened && i < replay.opened.size(); ++i)
        same_opened = replay.opened[i].local_id == supplied_vo.opened[i].local_id &&
                      replay.opened[i].level == supplied_vo.opened[i].level;
    if (replay.disclosed != supplied_vo.disclosed || !same_opened ||
        replay.answer != supplied_vo.answer)
        throw std::runtime_error("HNSW VO is not the canonical complete query trace");
}

inline void verify_hnsw_omission_rejected(const HnswIndex& hnsw, const float* query, int ef_search,
                                            int k, const HnswTrace& valid_vo) {
    if (valid_vo.disclosed.empty()) throw std::runtime_error("empty HNSW VO");
    HnswTrace omitted = valid_vo;
    omitted.disclosed.erase(omitted.disclosed.begin());
    bool rejected = false;
    try {
        verify_hnsw_route(hnsw, query, ef_search, k, omitted);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) throw std::runtime_error("HNSW omission self-check failed");
}

inline bool adjacency_is_open(const HnswTrace& trace, int local_id, int level) {
    return std::binary_search(trace.opened.begin(), trace.opened.end(), OpenedAdjacency{local_id, level},
                              [](const auto& a, const auto& b) {
                                  return a.local_id < b.local_id ||
                                         (a.local_id == b.local_id && a.level < b.level);
                              });
}

inline Hash hnsw_node_leaf(const HnswIndex& hnsw, int local_id) {
    std::vector<std::uint8_t> bytes;
    const std::string domain = "vann/hnsw-node/v1";
    bytes.insert(bytes.end(), domain.begin(), domain.end());
    const int global_id = hnsw.global_id(local_id);
    append_bytes(bytes, global_id);
    const int level_count = hnsw.index->hnsw.levels.at(static_cast<std::size_t>(local_id));
    append_bytes(bytes, level_count);
    const Hash vector_hash = sha256(hnsw.vector(local_id),
                                    sizeof(float) * static_cast<std::size_t>(hnsw.dimension()));
    append_hash(bytes, vector_hash);
    for (int level = 0; level < level_count; ++level) {
        const std::vector<int> local_neighbors = hnsw.neighbors(local_id, level);
        std::vector<int> global_neighbors;
        global_neighbors.reserve(local_neighbors.size());
        for (int neighbor : local_neighbors) global_neighbors.push_back(hnsw.global_id(neighbor));
        const std::uint32_t degree = static_cast<std::uint32_t>(global_neighbors.size());
        append_bytes(bytes, degree);
        const Hash adjacency_hash = sha256(global_neighbors.data(), global_neighbors.size() * sizeof(int));
        append_hash(bytes, adjacency_hash);
    }
    return sha256(bytes.data(), bytes.size());
}

inline Hash hnsw_header_root(const HnswIndex& hnsw, const Hash& node_root, int M,
                             int ef_construction) {
    std::vector<std::uint8_t> bytes;
    const std::string domain = "vann/hnsw-header/v1";
    bytes.insert(bytes.end(), domain.begin(), domain.end());
    append_hash(bytes, node_root);
    const int entry_global = hnsw.global_id(hnsw.index->hnsw.entry_point);
    append_bytes(bytes, entry_global);
    append_bytes(bytes, hnsw.index->hnsw.max_level);
    append_bytes(bytes, M);
    append_bytes(bytes, ef_construction);
    return sha256(bytes.data(), bytes.size());
}

struct VoSize {
    std::uint64_t route = 0;
    std::uint64_t distance = 0;
    std::uint64_t auth = 0;
    std::uint64_t visited = 0;
    std::uint64_t expanded_adjacencies = 0;

    std::uint64_t total() const { return route + distance + auth; }
    VoSize& operator+=(const VoSize& other) {
        route += other.route;
        distance += other.distance;
        auth += other.auth;
        visited += other.visited;
        expanded_adjacencies += other.expanded_adjacencies;
        return *this;
    }
};

inline VoSize hnsw_vo_size(const HnswIndex& hnsw, const HnswTrace& trace,
                           const MerkleProof& proof, bool include_local_position) {
    VoSize size;
    size.route = 4 * sizeof(std::uint32_t);  // entry, max level, M, efConstruction
    size.distance = trace.disclosed.size() * static_cast<std::size_t>(hnsw.dimension()) * sizeof(float);
    size.auth = proof.bytes();
    size.visited = trace.disclosed.size();
    size.expanded_adjacencies = trace.opened.size();
    for (int local_id : trace.disclosed) {
        const int level_count = hnsw.index->hnsw.levels.at(static_cast<std::size_t>(local_id));
        size.route += 2 * sizeof(std::uint32_t);  // global id and number of levels
        if (include_local_position) size.route += sizeof(std::uint32_t);
        for (int level = 0; level < level_count; ++level) {
            size.route += sizeof(std::uint32_t) + sizeof(std::uint8_t);  // degree and open flag
            if (adjacency_is_open(trace, local_id, level)) {
                size.route += hnsw.neighbors(local_id, level).size() * sizeof(std::uint32_t);
            } else {
                size.auth += sizeof(Hash);
            }
        }
    }
    return size;
}

inline std::vector<int> global_answers(const HnswIndex& hnsw, const HnswTrace& trace) {
    std::vector<int> result;
    result.reserve(trace.answer.size());
    for (int local_id : trace.answer) result.push_back(hnsw.global_id(local_id));
    return result;
}

inline std::uint64_t recall_hits(const std::vector<int>& result,
                                 const std::vector<int>& ground_truth, int k) {
    std::unordered_set<int> ids(result.begin(), result.end());
    std::uint64_t hits = 0;
    for (int i = 0; i < k && i < static_cast<int>(ground_truth.size()); ++i) {
        hits += ids.count(ground_truth[static_cast<std::size_t>(i)]);
    }
    return hits;
}

struct Cli {
    std::string dataset = "../dataset/sift";
    std::string assignments = "../populated_index/PriorMax_SIFT1M_C100_clusterid.tsv";
    std::string output = "results_sift1m.csv";
    std::string cache;
    int base = 1000000;
    int queries = 100;
    int k = 10;
    int nprobe = 10;
    int M = 16;
    int ef_construction = 100;
    int ef_search = 100;
};

inline Cli parse_cli(int argc, char** argv, bool uses_ivf, bool uses_hnsw) {
    Cli cli;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        auto value = [&]() {
            if (++i == argc) throw std::runtime_error("missing value after " + option);
            return std::string(argv[i]);
        };
        if (option == "--dataset") cli.dataset = value();
        else if (option == "--assignments") cli.assignments = value();
        else if (option == "--output") cli.output = value();
        else if (option == "--cache") cli.cache = value();
        else if (option == "--base") cli.base = std::stoi(value());
        else if (option == "--queries") cli.queries = std::stoi(value());
        else if (option == "--k") cli.k = std::stoi(value());
        else if (option == "--nprobe") cli.nprobe = std::stoi(value());
        else if (option == "--M") cli.M = std::stoi(value());
        else if (option == "--ef-construction") cli.ef_construction = std::stoi(value());
        else if (option == "--ef-search") cli.ef_search = std::stoi(value());
        else if (option == "--help") {
            std::cout << "Options: --dataset DIR --base N --queries N --k K --output FILE";
            if (uses_ivf) std::cout << " --assignments FILE --nprobe N";
            if (uses_hnsw) std::cout << " --M N --ef-construction N --ef-search N --cache PATH";
            std::cout << '\n';
            std::exit(0);
        } else {
            throw std::runtime_error("unknown option " + option);
        }
    }
    if (cli.base < cli.k || cli.queries < 1 || cli.k < 1 ||
        (uses_hnsw && cli.ef_search < cli.k)) throw std::runtime_error("invalid query parameters");
    return cli;
}

inline void write_result(const Cli& cli, const std::string& method, const VoSize& sum,
                         std::uint64_t hits, double elapsed_seconds) {
    const double queries = cli.queries;
    const double route = static_cast<double>(sum.route) / queries;
    const double distance = static_cast<double>(sum.distance) / queries;
    const double auth = static_cast<double>(sum.auth) / queries;
    const double total = route + distance + auth;
    std::ofstream output(cli.output);
    if (!output) throw std::runtime_error("cannot write " + cli.output);
    output << "method,base,queries,k,nprobe,M,ef_construction,ef_search,recall_at_k,"
              "vo_route_bytes,vo_distance_bytes,vo_auth_bytes,vo_total_bytes,"
              "vo_route_share,vo_distance_share,vo_auth_share,avg_visited,"
              "avg_expanded_adjacencies,elapsed_seconds\n";
    output << method << ',' << cli.base << ',' << cli.queries << ',' << cli.k << ','
           << cli.nprobe << ',' << cli.M << ',' << cli.ef_construction << ',' << cli.ef_search << ','
           << std::setprecision(10) << hits / (queries * cli.k) << ',' << route << ',' << distance << ','
           << auth << ',' << total << ',' << route / total << ',' << distance / total << ','
           << auth / total << ',' << sum.visited / queries << ','
           << sum.expanded_adjacencies / queries << ',' << elapsed_seconds << '\n';
    std::cout << method << " recall@" << cli.k << '=' << std::fixed << std::setprecision(4)
              << hits / (queries * cli.k) << " VO=" << std::setprecision(2) << total / 1024.0
              << " KiB [route " << route / 1024.0 << " KiB, distance " << distance / 1024.0
              << " KiB, auth " << auth / 1024.0 << " KiB]\n";
}

inline void verify_merkle_selection(const MerkleTree& tree, const std::vector<int>& positions,
                                    const MerkleProof& proof) {
    std::vector<Hash> leaves;
    leaves.reserve(positions.size());
    for (int position : positions) leaves.push_back(tree.leaf(static_cast<std::size_t>(position)));
    if (!tree.verify(positions, leaves, proof)) throw std::runtime_error("Merkle multiproof self-check failed");
    leaves.front()[0] ^= 1U;
    if (tree.verify(positions, leaves, proof)) throw std::runtime_error("Merkle tamper self-check failed");
}

}  // namespace vann

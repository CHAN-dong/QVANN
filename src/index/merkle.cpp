#include "index/merkle.h"
#include "utils/hash.h"

namespace qvann {
MerkleTree::MerkleTree(std::vector<Hash> leaves) : n(leaves.size()) {
    if (!n) {
        throw std::runtime_error("empty tree");
    }
    size_t p = 1;
    while (p < n) {
        p *= 2;
    }
    leaves.resize(p, leaves.back());
    levels.push_back(std::move(leaves));
    while (levels.back().size() > 1) {
        auto& c = levels.back();
        std::vector<Hash> next(c.size() / 2);
        for (size_t i = 0; i < next.size(); ++i) {
            next[i] = pair_hash(c[2 * i], c[2 * i + 1]);
        }
        levels.push_back(std::move(next));
    }
}

Hash MerkleTree::root() const {
    return levels.back()[0];
}

Proof MerkleTree::prove(std::set<uint32_t> targets, bool paths) const {
    if (paths) {
        Proof all;
        for (auto id : targets) {
            auto p = prove({id});
            all.insert(all.end(), p.begin(), p.end());
        }
        return all;
    }
    Proof p;
    for (uint32_t l = 0; l + 1 < levels.size(); ++l) {
        std::set<uint32_t> next;
        for (auto id : targets) {
            if (!targets.count(id ^ 1)) {
                p.push_back({l, id ^ 1, levels[l].at(id ^ 1)});
            }
            next.insert(id / 2);
        }
        targets = std::move(next);
    }
    return p;
}

bool MerkleTree::verify(Hash root, uint32_t n, std::map<uint32_t, Hash> targets, const Proof& p) {
    if (!n || targets.empty()) {
        return false;
    }
    size_t width = 1;
    while (width < n) {
        width *= 2;
    }
    for (auto [id, h] : targets) {
        if (id >= n) {
            return false;
        }
    }
    std::map<std::pair<uint32_t, uint32_t>, Hash> proof;
    uint32_t height = 0;
    for (size_t w = width; w > 1; w /= 2) {
        ++height;
    }
    for (auto node : p) {
        if (node.level >= height || node.position >= (width >> node.level)) {
            return false;
        }
        auto [it, ok] = proof.emplace(std::make_pair(node.level, node.position), node.value);
        if (!ok && it->second != node.value) {
            return false;
        }
    }
    std::map<std::pair<uint32_t, uint32_t>, Hash> authenticated;
    for (uint32_t l = 0; width > 1; ++l, width /= 2) {
        std::map<uint32_t, Hash> next;
        for (auto [id, h] : targets) {
            authenticated[{l, id}] = h;
            uint32_t sibling = id ^ 1;
            Hash sh;
            auto it = targets.find(sibling);
            if (it != targets.end()) {
                sh = it->second;
            } else {
                auto key = std::make_pair(l, sibling);
                auto pi = proof.find(key);
                if (pi == proof.end()) {
                    return false;
                }
                sh = pi->second;
                authenticated[key] = sh;
            }
            auto ph = (id % 2) ? pair_hash(sh, h) : pair_hash(h, sh);
            auto [ni, ok] = next.emplace(id / 2, ph);
            if (!ok && ni->second != ph) {
                return false;
            }
        }
        targets = std::move(next);
    }
    // Independent paths include redundant siblings that were also targets.
    // Check these against reconstructed values rather than silently ignoring them.
    for (auto [key, value] : proof) {
        auto it = authenticated.find(key);
        if (it == authenticated.end() || it->second != value) {
            return false;
        }
    }
    return targets.size() == 1 && targets.begin()->first == 0 && targets.begin()->second == root;
}

void proof_write(Bytes& b, const MerkleTree& t, const std::set<Id>& targets) {
    auto proof = t.prove(targets);
    u32(b, proof.size());
    for (auto node : proof) {
        digest(b, node.value);
    }
}

Proof proof_read(Reader& r, Id n, std::set<Id> targets) {
    Id width = 1;
    while (width < n) {
        width *= 2;
    }
    Proof result;
    Id count = r.count(32);
    for (Id level = 0; width > 1; ++level, width /= 2) {
        std::set<Id> next;
        for (Id id : targets) {
            if (!targets.count(id ^ 1)) {
                require(result.size() < count, "short multiproof");
                result.push_back({level, id ^ 1, r.digest()});
            }
            next.insert(id / 2);
        }
        targets = std::move(next);
    }
    require(result.size() == count, "noncanonical proof count");
    return result;
}

bool check_proof(Reader& r, Hash root, Id n, const std::map<Id, Hash>& leaves) {
    std::set<Id> targets;
    for (auto [id, h] : leaves) {
        targets.insert(id);
    }
    auto proof = proof_read(r, n, targets);
    return leaves.empty() ? proof.empty() : MerkleTree::verify(root, n, leaves, proof);
}
} // namespace qvann

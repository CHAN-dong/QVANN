#pragma once

#include "common/codec.h"

namespace qvann {

struct ProofNode {
    uint32_t level, position;
    Hash value;
};

using Proof = std::vector<ProofNode>;

struct MerkleTree {
    uint32_t n;
    std::vector<std::vector<Hash>> levels;
    explicit MerkleTree(std::vector<Hash> leaves);
    Hash root() const;
    Proof prove(std::set<Id> targets, bool paths = false) const;
    static bool verify(Hash root, uint32_t count, std::map<Id, Hash> targets, const Proof& proof);
};

void proof_write(Bytes& bytes, const MerkleTree& tree, const std::set<Id>& targets);
Proof proof_read(Reader& reader, Id count, std::set<Id> targets);
bool check_proof(Reader& reader, Hash root, Id count, const std::map<Id, Hash>& leaves);

} // namespace qvann

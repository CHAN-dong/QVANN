#include "index/merkle.h"
#include "utils/hash.h"

int main() try {
    using namespace qvann;
    std::vector<Hash> leaves;
    for (Id id = 0; id < 9; ++id) {
        Bytes bytes;
        u32(bytes, id);
        leaves.push_back(hash(bytes));
    }
    MerkleTree tree(leaves);
    for (const auto& targets : std::vector<std::set<Id>>{{0}, {8}, {0, 1, 7, 8}, {2, 4, 6}}) {
        Bytes wire;
        proof_write(wire, tree, targets);
        std::map<Id, Hash> selected;
        for (Id id : targets) {
            selected[id] = leaves[id];
        }
        Reader reader{wire};
        require(check_proof(reader, tree.root(), 9, selected), "honest multiproof rejected");
        reader.end();
        selected.begin()->second[0] ^= 1;
        Reader changed{wire};
        require(!check_proof(changed, tree.root(), 9, selected), "changed leaf accepted");
        wire.pop_back();
        bool rejected = false;
        try {
            Reader truncated{wire};
            check_proof(truncated, tree.root(), 9, selected);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "truncated proof accepted");
    }
    std::cout << "Proof: shared paths, padding and malformed proofs passed\n";
    return 0;
} catch (const std::exception& error) {

    std::cerr << error.what() << '\n';
    return 1;
}

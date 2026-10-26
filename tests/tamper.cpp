#include "tests/tamper.h"

namespace qvann {
void attacks(const VerificationObject& packet, const Digest& a, const Lookup& codec, const float* q,
             int k, int E, const std::string& path) {
    std::ofstream out(path + "/tamper_tests.csv");
    out << "attack,rejected\n";
    auto test = [&](const std::string& name, Bytes b, const float* query, const Digest& anchor,
                    int budget) {
        bool rejected = !verify(b, anchor, codec, query, k, budget);
        out << name << ',' << rejected << '\n';
        require(rejected, "tamper accepted: " + name);
    };
    for (auto [name, pos] :
         std::vector<std::pair<std::string, size_t>>{{"header", 0},
                                                     {"query_binding", 4},
                                                     {"answer", 48},
                                                     {"SQ4", packet.sq_offset},
                                                     {"Delta4to6", packet.delta_offset},
                                                     {"raw_vector", packet.raw_offset},
                                                     {"adjacency", packet.adj_offset},
                                                     {"proof", packet.wire.size() - 1}}) {
        require(pos > 0 || name == "header", "missing tamper target");
        auto b = packet.wire;
        b.at(pos) ^= 1;
        test(name, b, q, a, E);
    }
    auto b = packet.wire;
    b.pop_back();
    test("truncated", b, q, a, E);
    b = packet.wire;
    b.push_back(0);
    test("appended", b, q, a, E);
    auto changed = a;
    changed.route[0] ^= 1;
    test("wrong_root", packet.wire, q, changed, E);
    std::vector<float> query(q, q + codec.dim);
    query[0] += 1;
    test("wrong_query", packet.wire, query.data(), a, E);
    test("wrong_expansion_budget", packet.wire, q, a, E + 1);
}
} // namespace qvann

#include "verify/result.h"

namespace qvann {

void certify(const Evidence& evidence, const Trace& trace, const Lookup& codec, const float* q,
             int k) {
    const auto& answer = evidence.answer;
    const auto& raw = evidence.raw;
    const auto& codes = evidence.codes;
    const auto& deltas = evidence.deltas;
    std::set<Id> unique_answer;
    std::vector<std::pair<float, Id>> best;
    for (Id id : answer) {
        require(unique_answer.insert(id).second && raw.count(id), "invalid result");
    }
    for (auto& [id, v] : raw) {
        best.emplace_back(squared_l2(q, v.data(), codec.dim), id);
    }
    std::sort(best.begin(), best.end());
    require(best.size() >= size_t(k), "too few exact candidates");
    for (int i = 0; i < k; ++i) {
        require(best[i].second == answer[i], "top-k mismatch");
    }
    float tau = best[k - 1].first;
    std::set<Id> used_delta, used_raw;
    for (Id id : trace.disclosed) {
        const auto* s = codes.at(id).data();
        if (codec.lower(q, s) > tau) {
            continue;
        }
        used_delta.insert(id);
        auto d = deltas.at(id).data();
        if (codec.lower(q, s, d) > tau && !unique_answer.count(id)) {
            continue;
        }
        used_raw.insert(id);
        require(raw.count(id), "missing raw refinement");
    }
    require(used_delta.size() == deltas.size() && used_raw.size() == raw.size(),
            "extra refinement records");
}
} // namespace qvann

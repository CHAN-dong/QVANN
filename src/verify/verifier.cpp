#include "verify/verifier.h"
#include "utils/timer.h"
#include "verify/auth.h"
#include "verify/replay.h"
#include "verify/result.h"

namespace qvann {

bool verify(const Bytes& wire, const Digest& digest, const Lookup& codec, const float* query, int k,
            int budget, double* auth_ms, double* replay_ms) {
    try {
        auto started = Clock::now();
        auto evidence = authenticate(wire, digest, query, k, budget);
        if (auth_ms) {
            *auth_ms = ms(started);
        }
        started = Clock::now();
        auto trace = replay(evidence, digest, codec, query, budget);
        certify(evidence, trace, codec, query, k);
        if (replay_ms) {
            *replay_ms = ms(started);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace qvann

#pragma once

#include "common/vo.h"
#include "verify/verifier.h"

namespace qvann {

void attacks(const VerificationObject& packet, const Digest& digest, const Lookup& codec,
             const float* query, int k, int budget, const std::string& output);

} // namespace qvann

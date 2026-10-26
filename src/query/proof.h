#pragma once

#include "common/vo.h"
#include "index/index.h"

namespace qvann {

VerificationObject make_proof(const Graph& graph, const Lookup& codec, const Index& index,
                              const Trace& trace, const std::vector<Id>& answer, float threshold,
                              const float* query, int k, int budget);

} // namespace qvann

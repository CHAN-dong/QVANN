#pragma once

#include "verify/replay.h"

namespace qvann {

void certify(const Evidence& evidence, const Trace& trace, const Lookup& codec, const float* query,
             int k);

} // namespace qvann

#pragma once

#include "common/types.h"

namespace qvann {

inline double ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

} // namespace qvann

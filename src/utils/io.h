#pragma once

#include "common/types.h"

namespace qvann {

Matrix read_fvecs(const std::string& path, std::size_t limit = 0);
void save(const std::string& path, const Bytes& bytes);

} // namespace qvann

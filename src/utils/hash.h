#pragma once

#include "common/types.h"

namespace qvann {

Hash sha256(const void* data, std::size_t bytes);
Hash hash(const Bytes& bytes);
Hash pair_hash(const Hash& left, const Hash& right);
std::string hex(const Hash& hash);
Hash query_hash(const float* query, int dimension);
Hash data_hash(const Matrix& base);

} // namespace qvann

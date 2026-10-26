#pragma once

#include "common/types.h"

namespace qvann {

void u32(Bytes& bytes, uint32_t value);
void u64(Bytes& bytes, uint64_t value);
void f32(Bytes& bytes, float value);
void digest(Bytes& bytes, const Hash& value);
void string(Bytes& bytes, const std::string& value);

struct Reader {
    const Bytes& b;
    size_t p = 0;
    void need(size_t n);
    uint32_t u32();
    uint64_t u64();
    float f32();
    Hash digest();
    std::string string();
    uint32_t count(size_t minimum = 1);
    void end();
};

void write_header(Bytes& bytes, const float* query, int dim, int k, int budget,
                  const std::vector<Id>& answer);
std::vector<Id> read_header(Reader& reader, const float* query, int dim, int k, int budget);

} // namespace qvann

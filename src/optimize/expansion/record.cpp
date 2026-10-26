#include "optimize/expansion/record.h"

namespace qvann {
Bytes domain_bytes(const char* name) {
    return Bytes(name, name + std::strlen(name));
}

Hash refinement_leaf(const char* name, Id id, const uint8_t* data, size_t size) {
    auto b = domain_bytes(name);
    u32(b, id);
    b.insert(b.end(), data, data + size);
    return hash(b);
}
} // namespace qvann

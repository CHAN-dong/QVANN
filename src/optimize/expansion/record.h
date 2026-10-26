#pragma once

#include "common/codec.h"
#include "utils/hash.h"

namespace qvann {

Bytes domain_bytes(const char* name);
Hash refinement_leaf(const char* name, Id id, const uint8_t* data, size_t size);

template <class Codes>
Hash expansion_leaf(Hash snapshot, Id id, Id level, const std::vector<Id>& ns, int dim,
                    Codes code) {
    auto b = domain_bytes("record/expansion-direct/v1");
    b.reserve(64 + ns.size() * (4 + dim / 2));
    digest(b, snapshot);
    u32(b, id);
    u32(b, level);
    u32(b, ns.size());
    for (Id nb : ns) {
        u32(b, nb);
        auto s = code(nb);
        b.insert(b.end(), s, s + dim / 2);
    }
    return hash(b);
}

} // namespace qvann

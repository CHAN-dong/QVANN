#include "utils/hash.h"
#include "common/codec.h"
#include <openssl/sha.h>

namespace qvann {
Hash sha256(const void* data, std::size_t bytes) {
    Hash out{};
    SHA256(static_cast<const unsigned char*>(data), bytes, out.data());
    return out;
}

Hash hash(const Bytes& b) {
    Hash h{};

    SHA256(b.data(), b.size(), h.data());
    return h;
}

Hash pair_hash(const Hash& a, const Hash& b) {
    Bytes x(a.begin(), a.end());
    x.insert(x.end(), b.begin(), b.end());
    return hash(x);
}

std::string hex(const Hash& h) {
    std::ostringstream s;
    s << std::hex << std::setfill('0');
    for (auto c : h) {
        s << std::setw(2) << int(c);
    }
    return s.str();
}

Hash query_hash(const float* q, int d) {
    Bytes b;
    for (int j = 0; j < d; ++j) {
        f32(b, q[j]);
    }
    return hash(b);
}

Hash data_hash(const Matrix& b) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(b.values.data());
    return sha256(p, b.values.size() * sizeof(float));
}
} // namespace qvann

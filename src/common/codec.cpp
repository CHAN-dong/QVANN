#include "common/codec.h"
#include "utils/hash.h"

namespace qvann {
void u32(Bytes& b, uint32_t x) {
    for (int i = 0; i < 4; ++i) {
        b.push_back(uint8_t(x >> (8 * i)));
    }
}

void u64(Bytes& b, uint64_t x) {
    for (int i = 0; i < 8; ++i) {
        b.push_back(uint8_t(x >> (8 * i)));
    }
}

void f32(Bytes& b, float x) {
    uint32_t u;
    std::memcpy(&u, &x, 4);
    u32(b, u);
}

void digest(Bytes& b, const Hash& h) {
    b.insert(b.end(), h.begin(), h.end());
}

void string(Bytes& b, const std::string& s) {
    u32(b, s.size());
    b.insert(b.end(), s.begin(), s.end());
}

void Reader::need(size_t n) {
    if (n > b.size() - p) {
        throw std::runtime_error("truncated VO");
    }
}

uint32_t Reader::u32() {
    need(4);
    uint32_t x = 0;
    for (int i = 0; i < 4; ++i) {
        x |= uint32_t(b[p++]) << (8 * i);
    }
    return x;
}

uint64_t Reader::u64() {
    need(8);
    uint64_t x = 0;
    for (int i = 0; i < 8; ++i) {
        x |= uint64_t(b[p++]) << (8 * i);
    }
    return x;
}

float Reader::f32() {
    auto u = u32();
    float x;
    std::memcpy(&x, &u, 4);
    if (!std::isfinite(x)) {
        throw std::runtime_error("nonfinite float");
    }
    return x;
}

Hash Reader::digest() {
    need(32);
    Hash h;
    std::copy_n(b.begin() + p, 32, h.begin());
    p += 32;
    return h;
}

std::string Reader::string() {
    auto n = u32();
    need(n);
    std::string s(b.begin() + p, b.begin() + p + n);
    p += n;
    return s;
}

uint32_t Reader::count(size_t minimum) {
    auto n = u32();
    if (n > (b.size() - p) / minimum) {
        throw std::runtime_error("invalid VO count");
    }
    return n;
}

void Reader::end() {
    if (p != b.size()) {
        throw std::runtime_error("extra VO bytes");
    }
}

void write_header(Bytes& b, const float* q, int d, int k, int ef,
                  const std::vector<uint32_t>& answer) {
    u32(b, 0x56505231);
    digest(b, query_hash(q, d));
    u32(b, k);
    u32(b, ef);
    u32(b, answer.size());
    for (auto id : answer) {
        u32(b, id);
    }
}

std::vector<uint32_t> read_header(Reader& r, const float* q, int d, int k, int ef) {
    if (r.u32() != 0x56505231 || r.digest() != query_hash(q, d) || r.u32() != uint32_t(k) ||
        r.u32() != uint32_t(ef)) {
        throw std::runtime_error("invalid query binding");
    }
    auto n = r.count(4);
    if (n != uint32_t(k)) {
        throw std::runtime_error("invalid result count");
    }
    std::vector<uint32_t> a;
    for (uint32_t i = 0; i < n; ++i) {
        a.push_back(r.u32());
    }
    return a;
}
} // namespace qvann

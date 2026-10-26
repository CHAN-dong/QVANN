#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace qvann {

using Id = std::uint32_t;
using Key = std::pair<Id, Id>;
using Bytes = std::vector<std::uint8_t>;
using Hash = std::array<std::uint8_t, 32>;
using Clock = std::chrono::steady_clock;

struct Matrix {
    int dimension = 0;
    std::vector<float> values;
    std::size_t rows() const;
    const float* row(std::size_t i) const;
};

struct Trace {
    std::vector<Id> disclosed;
    std::set<Id> expanded;
    std::set<Key> opened;
};

float squared_l2(const float* a, const float* b, int dimension);

inline void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace qvann

#include "verifiable_ann_common.hpp"

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("usage: benchmark_index_build <fvecs>");
    const auto base = vann::read_fvecs(argv[1], 100000);
    const auto started = std::chrono::steady_clock::now();
    // No cache: measure actual graph construction without modifying packaged index.
    const auto index = vann::build_hnsw(base, 16, 100);
    std::cout << "vectors=" << index.size() << " build_seconds="
              << std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()
              << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Matrix {
    int dim = 0;
    std::vector<float> data;
    size_t rows() const { return dim ? data.size() / static_cast<size_t>(dim) : 0; }
    const float* operator[](size_t i) const { return data.data() + i * dim; }
};

static Matrix read_fvecs(const std::string& path, size_t limit = 0) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    Matrix m;
    while (!limit || m.rows() < limit) {
        int32_t d;
        if (!in.read(reinterpret_cast<char*>(&d), sizeof(d))) break;
        if (d <= 0 || (m.dim && d != m.dim)) throw std::runtime_error("bad fvecs file");
        m.dim = d;
        const size_t old = m.data.size(); m.data.resize(old + d);
        if (!in.read(reinterpret_cast<char*>(m.data.data() + old), sizeof(float) * d))
            throw std::runtime_error("truncated fvecs file");
    }
    return m;
}

static std::vector<std::vector<int>> read_ivecs(const std::string& path, size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::vector<std::vector<int>> out;
    while (out.size() < limit) {
        int32_t d;
        if (!in.read(reinterpret_cast<char*>(&d), 4)) break;
        out.emplace_back(d);
        if (!in.read(reinterpret_cast<char*>(out.back().data()), 4LL * d)) throw std::runtime_error("truncated ivecs");
    }
    return out;
}

static float l2sq(const float* a, const float* b, int d) {
    float s = 0;
    for (int j = 0; j < d; ++j) { const float v = a[j] - b[j]; s += v * v; }
    return s;
}

struct Quantizer {
    int bits, levels, dim;
    std::vector<float> lo, step, error;
    std::vector<uint8_t> codes; // one byte in RAM; payload calculation uses bit packing

    Quantizer(const Matrix& xb, int b) : bits(b), levels((1 << b) - 1), dim(xb.dim),
        lo(dim, std::numeric_limits<float>::infinity()), step(dim), error(xb.rows()),
        codes(xb.rows() * static_cast<size_t>(dim)) {
        std::vector<float> hi(dim, -std::numeric_limits<float>::infinity());
        for (size_t i = 0; i < xb.rows(); ++i) for (int j = 0; j < dim; ++j) {
            lo[j] = std::min(lo[j], xb[i][j]); hi[j] = std::max(hi[j], xb[i][j]);
        }
        for (int j = 0; j < dim; ++j) step[j] = (hi[j] - lo[j]) / levels;
        for (size_t i = 0; i < xb.rows(); ++i) {
            float e2 = 0;
            for (int j = 0; j < dim; ++j) {
                const int c = step[j] ? std::clamp(static_cast<int>(std::lround((xb[i][j] - lo[j]) / step[j])), 0, levels) : 0;
                codes[i * dim + j] = static_cast<uint8_t>(c);
                const float diff = xb[i][j] - (lo[j] + c * step[j]); e2 += diff * diff;
            }
            error[i] = std::nextafter(std::sqrt(e2) * (1.0f + 1e-6f) + 1e-5f,
                                      std::numeric_limits<float>::infinity());
        }
    }

    float lower_bound(const float* q, size_t id) const {
        float d2 = 0;
        for (int j = 0; j < dim; ++j) {
            const float xhat = lo[j] + codes[id * dim + j] * step[j];
            const float v = q[j] - xhat; d2 += v * v;
        }
        return std::max(0.0f, std::sqrt(d2) - error[id]);
    }
    int packed_bytes() const { return (dim * bits + 7) / 8; }
};

struct Config {
    std::string dataset = "../dataset/sift", assignments = "../populated_index/PriorMax_SIFT1M_C100_clusterid.tsv";
    std::string output = "quantization_results.csv";
    int nlist = 100, k = 100, queries = 50;
    std::vector<int> nprobes{1,2,4,8,16,32,64,100}, bits{4,6,8};
};

static std::vector<int> ints(const std::string& s) {
    std::vector<int> out; std::stringstream ss(s); std::string x;
    while (std::getline(ss, x, ',')) out.push_back(std::stoi(x));
    return out;
}

static Config parse(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&]() -> std::string { if (++i == argc) throw std::runtime_error("missing value"); return argv[i]; };
        if (a == "--dataset") c.dataset = val(); else if (a == "--assignments") c.assignments = val();
        else if (a == "--output") c.output = val(); else if (a == "--nlist") c.nlist = std::stoi(val());
        else if (a == "--k") c.k = std::stoi(val()); else if (a == "--queries") c.queries = std::stoi(val());
        else if (a == "--nprobes") c.nprobes = ints(val()); else if (a == "--bits") c.bits = ints(val());
        else if (a == "--help") { std::cout << "Usage: quantization_experiment [--k 100] [--bits 4,6,8] [--nprobes 1,2,...]\n"; std::exit(0); }
        else throw std::runtime_error("unknown option: " + a);
    }
    for (int b : c.bits) if (b < 1 || b > 8) throw std::runtime_error("bits must be in [1,8]");
    return c;
}

static std::vector<std::vector<int>> lists(const std::string& path, int nlist, size_t n) {
    std::ifstream in(path); if (!in) throw std::runtime_error("cannot open assignments");
    std::vector<std::vector<int>> out(nlist); long id, c; size_t count = 0;
    while (in >> id >> c) { if (id < 0 || static_cast<size_t>(id) >= n || c < 0 || c >= nlist) throw std::runtime_error("bad assignment"); out[c].push_back(id); ++count; }
    if (count != n) throw std::runtime_error("assignment count mismatch");
    return out;
}

static std::vector<std::vector<float>> means(const Matrix& xb, const std::vector<std::vector<int>>& ls) {
    std::vector<std::vector<float>> c(ls.size(), std::vector<float>(xb.dim));
    for (size_t i = 0; i < ls.size(); ++i) { for (int id : ls[i]) for (int j = 0; j < xb.dim; ++j) c[i][j] += xb[id][j]; for (float& x : c[i]) x /= ls[i].size(); }
    return c;
}

int main(int argc, char** argv) try {
    const Config cfg = parse(argc, argv);
    std::cerr << "Loading data...\n";
    const Matrix xb = read_fvecs(cfg.dataset + "/sift_base.fvecs");
    const Matrix xq = read_fvecs(cfg.dataset + "/sift_query.fvecs", cfg.queries);
    const auto gt = read_ivecs(cfg.dataset + "/sift_groundtruth.ivecs", xq.rows());
    const auto ls = lists(cfg.assignments, cfg.nlist, xb.rows()); const auto cs = means(xb, ls);
    std::vector<Quantizer> qs; for (int b : cfg.bits) { std::cerr << "Building safe SQ" << b << " certificates...\n"; qs.emplace_back(xb, b); }
    std::ofstream out(cfg.output);
    out << "k,nprobe,recall_at_k,bits,avg_visited,quant_only_ratio,exact_open_ratio,avg_exact_open,code_bytes,estimated_payload_bytes,baseline_payload_bytes,payload_reduction\n";
    std::cout << "nprobe recall bits quant_only exact_open payload_reduction\n";
    for (int np : cfg.nprobes) {
        uint64_t visited = 0, hits = 0; std::vector<uint64_t> filtered(qs.size());
        for (size_t qi = 0; qi < xq.rows(); ++qi) {
            std::vector<int> probes(cfg.nlist); std::iota(probes.begin(), probes.end(), 0);
            std::partial_sort(probes.begin(), probes.begin()+np, probes.end(), [&](int a, int b) { return l2sq(xq[qi], cs[a].data(), xb.dim) < l2sq(xq[qi], cs[b].data(), xb.dim); });
            using P = std::pair<float,int>; std::priority_queue<P> top;
            for (int p = 0; p < np; ++p) for (int id : ls[probes[p]]) {
                const float d = l2sq(xq[qi], xb[id], xb.dim); ++visited;
                if (static_cast<int>(top.size()) < cfg.k) top.push({d,id}); else if (d < top.top().first) { top.pop(); top.push({d,id}); }
            }
            const float tau = std::sqrt(top.top().first); std::vector<int> answer;
            while (!top.empty()) { answer.push_back(top.top().second); top.pop(); }
            for (int j = 0; j < cfg.k && j < static_cast<int>(gt[qi].size()); ++j) hits += std::find(answer.begin(), answer.end(), gt[qi][j]) != answer.end();
            for (int p = 0; p < np; ++p) for (int id : ls[probes[p]])
                for (size_t z = 0; z < qs.size(); ++z) filtered[z] += qs[z].lower_bound(xq[qi], id) > tau;
        }
        const double recall = static_cast<double>(hits)/(xq.rows()*cfg.k), base_bytes = visited * xb.dim * sizeof(float);
        for (size_t z = 0; z < qs.size(); ++z) {
            const uint64_t exact = visited-filtered[z];
            // Payload model: packed code + 4-byte certified error for every point, plus full vector for uncertain points.
            const double payload = visited * (qs[z].packed_bytes()+4.0) + exact * xb.dim * sizeof(float);
            const double qratio = static_cast<double>(filtered[z])/visited, reduction = 1.0-payload/base_bytes;
            out << cfg.k << ',' << np << ',' << recall << ',' << qs[z].bits << ',' << static_cast<double>(visited)/xq.rows() << ','
                << qratio << ',' << 1-qratio << ',' << static_cast<double>(exact)/xq.rows() << ',' << qs[z].packed_bytes() << ','
                << payload/xq.rows() << ',' << base_bytes/xq.rows() << ',' << reduction << '\n';
            std::cout << std::setw(6) << np << ' ' << std::fixed << std::setprecision(4) << recall << ' ' << std::setw(4) << qs[z].bits
                      << ' ' << std::setw(10) << qratio << ' ' << std::setw(10) << 1-qratio << ' ' << reduction << '\n';
        }
    }
    std::cerr << "Wrote " << cfg.output << '\n';
    return 0;
} catch (const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 1; }

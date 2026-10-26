// Reuse the exact data loaders and certified SQ implementation from the baseline.
#define main quantization_baseline_main
#include "quantization.cpp"
#undef main

struct VAConfig {
    std::string dataset = "../dataset/sift";
    std::string assignments = "../populated_index/PriorMax_SIFT1M_C100_clusterid.tsv";
    std::string output = "vo_aware_results.csv";
    std::string templates = "vo_aware_templates.csv";
    int nlist = 100, k = 100, test_queries = 50, train_queries = 50;
    std::vector<int> nprobes{1,2,4,8,16,32,64,100};
};

static VAConfig va_parse(int argc, char** argv) {
    VAConfig c;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() { if (++i == argc) throw std::runtime_error("missing value after " + a); return std::string(argv[i]); };
        if (a == "--dataset") c.dataset = value();
        else if (a == "--assignments") c.assignments = value();
        else if (a == "--output") c.output = value();
        else if (a == "--templates-output") c.templates = value();
        else if (a == "--test-queries") c.test_queries = std::stoi(value());
        else if (a == "--train-queries") c.train_queries = std::stoi(value());
        else if (a == "--k") c.k = std::stoi(value());
        else if (a == "--nprobes") c.nprobes = ints(value());
        else if (a == "--help") {
            std::cout << "Usage: vo_aware_quantization_experiment [--test-queries 50] "
                         "[--train-queries 50] [--k 100] [--nprobes 1,2,...]\n";
            std::exit(0);
        } else throw std::runtime_error("unknown option: " + a);
    }
    return c;
}

static std::vector<int> nearest_clusters(const float* q, int np,
        const std::vector<std::vector<float>>& cs, int d) {
    std::vector<int> p(cs.size()); std::iota(p.begin(), p.end(), 0);
    std::partial_sort(p.begin(), p.begin() + np, p.end(),
        [&](int a, int b) { return l2sq(q, cs[a].data(), d) < l2sq(q, cs[b].data(), d); });
    p.resize(np); return p;
}

static float exact_tau(const Matrix& xb, const float* q, int k,
        const std::vector<int>& probes, const std::vector<std::vector<int>>& ls,
        std::vector<int>* answer = nullptr) {
    using P = std::pair<float, int>; std::priority_queue<P> top;
    for (int c : probes) for (int id : ls[c]) {
        const float d = l2sq(q, xb[id], xb.dim);
        if (static_cast<int>(top.size()) < k) top.push({d, id});
        else if (d < top.top().first) { top.pop(); top.push({d, id}); }
    }
    if (static_cast<int>(top.size()) < k) throw std::runtime_error("fewer than k candidates");
    const float tau = std::sqrt(top.top().first);
    if (answer) { answer->clear(); while (!top.empty()) { answer->push_back(top.top().second); top.pop(); } }
    return tau;
}

int main(int argc, char** argv) try {
    const VAConfig cfg = va_parse(argc, argv);
    const int total_queries = cfg.test_queries + cfg.train_queries;
    std::cerr << "Loading data; test queries [0," << cfg.test_queries << "), train queries ["
              << cfg.test_queries << ',' << total_queries << ")...\n";
    const Matrix xb = read_fvecs(cfg.dataset + "/sift_base.fvecs");
    const Matrix xq = read_fvecs(cfg.dataset + "/sift_query.fvecs", total_queries);
    const auto gt = read_ivecs(cfg.dataset + "/sift_groundtruth.ivecs", cfg.test_queries);
    const auto ls = lists(cfg.assignments, cfg.nlist, xb.rows());
    const auto cs = means(xb, ls);
    const std::vector<int> bit_values{2,3,4};
    std::vector<Quantizer> qs;
    for (int b : bit_values) { std::cerr << "Building safe SQ" << b << " certificates...\n"; qs.emplace_back(xb, b); }

    std::ofstream result(cfg.output), selected_out(cfg.templates);
    result << "k,nprobe,recall_at_k,train_queries,test_queries,validation_penalty,sq2_clusters,sq3_clusters,sq4_clusters,"
              "avg_visited,quant_only_ratio,exact_open_ratio,avg_exact_open,avg_code_bytes,"
              "estimated_payload_bytes,baseline_payload_bytes,payload_reduction,best_fixed_bits,"
              "best_fixed_reduction,gain_percentage_points\n";
    selected_out << "nprobe,cluster_id,selected_bits\n";
    std::cout << "nprobe recall templates(2/3/4) exact_open avg_code optimized best_fixed gain_pp\n";

    for (int np : cfg.nprobes) {
        std::vector<std::array<double,3>> train_cost(cfg.nlist);
        std::vector<std::array<double,3>> select_cost(cfg.nlist), validation_cost(cfg.nlist);
        std::array<double,3> global_cost{};
        std::array<double,3> select_global{};
        std::vector<int> cluster_seen(cfg.nlist), select_seen(cfg.nlist);

        // Train: score the real byte cost of each fixed-bit template independently per cluster.
        for (int qi = cfg.test_queries; qi < total_queries; ++qi) {
            const auto probes = nearest_clusters(xq[qi], np, cs, xb.dim);
            const float tau = exact_tau(xb, xq[qi], cfg.k, probes, ls);
            for (int c : probes) {
                ++cluster_seen[c];
                for (int id : ls[c]) for (size_t z = 0; z < qs.size(); ++z) {
                    const double cost = qs[z].packed_bytes() + 4.0 +
                        (qs[z].lower_bound(xq[qi], id) <= tau ? xb.dim * 4.0 : 0.0);
                    train_cost[c][z] += cost; global_cost[z] += cost;
                    if (qi < cfg.test_queries + cfg.train_queries/2) {
                        select_cost[c][z] += cost; select_global[z] += cost;
                    } else validation_cost[c][z] += cost;
                }
                if (qi < cfg.test_queries + cfg.train_queries/2) ++select_seen[c];
            }
        }
        // Select a shrinkage penalty entirely inside the training split. A large penalty
        // falls back to the globally best fixed SQ and prevents sparse-cluster overfitting.
        const int select_global_best = static_cast<int>(std::min_element(select_global.begin(), select_global.end()) - select_global.begin());
        const std::vector<double> penalties{0.0,0.001,0.005,0.01,0.02,0.05,0.10,0.20,1e100};
        double chosen_penalty = penalties.back(), best_validation = std::numeric_limits<double>::infinity();
        for (double penalty : penalties) {
            double validation = 0;
            for (int c = 0; c < cfg.nlist; ++c) {
                int pick = select_global_best;
                if (select_seen[c] && penalty < 1e50) {
                    double best = std::numeric_limits<double>::infinity();
                    for (int z = 0; z < 3; ++z) {
                        const double score = select_cost[c][z] +
                            (z == select_global_best ? 0.0 : penalty*select_cost[c][select_global_best]);
                        if (score < best) { best = score; pick = z; }
                    }
                }
                validation += validation_cost[c][pick];
            }
            if (validation < best_validation) { best_validation = validation; chosen_penalty = penalty; }
        }
        const int global_best = static_cast<int>(std::min_element(global_cost.begin(), global_cost.end()) - global_cost.begin());
        std::vector<int> chosen(cfg.nlist, global_best); std::array<int,3> chosen_count{};
        for (int c = 0; c < cfg.nlist; ++c) {
            if (cluster_seen[c] && chosen_penalty < 1e50) {
                double best = std::numeric_limits<double>::infinity();
                for (int z = 0; z < 3; ++z) {
                    const double score = train_cost[c][z] +
                        (z == global_best ? 0.0 : chosen_penalty*train_cost[c][global_best]);
                    if (score < best) { best = score; chosen[c] = z; }
                }
            }
            ++chosen_count[chosen[c]];
            selected_out << np << ',' << c << ',' << bit_values[chosen[c]] << '\n';
        }

        uint64_t visited = 0, filtered = 0, exact = 0, hits = 0;
        double payload = 0, code_bytes_total = 0;
        std::array<double,3> fixed_payload{};
        for (int qi = 0; qi < cfg.test_queries; ++qi) {
            const auto probes = nearest_clusters(xq[qi], np, cs, xb.dim);
            std::vector<int> answer;
            const float tau = exact_tau(xb, xq[qi], cfg.k, probes, ls, &answer);
            for (int j = 0; j < cfg.k && j < static_cast<int>(gt[qi].size()); ++j)
                hits += std::find(answer.begin(), answer.end(), gt[qi][j]) != answer.end();
            for (int c : probes) for (int id : ls[c]) {
                ++visited;
                for (size_t z = 0; z < qs.size(); ++z)
                    fixed_payload[z] += qs[z].packed_bytes() + 4.0 +
                        (qs[z].lower_bound(xq[qi], id) <= tau ? xb.dim * 4.0 : 0.0);
                const Quantizer& qz = qs[chosen[c]];
                const bool pruned = qz.lower_bound(xq[qi], id) > tau;
                filtered += pruned; exact += !pruned;
                code_bytes_total += qz.packed_bytes();
                payload += qz.packed_bytes() + 4.0 + (!pruned ? xb.dim * 4.0 : 0.0);
            }
        }
        const double base = visited * xb.dim * 4.0;
        std::array<double,3> fixed_reduction{};
        for (size_t z = 0; z < 3; ++z) fixed_reduction[z] = 1.0 - fixed_payload[z] / base;
        const int best_fixed = static_cast<int>(std::max_element(fixed_reduction.begin(), fixed_reduction.end()) - fixed_reduction.begin());
        const double reduction = 1.0 - payload/base;
        const double gain_pp = (reduction-fixed_reduction[best_fixed])*100.0;
        const double recall = static_cast<double>(hits)/(cfg.test_queries*cfg.k);
        result << cfg.k << ',' << np << ',' << recall << ',' << cfg.train_queries << ',' << cfg.test_queries << ','
               << (chosen_penalty > 1e50 ? -1.0 : chosen_penalty) << ','
               << chosen_count[0] << ',' << chosen_count[1] << ',' << chosen_count[2] << ','
               << static_cast<double>(visited)/cfg.test_queries << ',' << static_cast<double>(filtered)/visited << ','
               << static_cast<double>(exact)/visited << ',' << static_cast<double>(exact)/cfg.test_queries << ','
               << code_bytes_total/visited << ',' << payload/cfg.test_queries << ',' << base/cfg.test_queries << ','
               << reduction << ',' << bit_values[best_fixed] << ',' << fixed_reduction[best_fixed] << ',' << gain_pp << '\n';
        std::cout << std::setw(6) << np << ' ' << std::fixed << std::setprecision(4) << recall << "  "
                  << chosen_count[0] << '/' << chosen_count[1] << '/' << chosen_count[2] << "  "
                  << static_cast<double>(exact)/visited << ' ' << code_bytes_total/visited << ' '
                  << reduction << " SQ" << bit_values[best_fixed] << '=' << fixed_reduction[best_fixed]
                  << " gain=" << std::setprecision(2) << gain_pp << "pp\n";
    }
    std::cerr << "Wrote " << cfg.output << " and " << cfg.templates << '\n';
    return 0;
} catch (const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 1; }

#include "index/graph.h"
#include "index/index.h"
#include "query/proof.h"
#include "query/search.h"
#include "query/topk.h"
#include "utils/config.h"
#include "utils/hash.h"
#include "utils/io.h"
#include "utils/metrics.h"
#include "utils/timer.h"
#include "verify/verifier.h"
#include <omp.h>

int main(int argc, char** argv) try {
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) {
        std::cout << "QVANN: authenticated approximate nearest neighbor search\n\n"
                  << "Usage: qvann --dataset DIR [options]\n"
                  << "  --base N                 Base vectors (100000)\n"
                  << "  --queries N              Queries to evaluate (100)\n"
                  << "  --query-offset N         First query index (0)\n"
                  << "  --M N                    Graph connectivity (8)\n"
                  << "  --ef-construction N      Construction parameter (100)\n"
                  << "  --budget E[,E...]        Expansion budgets (50)\n"
                  << "  --k K[,K...]             Result sizes (10)\n"
                  << "  --repeats N              Repetitions (1)\n"
                  << "  --build-threads N        Construction threads (1)\n"
                  << "  --cache DIR              Index cache (cache)\n"
                  << "  --output DIR             Local output (results)\n";
        return 0;
    }

    using namespace qvann;
    Config config = args(argc, argv);
    require(config.metric == "l2" && config.proof == "multiproof",
            "optimized protocol supports L2 multiproof only");
    std::cout << "[load] N=" << config.base << " queries=" << config.queries
              << " offset=" << config.query_offset << std::endl;
    auto base = read_fvecs(config.dataset + "/sift_base.fvecs", config.base),
         queries = read_queries(config);
    require(base.rows() == size_t(config.base), "short dataset");
    require(base.dimension == queries.dimension, "base/query dimension mismatch");
    require(base.dimension % 4 == 0, "dimension must be divisible by four");
    Hash data = data_hash(base);
    auto start = Clock::now();
    auto truth = ground_truth(
        base, queries, *std::max_element(config.result_sizes.begin(), config.result_sizes.end()));
    double gt_ms = ms(start);
    std::string cache = config.cache + "/" + hex(data).substr(0, 16) + "_M" +
                        std::to_string(config.M) + "_efc" + std::to_string(config.ef_construction) +
                        ".index";
    if (const char* override_path = std::getenv("QVANN_INDEX_PATH")) {
        cache = override_path;
    }
    start = Clock::now();
    omp_set_num_threads(config.build_threads);
    auto graph = build_graph(base, config.M, config.ef_construction, cache);
    omp_set_num_threads(1);
    double index_ms = ms(start);
    base.values.clear();
    base.values.shrink_to_fit();
    std::cout << "[index] ready; GT=" << gt_ms << "ms index=" << index_ms << "ms" << std::endl;
    start = Clock::now();
    Lookup quantizer(graph);
    Index index(graph, quantizer, data);
    double ads_ms = ms(start);
    // Pass metadata only to the client_quantizer; no database-sized code arrays.
    Lookup client_quantizer(quantizer, true);
    std::ofstream roots(config.output + "/trusted_roots.txt");
    roots << hex(index.anchor.route) << '\n'
          << hex(index.anchor.delta) << '\n'
          << hex(index.anchor.raw) << '\n';
    std::vector<Row> rows;
    bool sample_saved = false;
    for (int k : config.result_sizes) {
        for (int budget : config.budgets) {
            for (int repeat = 0; repeat < config.repeats; ++repeat) {
                for (int query_id = 0; query_id < config.queries; ++query_id) {
                    const float* query = queries.row(query_id);
                    Row row{};
                    row.query = query_id + config.query_offset;
                    row.repeat = repeat;
                    row.k = k;
                    row.budget = budget;
                    start = Clock::now();
                    auto trace = search(graph, quantizer, query, budget, index.anchor.entry,
                                        index.anchor.maxlevel);
                    auto selected = topk(graph, query, trace.disclosed, k);
                    const auto& answer = selected.answer;
                    float tau = selected.threshold;
                    row.search_ms = ms(start);
                    start = Clock::now();
                    auto packet =
                        make_proof(graph, quantizer, index, trace, answer, tau, query, k, budget);
                    row.vo_ms = ms(start);
                    start = Clock::now();
                    row.valid = verify(packet.wire, index.anchor, client_quantizer, query, k,
                                       budget, &row.auth_ms, &row.replay_ms);
                    row.verify_ms = ms(start);
                    require(row.valid, "honest QVANN VO rejected");
                    row.vo_bytes = packet.wire.size();
                    row.record_bytes = packet.records;
                    row.local_proof_bytes = packet.proofs - packet.route_proof;
                    row.route_bytes = packet.route_proof;
                    row.visited = trace.disclosed.size();
                    row.expanded = trace.expanded.size();
                    std::set<Id> expected(truth[query_id].begin(), truth[query_id].begin() + k);
                    int hits = 0;
                    for (Id id : answer) {
                        hits += expected.count(id);
                    }
                    row.recall = double(hits) / k;
                    row.recall1 = answer[0] == truth[query_id][0];
                    float optimal =
                        squared_l2(query, graph.vector(truth[query_id][k - 1]), graph.dimension());
                    row.kth_ratio =
                        optimal > 0 ? std::sqrt(tau / optimal) : (tau == 0 ? 1 : INFINITY);
                    rows.push_back(row);
                    if (!sample_saved) {
                        save(config.output + "/query0.vo", packet.wire);
                        sample_saved = true;
                    }
                    if ((query_id + 1) % 20 == 0) {
                        std::cout << "[query] k=" << k << " budget=" << budget
                                  << " repeat=" << repeat << " completed=" << query_id + 1 << '/'
                                  << config.queries << std::endl;
                    }
                }
            }
        }
    }
    size_t ads_bytes = 0;
    for (auto* t : {index.route.get(), index.delta.get(), index.raw.get()}) {
        for (auto& level : t->levels) {
            ads_bytes += level.size() * sizeof(Hash);
        }
    }
    output(config, rows, index_ms, ads_ms, gt_ms, data, "QVANN", ads_bytes);
    std::ofstream spec(config.output + "/optimizations.json");
    spec << R"({
  "traversal": "SQ4 top-1",
  "budget": "unique expanded-node count",
  "refinement": "SQ4 -> SQ6 -> raw",
  "authentication": "expansion-centric Merkle",
  "ordering": "HNSW-BFS",
  "multiproof": "canonical shared paths",
  "client": "VO-only authentication, replay and result certification"
})" << '\n';
    return 0;
} catch (const std::exception& e) {

    std::cerr << "error: " << e.what() << std::endl;
    return 1;
}

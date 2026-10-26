#include "utils/metrics.h"
#include "utils/hash.h"
#include <sys/resource.h>

namespace qvann {
namespace {
double mean(const std::vector<double>& values) {
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double quantile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values.at(size_t(std::ceil(fraction * values.size()) - 1));
}

struct Samples {
    std::vector<double> search, generation, verification, authentication, replay;
    std::vector<double> bytes, recall, recall1, verified;

    void add(const Row& row) {
        search.push_back(row.search_ms);
        generation.push_back(row.vo_ms);
        verification.push_back(row.verify_ms);
        authentication.push_back(row.auth_ms);
        replay.push_back(row.replay_ms);
        bytes.push_back(row.vo_bytes / 1024.0);
        recall.push_back(row.recall);
        recall1.push_back(row.recall1);
        verified.push_back(row.valid);
    }
};
} // namespace

void output(const Config& config, const std::vector<Row>& rows, double index_ms, double ads_ms,
            double gt_ms, const Hash& snapshot, const std::string& method, size_t ads_bytes) {
    std::ofstream per_query(config.output + "/per_query.csv");
    per_query << "method,query,repeat,k,budget,search_ms,vo_generation_ms,verification_ms,"
                 "authentication_ms,replay_ms,server_total_ms,end_to_end_compute_ms,vo_bytes,"
                 "vo_kib,record_bytes,local_proof_bytes,route_proof_bytes,visited,expanded,"
                 "recall_at_k,recall_at_1,kth_distance_ratio,verified\n";
    per_query << std::setprecision(10);
    for (const auto& row : rows) {
        per_query << method << ',' << row.query << ',' << row.repeat << ',' << row.k << ','
                  << row.budget << ',' << row.search_ms << ',' << row.vo_ms << ',' << row.verify_ms
                  << ',' << row.auth_ms << ',' << row.replay_ms << ',' << row.search_ms + row.vo_ms
                  << ',' << row.search_ms + row.vo_ms + row.verify_ms << ',' << row.vo_bytes << ','
                  << row.vo_bytes / 1024.0 << ',' << row.record_bytes << ','
                  << row.local_proof_bytes << ',' << row.route_bytes << ',' << row.visited << ','
                  << row.expanded << ',' << row.recall << ',' << row.recall1 << ',' << row.kth_ratio
                  << ',' << row.valid << '\n';
    }

    std::ofstream summary(config.output + "/summary.csv");
    summary
        << "method,k,budget,samples,search_mean_ms,search_p50_ms,search_p95_ms,"
           "vo_generation_mean_ms,verification_mean_ms,verification_p95_ms,"
           "authentication_mean_ms,replay_mean_ms,vo_mean_kib,vo_p95_kib,recall_at_k,"
           "recall_at_1,verified_fraction,index_ms,ads_ms,ground_truth_ms,ads_bytes,peak_rss_mib\n";
    std::map<std::pair<int, int>, Samples> groups;
    for (const auto& row : rows) {
        groups[{row.k, row.budget}].add(row);
    }
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    for (const auto& [key, samples] : groups) {
        auto [k, budget] = key;
        summary << method << ',' << k << ',' << budget << ',' << samples.search.size() << ','
                << mean(samples.search) << ',' << quantile(samples.search, .5) << ','
                << quantile(samples.search, .95) << ',' << mean(samples.generation) << ','
                << mean(samples.verification) << ',' << quantile(samples.verification, .95) << ','
                << mean(samples.authentication) << ',' << mean(samples.replay) << ','
                << mean(samples.bytes) << ',' << quantile(samples.bytes, .95) << ','
                << mean(samples.recall) << ',' << mean(samples.recall1) << ','
                << mean(samples.verified) << ',' << index_ms << ',' << ads_ms << ',' << gt_ms << ','
                << ads_bytes << ',' << usage.ru_maxrss / 1024.0 << '\n';
        std::cout << method << " k=" << k << " E=" << budget << " recall=" << mean(samples.recall)
                  << " VO=" << mean(samples.bytes) << " KiB search=" << mean(samples.search)
                  << "ms verify=" << mean(samples.verification) << "ms\n";
    }

    std::ofstream manifest(config.output + "/manifest.json");
    manifest << "{\n  \"method\": \"" << method << "\",\n"
             << "  \"base\": " << config.base << ",\n"
             << "  \"queries\": " << config.queries << ",\n"
             << "  \"query_offset\": " << config.query_offset << ",\n"
             << "  \"M\": " << config.M << ",\n"
             << "  \"ef_construction\": " << config.ef_construction << ",\n"
             << "  \"repeats\": " << config.repeats << ",\n"
             << "  \"build_threads\": " << config.build_threads << ",\n"
             << "  \"query_threads\": 1,\n"
             << "  \"metric\": \"squared_l2\",\n"
             << "  \"hash\": \"SHA-256\",\n"
             << "  \"proof\": \"multiproof\",\n"
             << "  \"dataset_sha256\": \"" << hex(snapshot) << "\",\n"
             << "  \"timing\": \"steady_clock; network transport excluded\"\n}\n";
}
} // namespace qvann

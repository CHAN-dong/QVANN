#include "optimize/traversal/lookup.h"
#include "tests/fixture.h"

int main() try {
    using namespace qvann;
    auto data = test::vectors();
    auto graph = test::graph(data);
    Quantizer scalar(graph);
    Lookup lookup(graph);
    Lookup client(lookup, true);
    require(client.high.empty() && client.low.empty(), "client retained server code arrays");
    for (size_t qi = 0; qi < 12; ++qi) {
        const float* query = data.row(qi);
        lookup.prepare(query);
        client.prepare(query);
        for (Id id = 0; id < data.rows(); ++id) {
            const auto* code = lookup.sq(id);
            const auto* delta = lookup.delta(id);
            require(lookup.score(query, code) == scalar.score(query, code), "SQ4 LUT differs");
            require(client.score(query, code) == lookup.score(query, code), "client score differs");
            for (auto refinement : {static_cast<const uint8_t*>(nullptr), delta}) {
                float bound = lookup.lower(query, code, refinement);
                require(bound == scalar.lower(query, code, refinement), "lower-bound LUT differs");
                require(bound <= squared_l2(query, graph.vector(id), graph.dimension()),
                        "unsafe lower bound");
            }
        }
    }
    std::cout << "Quantizer: scalar/LUT agreement and safe bounds passed\n";
    return 0;
} catch (const std::exception& error) {

    std::cerr << error.what() << '\n';
    return 1;
}

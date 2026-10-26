#include "index/index.h"
#include "query/proof.h"
#include "query/search.h"
#include "query/topk.h"
#include "tests/fixture.h"
#include "tests/tamper.h"
#include "utils/hash.h"

int main(int argc, char** argv) try {
    using namespace qvann;
    const std::string output = argc > 1 ? argv[1] : "test-output";
    std::filesystem::create_directories(output);
    auto data = test::vectors();
    auto graph = test::graph(data);
    Lookup codec(graph);
    Lookup client(codec, true);
    Index index(graph, codec, data_hash(data));
    for (Id qi = 0; qi < 5; ++qi) {
        const auto* query = data.row(qi);
        auto trace = search(graph, codec, query, 32, index.anchor.entry, index.anchor.maxlevel);
        auto selected = topk(graph, query, trace.disclosed, 10);
        auto packet = make_proof(graph, codec, index, trace, selected.answer, selected.threshold,
                                 query, 10, 32);
        require(verify(packet.wire, index.anchor, client, query, 10, 32), "honest VO rejected");
        attacks(packet, index.anchor, client, query, 10, 32, output);
    }
    std::cout << "Verifier: honest queries and 13 tamper cases per query passed\n";
    return 0;
} catch (const std::exception& error) {

    std::cerr << error.what() << '\n';
    return 1;
}

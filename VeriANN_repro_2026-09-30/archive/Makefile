CXX ?= g++
CXXFLAGS ?= -O3 -DNDEBUG -std=c++17 -Wall -Wextra -Wpedantic

.PHONY: all clean
all: veriann quantization_experiment vo_aware_quantization_experiment residual_pq_experiment block_auth_experiment batch_pruning_experiment adaptive_batch_experiment query_aware_batch_experiment pivot_batch_experiment box_batch_experiment ivf_granularity_experiment progressive_sq_experiment progressive_sq_fast_experiment vo_latency_quantization_experiment workload_block_sq_experiment trace_aware_merkle_ivf_experiment workload_hybrid_vo_experiment microcluster_routing_experiment complete_routing_vo_experiment

veriann: main.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

quantization_experiment: quantization.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

vo_aware_quantization_experiment: vo_aware_quantization.cpp quantization.cpp
	$(CXX) $(CXXFLAGS) vo_aware_quantization.cpp -o $@

residual_pq_experiment: residual_pq.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

block_auth_experiment: block_auth.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ -lcrypto

batch_pruning_experiment: batch_pruning.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) batch_pruning.cpp -o $@ -lcrypto

adaptive_batch_experiment: adaptive_batch.cpp batch_pruning.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) adaptive_batch.cpp -o $@ -lcrypto

query_aware_batch_experiment: query_aware_batch.cpp batch_pruning.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) query_aware_batch.cpp -o $@ -lcrypto

pivot_batch_experiment: pivot_batch.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) pivot_batch.cpp -o $@ -lcrypto

box_batch_experiment: box_batch.cpp batch_pruning.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) box_batch.cpp -o $@ -lcrypto

ivf_granularity_experiment: ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) ivf_granularity.cpp -o $@ -lcrypto

progressive_sq_experiment: progressive_sq.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) progressive_sq.cpp -o $@ -lcrypto

progressive_sq_fast_experiment: progressive_sq_fast.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) progressive_sq_fast.cpp -o $@ -lcrypto

vo_latency_quantization_experiment: vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) vo_latency_quantization.cpp -o $@ -lcrypto

workload_block_sq_experiment: workload_block_sq.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) workload_block_sq.cpp -o $@ -lcrypto

trace_aware_merkle_ivf_experiment: trace_aware_merkle_ivf.cpp progressive_sq_fast.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) trace_aware_merkle_ivf.cpp -o $@ -lcrypto

workload_hybrid_vo_experiment: workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) workload_hybrid_vo.cpp -o $@ -lcrypto

microcluster_routing_experiment: microcluster_routing.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) microcluster_routing.cpp -o $@ -lcrypto

complete_routing_vo_experiment: complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) complete_routing_vo.cpp -o $@ -lcrypto

best_first_auth_tree_experiment: best_first_auth_tree.cpp complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) best_first_auth_tree.cpp -o $@ -lcrypto

complete_block_vo_experiment: complete_block_vo.cpp complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) complete_block_vo.cpp -o $@ -lcrypto

residual_hier_vo_experiment: residual_hier_vo.cpp complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) residual_hier_vo.cpp -o $@ -lcrypto

residual_3level_vo_experiment: residual_3level_vo.cpp complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) residual_3level_vo.cpp -o $@ -lcrypto

pq_block_tree_vo_experiment: pq_block_tree_vo.cpp residual_hier_vo.cpp complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) pq_block_tree_vo.cpp -o $@ -lcrypto

region_block_graph_experiment: region_block_graph.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) region_block_graph.cpp -o $@ -lcrypto

hnsw_block_graph_experiment: hnsw_block_graph.cpp region_block_graph.cpp ../verifiable_ann_common.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -I.. -I../LearnedEarlyTer-20SIGMOD -I../LearnedEarlyTer-20SIGMOD/include $< -o $@ -L../LearnedEarlyTer-20SIGMOD -L../LearnedEarlyTer-20SIGMOD/lib -L/home/dwang12576/.conda/envs/torch_env/lib -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD) -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD/lib) -Wl,-rpath,/home/dwang12576/.conda/envs/torch_env/lib -lfaiss -l_lightgbm -lopenblas -llapack -lcrypto -lpthread -ldl

relaxed_hnsw_harvest_experiment: relaxed_hnsw_harvest.cpp ../verifiable_ann_common.hpp
	$(CXX) $(CXXFLAGS) -I.. -I../LearnedEarlyTer-20SIGMOD -I../LearnedEarlyTer-20SIGMOD/include $< -o $@ -L../LearnedEarlyTer-20SIGMOD -L../LearnedEarlyTer-20SIGMOD/lib -L/home/dwang12576/.conda/envs/torch_env/lib -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD) -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD/lib) -Wl,-rpath,/home/dwang12576/.conda/envs/torch_env/lib -lfaiss -l_lightgbm -lopenblas -llapack -lcrypto -lpthread -ldl

aggregated_mac_hnsw_experiment: aggregated_mac_hnsw.cpp relaxed_hnsw_harvest.cpp ../verifiable_ann_common.hpp
	$(CXX) $(CXXFLAGS) -I.. -I../LearnedEarlyTer-20SIGMOD -I../LearnedEarlyTer-20SIGMOD/include $< -o $@ -L../LearnedEarlyTer-20SIGMOD -L../LearnedEarlyTer-20SIGMOD/lib -L/home/dwang12576/.conda/envs/torch_env/lib -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD) -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD/lib) -Wl,-rpath,/home/dwang12576/.conda/envs/torch_env/lib -lfaiss -l_lightgbm -lopenblas -llapack -lcrypto -lpthread -ldl

progressive_relaxed_hnsw_experiment: progressive_relaxed_hnsw.cpp relaxed_hnsw_harvest.cpp ../verifiable_ann_common.hpp
	$(CXX) $(CXXFLAGS) -I.. -I../LearnedEarlyTer-20SIGMOD -I../LearnedEarlyTer-20SIGMOD/include $< -o $@ -L../LearnedEarlyTer-20SIGMOD -L../LearnedEarlyTer-20SIGMOD/lib -L/home/dwang12576/.conda/envs/torch_env/lib -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD) -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD/lib) -Wl,-rpath,/home/dwang12576/.conda/envs/torch_env/lib -lfaiss -l_lightgbm -lopenblas -llapack -lcrypto -lpthread -ldl

e_budget_relaxed_hnsw_experiment: e_budget_relaxed_hnsw.cpp relaxed_hnsw_harvest.cpp ../verifiable_ann_common.hpp
	$(CXX) $(CXXFLAGS) -I.. -I../LearnedEarlyTer-20SIGMOD -I../LearnedEarlyTer-20SIGMOD/include $< -o $@ -L../LearnedEarlyTer-20SIGMOD -L../LearnedEarlyTer-20SIGMOD/lib -L/home/dwang12576/.conda/envs/torch_env/lib -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD) -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD/lib) -Wl,-rpath,/home/dwang12576/.conda/envs/torch_env/lib -lfaiss -l_lightgbm -lopenblas -llapack -lcrypto -lpthread -ldl

verification_block_screen_experiment: verification_block_screen.cpp e_budget_relaxed_hnsw.cpp ../verifiable_ann_common.hpp
	$(CXX) $(CXXFLAGS) -I.. -I../LearnedEarlyTer-20SIGMOD -I../LearnedEarlyTer-20SIGMOD/include $< -o $@ -L../LearnedEarlyTer-20SIGMOD -L../LearnedEarlyTer-20SIGMOD/lib -L/home/dwang12576/.conda/envs/torch_env/lib -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD) -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD/lib) -Wl,-rpath,/home/dwang12576/.conda/envs/torch_env/lib -lfaiss -l_lightgbm -lopenblas -llapack -lcrypto -lpthread -ldl

sq4_progressive_adjacency_experiment: sq4_progressive_adjacency.cpp e_budget_relaxed_hnsw.cpp ../verifiable_ann_common.hpp
	$(CXX) $(CXXFLAGS) -I.. -I../LearnedEarlyTer-20SIGMOD -I../LearnedEarlyTer-20SIGMOD/include $< -o $@ -L../LearnedEarlyTer-20SIGMOD -L../LearnedEarlyTer-20SIGMOD/lib -L/home/dwang12576/.conda/envs/torch_env/lib -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD) -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD/lib) -Wl,-rpath,/home/dwang12576/.conda/envs/torch_env/lib -lfaiss -l_lightgbm -lopenblas -llapack -lcrypto -lpthread -ldl

expansion_centric_merkle_experiment: expansion_centric_merkle.cpp e_budget_relaxed_hnsw.cpp ../verifiable_ann_common.hpp
	$(CXX) $(CXXFLAGS) -I.. -I../LearnedEarlyTer-20SIGMOD -I../LearnedEarlyTer-20SIGMOD/include $< -o $@ -L../LearnedEarlyTer-20SIGMOD -L../LearnedEarlyTer-20SIGMOD/lib -L/home/dwang12576/.conda/envs/torch_env/lib -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD) -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD/lib) -Wl,-rpath,/home/dwang12576/.conda/envs/torch_env/lib -lfaiss -l_lightgbm -lopenblas -llapack -lcrypto -lpthread -ldl

refinement_merkle_ordering_experiment: refinement_merkle_ordering.cpp expansion_centric_merkle.cpp e_budget_relaxed_hnsw.cpp ../verifiable_ann_common.hpp
	$(CXX) $(CXXFLAGS) -I.. -I../LearnedEarlyTer-20SIGMOD -I../LearnedEarlyTer-20SIGMOD/include $< -o $@ -L../LearnedEarlyTer-20SIGMOD -L../LearnedEarlyTer-20SIGMOD/lib -L/home/dwang12576/.conda/envs/torch_env/lib -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD) -Wl,-rpath,$(abspath ../LearnedEarlyTer-20SIGMOD/lib) -Wl,-rpath,/home/dwang12576/.conda/envs/torch_env/lib -lfaiss -l_lightgbm -lopenblas -llapack -lcrypto -lpthread -ldl

pq_graph_vo_experiment: pq_graph_vo.cpp residual_hier_vo.cpp complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) pq_graph_vo.cpp -o $@ -lcrypto

pq_search_tree_vo_experiment: pq_search_tree_vo.cpp residual_hier_vo.cpp complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) pq_search_tree_vo.cpp -o $@ -lcrypto

auto_vo_tree_experiment: auto_vo_tree.cpp residual_hier_vo.cpp complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) auto_vo_tree.cpp -o $@ -lcrypto

budgeted_residual_vo_experiment: budgeted_residual_vo.cpp complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) budgeted_residual_vo.cpp -o $@ -lcrypto

compact_probe_allocator_experiment: compact_probe_allocator.cpp budgeted_residual_vo.cpp complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) compact_probe_allocator.cpp -o $@ -lcrypto

joint_vo_successive_refinement_experiment: joint_vo_successive_refinement.cpp compact_probe_allocator.cpp budgeted_residual_vo.cpp complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) joint_vo_successive_refinement.cpp -o $@ -lcrypto

joint_vo_bound_pruning_experiment: joint_vo_bound_pruning.cpp joint_vo_successive_refinement.cpp compact_probe_allocator.cpp budgeted_residual_vo.cpp complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) joint_vo_bound_pruning.cpp -o $@ -lcrypto

adaptive_auth_tree_experiment: adaptive_auth_tree.cpp complete_routing_vo.cpp workload_hybrid_vo.cpp vo_latency_quantization.cpp ivf_granularity.cpp block_auth.cpp
	$(CXX) $(CXXFLAGS) adaptive_auth_tree.cpp -o $@ -lcrypto

clean:
	$(RM) veriann quantization_experiment vo_aware_quantization_experiment residual_pq_experiment block_auth_experiment batch_pruning_experiment adaptive_batch_experiment query_aware_batch_experiment pivot_batch_experiment box_batch_experiment ivf_granularity_experiment progressive_sq_experiment progressive_sq_fast_experiment vo_latency_quantization_experiment workload_block_sq_experiment trace_aware_merkle_ivf_experiment workload_hybrid_vo_experiment microcluster_routing_experiment complete_routing_vo_experiment best_first_auth_tree_experiment complete_block_vo_experiment residual_hier_vo_experiment residual_3level_vo_experiment pq_block_tree_vo_experiment pq_graph_vo_experiment pq_search_tree_vo_experiment auto_vo_tree_experiment budgeted_residual_vo_experiment compact_probe_allocator_experiment joint_vo_successive_refinement_experiment joint_vo_bound_pruning_experiment adaptive_auth_tree_experiment hnsw_block_graph_experiment

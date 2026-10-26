#define COMPLETE_ROUTING_VO_NO_MAIN
#include "complete_routing_vo.cpp"
#undef COMPLETE_ROUTING_VO_NO_MAIN

// Two-level authenticated IVF with independently encoded coarse centroids and
// fine residuals.  For fine leaf i in coarse group g:
//     reconstructed_center_i = decode(coarse_g) + decode(residual_i).
// A stateless verifier receives every coarse code and the complete residual
// code slice for every selected group, then replays both routing stages.

struct RHStates {std::vector<VLState>state;std::vector<std::vector<int>>coarse;double candidates=0,recall=0,query_ms=0;};

static RHStates rh_states(const Matrix&xb,const Matrix&q,const std::vector<std::vector<int>>&lists,const Matrix&fine_centers,
        const std::vector<std::vector<int>>&groups,const std::vector<int>&group_of,const Matrix&coarse_centers,const Matrix&residual,
        const CRCodec&coarse_codec,const CRCodec&residual_codec,int coarse_probe,int fine_probe,int k,const std::vector<std::vector<int>>*gt){
    RHStates out;out.state.resize(q.rows());out.coarse.resize(q.rows());uint64_t hits=0,candidate_total=0;auto start=std::chrono::steady_clock::now();
    for(size_t qi=0;qi<q.rows();++qi){auto coarse_lut=coarse_codec.prepare(q[qi]);std::vector<int>order(groups.size());std::iota(order.begin(),order.end(),0);
        std::partial_sort(order.begin(),order.begin()+coarse_probe,order.end(),[&](int a,int b){float x=coarse_codec.distance(q[qi],coarse_lut,coarse_centers,a,false),y=coarse_codec.distance(q[qi],coarse_lut,coarse_centers,b,false);return x==y?a<b:x<y;});
        out.coarse[qi].assign(order.begin(),order.begin()+coarse_probe);std::vector<std::pair<float,int>>candidate;
        for(int z=0;z<coarse_probe;++z){int g=order[z];auto decoded=coarse_codec.decode(coarse_centers,g);std::vector<float>qr(q.d);for(int j=0;j<q.d;++j)qr[j]=q[qi][j]-decoded[j];auto lut=residual_codec.prepare(qr.data());
            for(int leaf:groups[g])candidate.push_back({residual_codec.distance(qr.data(),lut,residual,leaf,false),leaf});}
        if((int)candidate.size()<fine_probe)throw std::runtime_error("residual shortlist smaller than fine_probe");candidate_total+=candidate.size();
        std::partial_sort(candidate.begin(),candidate.begin()+fine_probe,candidate.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});
        auto&s=out.state[qi];for(int z=0;z<fine_probe;++z){if(group_of[candidate[z].second]<0)throw std::runtime_error("missing residual group");s.lists.push_back(candidate[z].second);}
        std::priority_queue<std::pair<float,int>>top;for(int c:s.lists)for(int id:lists[c]){float d=l2(q[qi],xb[id],xb.d);if((int)top.size()<k)top.push({d,id});else if(d<top.top().first||(d==top.top().first&&id<top.top().second)){top.pop();top.push({d,id});}}
        if((int)top.size()!=k)throw std::runtime_error("fewer than k residual candidates");s.tau2=top.top().first;while(!top.empty()){s.answer.push_back(top.top().second);top.pop();}
        if(gt){std::unordered_set<int>a(s.answer.begin(),s.answer.end());for(int id:(*gt)[qi])hits+=a.count(id);}}
    out.query_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/q.rows();out.candidates=candidate_total/(double)q.rows();out.recall=gt?hits/(q.rows()*(double)k):0;return out;
}

struct RHRoute {
    double parameters=0,coarse_codes=0,residual_codes=0;
    double group_metadata=0,fine_metadata=0,coarse_proofs=0,fine_proofs=0;
    double metadata=0,proofs=0,total=0;
    double entropy_models=0,entropy_coarse_codes=0,entropy_residual_codes=0,entropy_total=0;
    double huffman_models=0,huffman_coarse_codes=0,huffman_residual_codes=0,huffman_total=0;
};
struct RHLayout {std::vector<int>coarse_position,fine_position;double train_proofs=0;};

static double rh_layout_cost(const RHStates&trace,const std::vector<std::vector<int>>&groups,const std::vector<int>&group_of,
        const std::vector<int>&coarse_position,const std::vector<int>&fine_position){
    double hashes=0;std::vector<std::vector<int>>selected(groups.size());std::vector<int>mapped;
    for(size_t qi=0;qi<trace.state.size();++qi){mapped.clear();for(int g:trace.coarse[qi])mapped.push_back(coarse_position[g]);hashes+=hv_dummy_proof_count(groups.size(),mapped);
        for(auto&v:selected)v.clear();for(int leaf:trace.state[qi].lists)selected[group_of[leaf]].push_back(fine_position[leaf]);for(int g:trace.coarse[qi])if(!selected[g].empty())hashes+=hv_dummy_proof_count(groups[g].size(),selected[g]);}
    return hashes;
}

static RHLayout rh_learn_layout(const RHStates&train,const std::vector<std::vector<int>>&groups,const std::vector<int>&group_of,const std::vector<int>&original_position,uint64_t seed){
    RHLayout out;std::vector<int>identity(groups.size());std::iota(identity.begin(),identity.end(),0);out.coarse_position=identity;out.fine_position=original_position;
    std::vector<VLState>coarse_trace(train.state.size());for(size_t qi=0;qi<train.state.size();++qi)coarse_trace[qi].lists=train.coarse[qi];
    double best=std::numeric_limits<double>::infinity();for(int z=-1;z<16;++z){auto order=z<0?identity:hv_trace_order(groups.size(),coarse_trace,hv_mix(seed+z));auto pos=hv_positions(order);
        double cost=0;for(const auto&s:coarse_trace){std::vector<int>mapped;for(int g:s.lists)mapped.push_back(pos[g]);cost+=hv_dummy_proof_count(groups.size(),mapped);}if(cost<best){best=cost;out.coarse_position=std::move(pos);}}
    for(size_t g=0;g<groups.size();++g){int n=groups[g].size();std::vector<VLState>local(train.state.size());for(size_t qi=0;qi<train.state.size();++qi)for(int leaf:train.state[qi].lists)if(group_of[leaf]==(int)g)local[qi].lists.push_back(original_position[leaf]);
        std::vector<int>local_identity(n);std::iota(local_identity.begin(),local_identity.end(),0);std::vector<int>best_position=local_identity;double local_best=std::numeric_limits<double>::infinity();
        for(int z=-1;z<4;++z){auto order=z<0?local_identity:hv_trace_order(n,local,hv_mix(seed+g*17+z));auto pos=hv_positions(order);double cost=0;for(const auto&s:local)if(!s.lists.empty()){std::vector<int>mapped;for(int p:s.lists)mapped.push_back(pos[p]);cost+=hv_dummy_proof_count(n,mapped);}if(cost<local_best){local_best=cost;best_position=std::move(pos);}}
        for(int leaf:groups[g])out.fine_position[leaf]=best_position[original_position[leaf]];}
    out.train_proofs=rh_layout_cost(train,groups,group_of,out.coarse_position,out.fine_position);return out;
}

static RHRoute rh_route(const RHStates&query,const std::vector<std::vector<int>>&groups,const std::vector<int>&group_of,const RHLayout&layout,
        const CRCodec&coarse_codec,const CRCodec&residual_codec){
    RHRoute out;out.parameters=96.0+coarse_codec.parameter_bytes()+residual_codec.parameter_bytes();out.coarse_codes=groups.size()*(double)coarse_codec.code_bytes();
    out.entropy_models=coarse_codec.entropy_model_bytes()+residual_codec.entropy_model_bytes();std::vector<int>all_coarse(groups.size());std::iota(all_coarse.begin(),all_coarse.end(),0);
    out.entropy_coarse_codes=coarse_codec.entropy_bytes(all_coarse);
    out.huffman_models=coarse_codec.huffman_model_bytes()+residual_codec.huffman_model_bytes();out.huffman_coarse_codes=coarse_codec.huffman_bytes(all_coarse);
    std::vector<std::vector<int>>selected(groups.size());std::vector<int>mapped_coarse;for(size_t qi=0;qi<query.state.size();++qi){const auto&cg=query.coarse[qi];mapped_coarse.clear();for(int g:cg)mapped_coarse.push_back(layout.coarse_position[g]);out.group_metadata+=cg.size()*68.0;out.coarse_proofs+=hv_dummy_proof_count(groups.size(),mapped_coarse)*32.0;
        for(int g:cg){out.residual_codes+=groups[g].size()*(double)residual_codec.code_bytes();out.entropy_residual_codes+=residual_codec.entropy_bytes(groups[g]);out.huffman_residual_codes+=residual_codec.huffman_bytes(groups[g]);}for(auto&v:selected)v.clear();for(int leaf:query.state[qi].lists)selected[group_of[leaf]].push_back(layout.fine_position[leaf]);
        for(int g:cg)if(!selected[g].empty()){out.fine_metadata+=selected[g].size()*4.0;out.fine_proofs+=hv_dummy_proof_count(groups[g].size(),selected[g])*32.0;}}
    double nq=query.state.size();out.residual_codes/=nq;out.entropy_residual_codes/=nq;out.huffman_residual_codes/=nq;out.group_metadata/=nq;out.fine_metadata/=nq;out.coarse_proofs/=nq;out.fine_proofs/=nq;
    out.metadata=out.group_metadata+out.fine_metadata;out.proofs=out.coarse_proofs+out.fine_proofs;
    out.total=out.parameters+out.coarse_codes+out.residual_codes+out.metadata+out.proofs;
    out.entropy_total=out.parameters+out.entropy_models+out.entropy_coarse_codes+out.entropy_residual_codes+16.0+out.metadata+out.proofs;
    out.huffman_total=out.parameters+out.huffman_models+out.huffman_coarse_codes+out.huffman_residual_codes+16.0+out.metadata+out.proofs;return out;
}

#ifndef RESIDUAL_HIER_VO_NO_MAIN
int main(int argc,char**argv)try{
    auto cfg=cr_args(argc,argv);if(!cfg.coarse_nlist||cfg.coarse_codec.empty()||cfg.residual_codec.empty())throw std::runtime_error("require coarse options plus --coarse-codec and --residual-codec");
    std::cerr<<"Loading data...\n";Matrix xb=fvecs(cfg.data+"/sift_base.fvecs",cfg.base),trainq=fvecs(cfg.data+"/sift_learn.fvecs",cfg.train_queries),testq=fvecs(cfg.data+"/sift_query.fvecs",cfg.test_queries);
    std::cerr<<"Training fine IVF="<<cfg.nlist<<"...\n";auto lists=hkmeans(xb,cfg.nlist,cfg.iters);auto fine_vector=centers(xb,lists);Matrix fine_centers=cr_matrix(fine_vector);
    std::cerr<<"Grouping fine centers="<<cfg.coarse_nlist<<(cfg.balanced_groups?" (balanced)":"")<<"...\n";auto groups=cfg.balanced_groups?hkmeans_balanced(fine_centers,cfg.coarse_nlist,cfg.iters):hkmeans(fine_centers,cfg.coarse_nlist,cfg.iters);auto coarse_vector=centers(fine_centers,groups);Matrix coarse_centers=cr_matrix(coarse_vector);
    std::vector<int>group_of(cfg.nlist,-1),position(cfg.nlist,-1);for(size_t g=0;g<groups.size();++g)for(size_t p=0;p<groups[g].size();++p){group_of[groups[g][p]]=g;position[groups[g][p]]=p;}
    Matrix residual;residual.d=xb.d;residual.x.resize((size_t)cfg.nlist*xb.d);for(int leaf=0;leaf<cfg.nlist;++leaf)for(int j=0;j<xb.d;++j)residual.x[(size_t)leaf*xb.d+j]=fine_centers[leaf][j]-coarse_centers[group_of[leaf]][j];
    Matrix empty;uint64_t coarse_seed=cfg.coarse_seed?cfg.coarse_seed:cfg.seed,residual_seed=cfg.residual_seed?cfg.residual_seed:cfg.seed+1;
    std::cerr<<"Encoding coarse="<<cfg.coarse_codec<<" residual="<<cfg.residual_codec<<"...\n";CRCodec coarse_codec(cfg.coarse_codec,coarse_centers,empty,std::min(cfg.pq_train,cfg.coarse_nlist),cfg.pq_iters,coarse_seed);
    CRCodec residual_codec(cfg.residual_codec,residual,empty,cfg.pq_train,cfg.pq_iters,residual_seed);auto gt=exact_gt(xb,testq,cfg.k);
    auto train=rh_states(xb,trainq,lists,fine_centers,groups,group_of,coarse_centers,residual,coarse_codec,residual_codec,cfg.coarse_nprobe,cfg.nprobe,cfg.k,nullptr);
    auto test=rh_states(xb,testq,lists,fine_centers,groups,group_of,coarse_centers,residual,coarse_codec,residual_codec,cfg.coarse_nprobe,cfg.nprobe,cfg.k,&gt);
    auto boundaries=verification_tree_bounds(xb,trainq,lists,train.state,0,cfg.train_queries);NestedRule quantizer(xb,std::move(boundaries));if(!quantizer.contains(xb))throw std::runtime_error("quantizer containment failure");auto tail=hv_lossless_tail(xb,quantizer);
    HVLayout identity;identity.name="identity";identity.routing_position.resize(lists.size());std::iota(identity.routing_position.begin(),identity.routing_position.end(),0);int split=cfg.train_queries/2;
    auto order=hv_downgrade_order(xb,trainq,train.state,0,split,lists,quantizer);std::vector<int>counts{0,8,16,24,32,48,64,80,96,112,128};HVProtocol independent;independent.lossless_tail=true;independent.positional_ids=true;independent.public_dummy=true;
    int best_count=0;double best=std::numeric_limits<double>::infinity();for(int count:counts){auto bits=hv_downgrade_bits(xb.d,order,count);auto e=hv_protocol_evaluate(xb,trainq,train.state,split,cfg.train_queries,lists,quantizer,bits,identity,tail,independent,1,false);double leaf=e.total-e.routing;if(leaf<best){best=leaf;best_count=count;}}
    auto all_order=hv_downgrade_order(xb,trainq,train.state,0,cfg.train_queries,lists,quantizer);auto bits=hv_downgrade_bits(xb.d,all_order,best_count);auto independent_train=hv_protocol_evaluate(xb,trainq,train.state,0,cfg.train_queries,lists,quantizer,bits,identity,tail,independent,1,false);
    HVProtocol fused=independent;fused.fused_commitment=true;auto fused_train=hv_protocol_evaluate(xb,trainq,train.state,0,cfg.train_queries,lists,quantizer,bits,identity,tail,fused,1,false);const auto&protocol=(fused_train.total-fused_train.routing)<(independent_train.total-independent_train.routing)?fused:independent;
    auto leaf=hv_protocol_evaluate(xb,testq,test.state,0,cfg.test_queries,lists,quantizer,bits,identity,tail,protocol,cfg.repeats,true);double leaf_total=leaf.total-leaf.routing;auto route_layout=rh_learn_layout(train,groups,group_of,position,cfg.seed);auto route=rh_route(test,groups,group_of,route_layout,coarse_codec,residual_codec);double leaf_parameters=cr_leaf_parameter_bytes(quantizer,bits);route.parameters+=leaf_parameters;route.total+=leaf_parameters;route.entropy_total+=leaf_parameters;route.huffman_total+=leaf_parameters;double total=route.total+leaf_total;
    std::ofstream out(cfg.output);if(!out)throw std::runtime_error("cannot open output");out<<std::setprecision(12);out<<"coarse_codec,residual_codec,base,nlist,nprobe,coarse_nlist,coarse_nprobe,train_queries,test_queries,recall,avg_candidate_residuals,avg_visited,avg_refined,avg_exact,base_bits,routing_parameters,routing_coarse_codes,routing_residual_codes,routing_group_metadata,routing_fine_metadata,routing_coarse_proofs,routing_fine_proofs,routing_metadata,routing_proofs,routing_total,entropy_models,entropy_coarse_codes,entropy_residual_codes,entropy_routing_total,huffman_models,huffman_coarse_codes,huffman_residual_codes,huffman_routing_total,leaf_summary_payload,leaf_base_payload,leaf_refine_payload,leaf_exact_payload,leaf_local_merkle,leaf_total,total_vo,entropy_total_vo,huffman_total_vo,ann_query_ms,vo_generation_ms,violations\n";
    out<<cfg.coarse_codec<<','<<cfg.residual_codec<<','<<cfg.base<<','<<cfg.nlist<<','<<cfg.nprobe<<','<<cfg.coarse_nlist<<','<<cfg.coarse_nprobe<<','<<cfg.train_queries<<','<<cfg.test_queries<<','<<test.recall<<','<<test.candidates<<','<<leaf.visited<<','<<leaf.after4<<','<<leaf.exact<<','<<bit_sum(bits)<<','<<route.parameters<<','<<route.coarse_codes<<','<<route.residual_codes<<','<<route.group_metadata<<','<<route.fine_metadata<<','<<route.coarse_proofs<<','<<route.fine_proofs<<','<<route.metadata<<','<<route.proofs<<','<<route.total<<','<<route.entropy_models<<','<<route.entropy_coarse_codes<<','<<route.entropy_residual_codes<<','<<route.entropy_total<<','<<route.huffman_models<<','<<route.huffman_coarse_codes<<','<<route.huffman_residual_codes<<','<<route.huffman_total<<','<<leaf.summary_payload<<','<<leaf.base_payload<<','<<leaf.refine_payload<<','<<leaf.exact_payload<<','<<leaf.local<<','<<leaf_total<<','<<total<<','<<route.entropy_total+leaf_total<<','<<route.huffman_total+leaf_total<<','<<test.query_ms<<','<<leaf.ms<<','<<leaf.object_violations<<'\n';
    std::cout<<"coarse="<<cfg.coarse_codec<<" residual="<<cfg.residual_codec<<" recall="<<test.recall<<" candidates="<<test.candidates<<" visited="<<leaf.visited<<" route="<<route.total/1024<<" KiB leaf="<<leaf_total/1024<<" KiB total="<<total/1024<<" KiB query="<<test.query_ms<<" ms violations="<<leaf.object_violations<<'\n';return leaf.object_violations?2:0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
#endif

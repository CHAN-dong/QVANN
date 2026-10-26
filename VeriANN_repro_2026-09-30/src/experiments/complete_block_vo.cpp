#define COMPLETE_ROUTING_VO_NO_MAIN
#include "complete_routing_vo.cpp"
#undef COMPLETE_ROUTING_VO_NO_MAIN

// Complete-routing VO plus a learned authenticated cover inside every IVF
// leaf.  Learning decides only whether a static tree node is worth equipping
// with a batch certificate.  Query-time pruning still requires a conservative
// geometric lower bound, so a bad learned plan can increase bytes but cannot
// cause a false exclusion.

struct CBNode {
    std::vector<float>center;std::vector<uint8_t>code;float radius=0,qerr=0;
    int begin=0,end=0,left=-1,right=-1;bool leaf()const{return left<0;}
};
struct CBTree {std::vector<int>ids;std::vector<CBNode>nodes;};

class CBForest {
    const Matrix&x_;const SQ4&sq_;
    int build(CBTree&t,int begin,int end){
        int d=x_.d,n=end-begin;CBNode v;v.begin=begin;v.end=end;v.center.assign(d,0);std::vector<double>s2(d);
        for(int p=begin;p<end;++p)for(int j=0;j<d;++j){float z=x_[t.ids[p]][j];v.center[j]+=z;s2[j]+=z*z;}
        for(float&z:v.center)z/=n;int split_dim=0;double variance=-1;for(int j=0;j<d;++j){double z=s2[j]/n-v.center[j]*v.center[j];if(z>variance){variance=z;split_dim=j;}}
        float r2=0;for(int p=begin;p<end;++p)r2=std::max(r2,l2(x_[t.ids[p]],v.center.data(),d));
        v.radius=std::nextafter(std::sqrt(r2)*(1+1e-6f)+1e-5f,std::numeric_limits<float>::infinity());v.code.resize(d);float e2=0;
        for(int j=0;j<d;++j){int z=sq_.step[j]?std::clamp((int)std::lround((v.center[j]-sq_.lo[j])/sq_.step[j]),0,15):0;v.code[j]=(uint8_t)z;
            float e=v.center[j]-(sq_.lo[j]+z*sq_.step[j]);e2+=e*e;}
        v.qerr=std::nextafter(std::sqrt(e2)*(1+1e-6f)+1e-5f,std::numeric_limits<float>::infinity());int me=t.nodes.size();t.nodes.push_back(std::move(v));
        if(n==1)return me;int mid=begin+n/2;std::nth_element(t.ids.begin()+begin,t.ids.begin()+mid,t.ids.begin()+end,[&](int a,int b){float u=x_[a][split_dim],v=x_[b][split_dim];return u==v?a<b:u<v;});
        int left=build(t,begin,mid),right=build(t,mid,end);t.nodes[me].left=left;t.nodes[me].right=right;return me;
    }
public:
    std::vector<CBTree>trees;
    CBForest(const Matrix&x,const SQ4&sq,std::vector<std::vector<int>>&lists):x_(x),sq_(sq),trees(lists.size()){
        for(size_t c=0;c<lists.size();++c){trees[c].ids=lists[c];if(!lists[c].empty())build(trees[c],0,lists[c].size());lists[c]=trees[c].ids;}
    }
};

static float cb_lower(const float*q,const CBNode&n,const SQ4&sq){
    float d2=0;for(int j=0;j<sq.d;++j){float c=sq.lo[j]+n.code[j]*sq.step[j],z=q[j]-c;d2+=z*z;}
    return std::max(0.0f,std::sqrt(d2)-n.qerr-n.radius);
}

struct CBSegment {int node=-1;bool batch=false;};
struct CBDP {double best=0,raw=0;std::vector<CBSegment>segments;};
struct CBWork {const float*q=nullptr;double tau2=0;NestedRule::LUT first,last;};

static double cb_raw_cost(const Matrix&xb,const CBTree&t,const CBNode&n,const NestedRule&q,const std::vector<CBWork>&work,
        const std::vector<int>&work_ids,const std::vector<int>&bits,const HVLosslessTail&tail,const HVProtocol&protocol){
    int base_bytes=(bit_sum(bits)+7)/8,delta_bytes=(6*xb.d-bit_sum(bits)+7)/8;double out=0;
    for(int wi:work_ids){const auto&w=work[wi];for(int p=n.begin;p<n.end;++p){int id=t.ids[p];out+=base_bytes;bool pass=q.overlaps(id,w.tau2,w.first);
            if(pass){out+=delta_bytes+(protocol.fused_commitment?32.0:0.0)+32.0;pass=q.overlaps(id,w.tau2,w.last);}
            if(pass){out+=tail.bytes[id]+(protocol.positional_ids?4.0:0.0);if(!protocol.fused_commitment)out+=32.0;}}}
    return out;
}

static CBDP cb_learn_node(const Matrix&xb,const CBTree&t,int node,const SQ4&sq,const NestedRule&q,const std::vector<CBWork>&work,
        const std::vector<int>&work_ids,const std::vector<int>&bits,const HVLosslessTail&tail,const HVProtocol&protocol,int max_block){
    const auto&n=t.nodes[node];CBDP z;z.raw=cb_raw_cost(xb,t,n,q,work,work_ids,bits,tail,protocol);z.best=z.raw;z.segments={{node,false}};
    if(!n.leaf()){auto left=cb_learn_node(xb,t,n.left,sq,q,work,work_ids,bits,tail,protocol,max_block),right=cb_learn_node(xb,t,n.right,sq,q,work,work_ids,bits,tail,protocol,max_block);
        if(left.best+right.best<z.best){z.best=left.best+right.best;z.segments=std::move(left.segments);z.segments.insert(z.segments.end(),right.segments.begin(),right.segments.end());}}
    int count=n.end-n.begin;if(count<=max_block&&work_ids.size()>=2){double block=0;for(int wi:work_ids){const auto&w=work[wi];block+=108.0;
            float lb=cb_lower(w.q,n,sq);if((double)lb*lb<=w.tau2){CBWork one=w;std::vector<CBWork>single{std::move(one)};std::vector<int>zero{0};block+=cb_raw_cost(xb,t,n,q,single,zero,bits,tail,protocol);}}
        if(block<z.best){z.best=block;z.segments={{node,true}};}}
    return z;
}

struct CBEval {HVEval leaf;double blocks=0,opened=0;};
static CBEval cb_evaluate(const Matrix&xb,const Matrix&queries,const std::vector<VLState>&states,const std::vector<std::vector<int>>&lists,
        const CBForest&forest,const SQ4&sq,const std::vector<std::vector<CBSegment>>&plans,const NestedRule&q,const std::vector<int>&bits,
        const HVLosslessTail&tail,const HVProtocol&protocol,int repeats,bool safety){
    CBEval out;auto max_bits=uniform_bits(xb.d,6);int base_bytes=(bit_sum(bits)+7)/8,delta_bytes=(bit_sum(max_bits)-bit_sum(bits)+7)/8;
    auto hashes=[&](size_t n,const std::vector<int>&v){return protocol.public_dummy?hv_dummy_proof_count(n,v):proof_count(n,v);};
    for(int rep=0;rep<repeats;++rep){HVEval e;double blocks=0,opened=0;auto start=std::chrono::steady_clock::now();std::vector<int>refine,exact;
        for(size_t qi=0;qi<queries.rows();++qi){auto first=q.prepare(queries[qi],bits),last=q.prepare(queries[qi],max_bits);for(int c:states[qi].lists){const auto&tree=forest.trees[c];const auto&list=lists[c];
                e.visited+=list.size();refine.clear();exact.clear();for(const auto&segment:plans[c]){const auto&n=tree.nodes[segment.node];bool prune=false;
                    if(segment.batch){++blocks;e.summary_payload+=108.0;float lb=cb_lower(queries[qi],n,sq);prune=(double)lb*lb>states[qi].tau2;}
                    if(prune){++e.pruned_lists;e.pruned_vectors+=n.end-n.begin;if(safety)for(int p=n.begin;p<n.end;++p)if(l2(queries[qi],xb[tree.ids[p]],xb.d)<=states[qi].tau2)++e.block_violations;continue;}
                    opened+=n.end-n.begin;e.base_payload+=(n.end-n.begin)*base_bytes;for(int p=n.begin;p<n.end;++p){int id=tree.ids[p];bool pass=q.overlaps(id,states[qi].tau2,first);
                        if(pass){refine.push_back(p);pass=q.overlaps(id,states[qi].tau2,last);}if(pass)exact.push_back(p);if(safety){double d=l2(queries[qi],xb[id],xb.d);e.object_violations+=q.lower2(id,first)>d||q.lower2(id,last)>d;}}}
                e.after4+=refine.size();e.exact+=exact.size();e.refine_payload+=refine.size()*(delta_bytes+(protocol.fused_commitment?32.0:0.0));
                for(int p:exact)e.exact_payload+=tail.bytes[tree.ids[p]]+(protocol.positional_ids?4.0:0.0);e.refinement_merkle+=hashes(list.size(),refine)*32.0;
                if(!protocol.fused_commitment)e.exact_merkle+=hashes(list.size(),exact)*32.0;}}
        e.local=e.refinement_merkle+e.exact_merkle;e.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();double nq=queries.rows();
        e.summary_payload/=nq;e.base_payload/=nq;e.refine_payload/=nq;e.exact_payload/=nq;e.refinement_merkle/=nq;e.exact_merkle/=nq;e.local/=nq;e.visited/=nq;e.pruned_lists/=nq;e.pruned_vectors/=nq;e.after4/=nq;e.exact/=nq;e.ms/=nq;blocks/=nq;opened/=nq;
        out.leaf.summary_payload+=e.summary_payload;out.leaf.base_payload+=e.base_payload;out.leaf.refine_payload+=e.refine_payload;out.leaf.exact_payload+=e.exact_payload;out.leaf.refinement_merkle+=e.refinement_merkle;out.leaf.exact_merkle+=e.exact_merkle;out.leaf.local+=e.local;
        out.leaf.visited+=e.visited;out.leaf.pruned_lists+=e.pruned_lists;out.leaf.pruned_vectors+=e.pruned_vectors;out.leaf.after4+=e.after4;out.leaf.exact+=e.exact;out.leaf.ms+=e.ms;out.leaf.block_violations+=e.block_violations;out.leaf.object_violations+=e.object_violations;out.blocks+=blocks;out.opened+=opened;}
    double r=repeats;out.leaf.summary_payload/=r;out.leaf.base_payload/=r;out.leaf.refine_payload/=r;out.leaf.exact_payload/=r;out.leaf.refinement_merkle/=r;out.leaf.exact_merkle/=r;out.leaf.local/=r;out.leaf.visited/=r;out.leaf.pruned_lists/=r;out.leaf.pruned_vectors/=r;out.leaf.after4/=r;out.leaf.exact/=r;out.leaf.ms/=r;out.blocks/=r;out.opened/=r;
    out.leaf.total=out.leaf.summary_payload+out.leaf.base_payload+out.leaf.refine_payload+out.leaf.exact_payload+out.leaf.local;return out;
}

int main(int argc,char**argv)try{
    auto cfg=cr_args(argc,argv);if(cfg.coarse_nlist)throw std::runtime_error("complete block experiment currently supports flat routing only");
    std::cerr<<"Loading data...\n";Matrix xb=fvecs(cfg.data+"/sift_base.fvecs",cfg.base),trainq=fvecs(cfg.data+"/sift_learn.fvecs",cfg.train_queries),testq=fvecs(cfg.data+"/sift_query.fvecs",cfg.test_queries);
    std::cerr<<"Training IVF nlist="<<cfg.nlist<<"...\n";auto lists=hkmeans(xb,cfg.nlist,cfg.iters);auto fine_vector=centers(xb,lists);Matrix fine_centers=cr_matrix(fine_vector);
    SQ4 block_sq(xb);std::cerr<<"Building geometric singleton trees...\n";CBForest forest(xb,block_sq,lists);Matrix empty;CRCodec codec(cfg.codec,fine_centers,empty,cfg.pq_train,cfg.pq_iters,cfg.seed);
    auto gt=exact_gt(xb,testq,cfg.k);std::vector<std::vector<int>>no_groups;auto train=cr_states(xb,trainq,lists,fine_centers,no_groups,empty,codec,0,cfg.nprobe,cfg.k,nullptr);
    auto test=cr_states(xb,testq,lists,fine_centers,no_groups,empty,codec,0,cfg.nprobe,cfg.k,&gt);
    auto boundaries=verification_tree_bounds(xb,trainq,lists,train.state,0,cfg.train_queries);NestedRule quantizer(xb,std::move(boundaries));if(!quantizer.contains(xb))throw std::runtime_error("quantizer containment failure");auto tail=hv_lossless_tail(xb,quantizer);
    auto layouts=hv_layouts(xb,fine_vector,train.state,16,cfg.seed);const auto&layout=layouts[3];int split=cfg.train_queries/2;auto order=hv_downgrade_order(xb,trainq,train.state,0,split,lists,quantizer);
    std::vector<int>counts{0,8,16,24,32,48,64,80,96,112,128};HVProtocol independent;independent.lossless_tail=true;independent.positional_ids=true;independent.public_dummy=true;
    int best_count=0;double best=std::numeric_limits<double>::infinity();for(int count:counts){auto bits=hv_downgrade_bits(xb.d,order,count);auto e=hv_protocol_evaluate(xb,trainq,train.state,split,cfg.train_queries,lists,quantizer,bits,layout,tail,independent,1,false);double leaf=e.total-e.routing;if(leaf<best){best=leaf;best_count=count;}}
    auto all_order=hv_downgrade_order(xb,trainq,train.state,0,cfg.train_queries,lists,quantizer);auto bits=hv_downgrade_bits(xb.d,all_order,best_count);
    auto independent_train=hv_protocol_evaluate(xb,trainq,train.state,0,cfg.train_queries,lists,quantizer,bits,layout,tail,independent,1,false);HVProtocol fused=independent;fused.fused_commitment=true;
    auto fused_train=hv_protocol_evaluate(xb,trainq,train.state,0,cfg.train_queries,lists,quantizer,bits,layout,tail,fused,1,false);const auto&protocol=(fused_train.total-fused_train.routing)<(independent_train.total-independent_train.routing)?fused:independent;
    std::vector<CBWork>work(cfg.train_queries);std::vector<std::vector<int>>work_by_leaf(lists.size());auto max_bits=uniform_bits(xb.d,6);
    for(int qi=0;qi<cfg.train_queries;++qi){work[qi].q=trainq[qi];work[qi].tau2=train.state[qi].tau2;work[qi].first=quantizer.prepare(trainq[qi],bits);work[qi].last=quantizer.prepare(trainq[qi],max_bits);for(int c:train.state[qi].lists)work_by_leaf[c].push_back(qi);}
    std::cerr<<"Learning authenticated cover max_block="<<cfg.max_block<<"...\n";std::vector<std::vector<CBSegment>>plans(lists.size());for(size_t c=0;c<lists.size();++c)if(!forest.trees[c].nodes.empty())plans[c]=cb_learn_node(xb,forest.trees[c],0,block_sq,quantizer,work,work_by_leaf[c],bits,tail,protocol,cfg.max_block).segments;
    auto block=cb_evaluate(xb,testq,test.state,lists,forest,block_sq,plans,quantizer,bits,tail,protocol,cfg.repeats,true);auto plain=hv_protocol_evaluate(xb,testq,test.state,0,cfg.test_queries,lists,quantizer,bits,layout,tail,protocol,cfg.repeats,true);double plain_leaf=plain.total-plain.routing;
    auto route=cr_route_bytes(test,no_groups,cfg.nlist,codec,layout);double extra_parameters=1024.0+cr_leaf_parameter_bytes(quantizer,bits);route.parameters+=extra_parameters;route.total+=extra_parameters;double total=route.total+block.leaf.total;uint64_t violations=block.leaf.block_violations+block.leaf.object_violations;
    std::ofstream out(cfg.output);if(!out)throw std::runtime_error("cannot open output");out<<std::setprecision(12);
    out<<"codec,base,nlist,nprobe,max_block,train_queries,test_queries,recall,avg_visited,avg_block_records,avg_pruned_vectors,avg_opened,avg_refined,avg_exact,base_bits,block_payload,base_payload,refine_payload,exact_payload,local_merkle,routing_total,plain_leaf_total,block_leaf_total,total_vo,leaf_reduction,ann_query_ms,vo_generation_ms,violations\n";
    out<<cfg.codec<<','<<cfg.base<<','<<cfg.nlist<<','<<cfg.nprobe<<','<<cfg.max_block<<','<<cfg.train_queries<<','<<cfg.test_queries<<','<<test.recall<<','<<block.leaf.visited<<','<<block.blocks<<','<<block.leaf.pruned_vectors<<','<<block.opened<<','<<block.leaf.after4<<','<<block.leaf.exact<<','<<bit_sum(bits)<<','<<block.leaf.summary_payload<<','<<block.leaf.base_payload<<','<<block.leaf.refine_payload<<','<<block.leaf.exact_payload<<','<<block.leaf.local<<','<<route.total<<','<<plain_leaf<<','<<block.leaf.total<<','<<total<<','<<(1-block.leaf.total/plain_leaf)<<','<<test.query_ms<<','<<block.leaf.ms<<','<<violations<<'\n';
    std::cout<<"codec="<<cfg.codec<<" recall="<<test.recall<<" visited="<<block.leaf.visited<<" pruned="<<block.leaf.pruned_vectors<<" blocks="<<block.blocks<<" route="<<route.total/1024<<" KiB leaf="<<block.leaf.total/1024<<" KiB total="<<total/1024<<" KiB leaf_gain="<<(1-block.leaf.total/plain_leaf)*100<<"% query="<<test.query_ms<<" ms violations="<<violations<<'\n';return violations?2:0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

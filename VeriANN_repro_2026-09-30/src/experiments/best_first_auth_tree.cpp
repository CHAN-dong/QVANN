#define COMPLETE_ROUTING_VO_NO_MAIN
#include "complete_routing_vo.cpp"
#undef COMPLETE_ROUTING_VO_NO_MAIN

// Authenticated best-first binary routing tree.  The signed root commits to
// both child records.  Expanding a node reveals exactly two records:
//   routing_code || child_kind || child_id || subtree_size || child_commitment.
// The verifier maintains the same priority queue as the server, so omission or
// substitution changes either a commitment or the deterministic next node.

struct BFNode {
    std::vector<int> members;
    std::vector<float> center;
    int left=-1,right=-1,leaf=-1;
};

struct BFTree {
    std::vector<BFNode> node;
    std::vector<std::vector<int>> lists;
    Matrix centers;
};

static std::vector<float> bf_center(const Matrix&x,const std::vector<int>&ids){
    std::vector<float>c(x.d,0.0f);for(int id:ids)for(int j=0;j<x.d;++j)c[j]+=x[id][j];
    for(float&v:c)v/=ids.size();return c;
}

static BFTree bf_build(const Matrix&x,int leaves,int iters){
    BFTree t;BFNode root;root.members.resize(x.rows());std::iota(root.members.begin(),root.members.end(),0);
    root.center=bf_center(x,root.members);t.node.push_back(std::move(root));
    using Item=std::pair<size_t,int>;std::priority_queue<Item>q;q.push({x.rows(),0});int live=1;
    while(live<leaves){auto [sz,id]=q.top();q.pop();(void)sz;auto&n=t.node[id];
        if(n.members.size()<2)throw std::runtime_error("cannot split routing leaf further");auto p=split2(x,n.members,iters);
        if(p.first.empty()||p.second.empty())throw std::runtime_error("empty routing-tree child");
        BFNode a,b;a.members=std::move(p.first);b.members=std::move(p.second);a.center=bf_center(x,a.members);b.center=bf_center(x,b.members);
        int ai=t.node.size();t.node.push_back(std::move(a));int bi=t.node.size();t.node.push_back(std::move(b));
        t.node[id].left=ai;t.node[id].right=bi;t.node[id].members.clear();t.node[id].members.shrink_to_fit();
        q.push({t.node[ai].members.size(),ai});q.push({t.node[bi].members.size(),bi});++live;
        if((live&(live-1))==0)std::cerr<<" leaves="<<live<<'\n';
    }
    for(auto&n:t.node)if(n.left<0){n.leaf=t.lists.size();t.lists.push_back(std::move(n.members));}
    std::vector<std::vector<float>>all;t.node.shrink_to_fit();all.reserve(t.node.size());for(const auto&n:t.node)all.push_back(n.center);
    t.centers=cr_matrix(all);return t;
}

struct BFStates {std::vector<VLState>state;double expansions=0,recall=0,query_ms=0;};

static BFStates bf_states(const Matrix&xb,const Matrix&q,const BFTree&t,const CRCodec&codec,int nprobe,int k,
        const std::vector<std::vector<int>>*gt){
    struct Entry{float distance;int node;};struct Greater{bool operator()(const Entry&a,const Entry&b)const{
        return a.distance!=b.distance?a.distance>b.distance:a.node>b.node;}};
    BFStates out;out.state.resize(q.rows());uint64_t hits=0,expanded=0;auto start=std::chrono::steady_clock::now();
    for(size_t qi=0;qi<q.rows();++qi){auto lut=codec.prepare(q[qi]);std::priority_queue<Entry,std::vector<Entry>,Greater>frontier;
        frontier.push({0.0f,0});auto&s=out.state[qi];while((int)s.lists.size()<nprobe){
            if(frontier.empty())throw std::runtime_error("routing frontier exhausted");auto current=frontier.top();frontier.pop();const auto&n=t.node[current.node];
            if(n.left<0){s.lists.push_back(n.leaf);continue;}++expanded;
            for(int child:{n.left,n.right})frontier.push({codec.distance(q[qi],lut,t.centers,child,false),child});
        }
        std::priority_queue<std::pair<float,int>>top;for(int c:s.lists)for(int id:t.lists[c]){float d=l2(q[qi],xb[id],xb.d);
            if((int)top.size()<k)top.push({d,id});else if(d<top.top().first||(d==top.top().first&&id<top.top().second)){top.pop();top.push({d,id});}}
        if((int)top.size()!=k)throw std::runtime_error("fewer than k routed objects");s.tau2=top.top().first;
        while(!top.empty()){s.answer.push_back(top.top().second);top.pop();}if(gt){std::unordered_set<int>a(s.answer.begin(),s.answer.end());for(int id:(*gt)[qi])hits+=a.count(id);}
    }
    out.query_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/q.rows();
    out.expansions=expanded/(double)q.rows();out.recall=gt?hits/(q.rows()*(double)k):0;return out;
}

static BFStates bf_beam_states(const Matrix&xb,const Matrix&q,const BFTree&t,const CRCodec&codec,int beam,int nprobe,int k,
        const std::vector<std::vector<int>>*gt){
    if(beam<nprobe)throw std::runtime_error("beam must be at least nprobe");BFStates out;out.state.resize(q.rows());uint64_t hits=0,expanded=0;
    auto start=std::chrono::steady_clock::now();for(size_t qi=0;qi<q.rows();++qi){auto lut=codec.prepare(q[qi]);std::vector<int>frontier{0};
        for(;;){bool terminal=true;std::vector<int>next;next.reserve(frontier.size()*2);for(int id:frontier){const auto&n=t.node[id];
                if(n.left<0)next.push_back(id);else{terminal=false;++expanded;next.push_back(n.left);next.push_back(n.right);}}
            if(terminal)break;auto cmp=[&](int a,int b){float x=codec.distance(q[qi],lut,t.centers,a,false),y=codec.distance(q[qi],lut,t.centers,b,false);return x==y?a<b:x<y;};
            if((int)next.size()>beam){std::partial_sort(next.begin(),next.begin()+beam,next.end(),cmp);next.resize(beam);}frontier.swap(next);
        }
        auto cmp=[&](int a,int b){float x=codec.distance(q[qi],lut,t.centers,a,false),y=codec.distance(q[qi],lut,t.centers,b,false);return x==y?a<b:x<y;};
        if((int)frontier.size()<nprobe)throw std::runtime_error("beam ended with too few leaves");std::partial_sort(frontier.begin(),frontier.begin()+nprobe,frontier.end(),cmp);
        auto&s=out.state[qi];for(int z=0;z<nprobe;++z)s.lists.push_back(t.node[frontier[z]].leaf);std::priority_queue<std::pair<float,int>>top;
        for(int c:s.lists)for(int id:t.lists[c]){float d=l2(q[qi],xb[id],xb.d);if((int)top.size()<k)top.push({d,id});else if(d<top.top().first||(d==top.top().first&&id<top.top().second)){top.pop();top.push({d,id});}}
        if((int)top.size()!=k)throw std::runtime_error("fewer than k routed objects");s.tau2=top.top().first;while(!top.empty()){s.answer.push_back(top.top().second);top.pop();}
        if(gt){std::unordered_set<int>a(s.answer.begin(),s.answer.end());for(int id:(*gt)[qi])hits+=a.count(id);}
    }
    out.query_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/q.rows();out.expansions=expanded/(double)q.rows();
    out.recall=gt?hits/(q.rows()*(double)k):0;return out;
}

int main(int argc,char**argv)try{
    auto cfg=cr_args(argc,argv);if(cfg.coarse_nlist)throw std::runtime_error("best-first tree does not use coarse options");
    std::cerr<<"Loading data...\n";Matrix xb=fvecs(cfg.data+"/sift_base.fvecs",cfg.base),trainq=fvecs(cfg.data+"/sift_learn.fvecs",cfg.train_queries),testq=fvecs(cfg.data+"/sift_query.fvecs",cfg.test_queries);
    std::cerr<<"Building authenticated binary routing tree leaves="<<cfg.nlist<<"...\n";auto tree=bf_build(xb,cfg.nlist,cfg.iters);
    Matrix empty;std::cerr<<"Training/encoding routing codec="<<cfg.codec<<" nodes="<<tree.node.size()<<"...\n";CRCodec codec(cfg.codec,tree.centers,empty,cfg.pq_train,cfg.pq_iters,cfg.seed);
    auto gt=exact_gt(xb,testq,cfg.k);auto train=cfg.beam?bf_beam_states(xb,trainq,tree,codec,cfg.beam,cfg.nprobe,cfg.k,nullptr):bf_states(xb,trainq,tree,codec,cfg.nprobe,cfg.k,nullptr);
    auto test=cfg.beam?bf_beam_states(xb,testq,tree,codec,cfg.beam,cfg.nprobe,cfg.k,&gt):bf_states(xb,testq,tree,codec,cfg.nprobe,cfg.k,&gt);
    std::cerr<<"Training leaf verification codes...\n";auto boundaries=verification_tree_bounds(xb,trainq,tree.lists,train.state,0,cfg.train_queries);NestedRule quantizer(xb,std::move(boundaries));
    if(!quantizer.contains(xb))throw std::runtime_error("quantizer containment failure");auto tail=hv_lossless_tail(xb,quantizer);
    HVLayout identity;identity.name="identity";identity.routing_position.resize(tree.lists.size());std::iota(identity.routing_position.begin(),identity.routing_position.end(),0);
    int split=cfg.train_queries/2;auto order=hv_downgrade_order(xb,trainq,train.state,0,split,tree.lists,quantizer);std::vector<int>counts{0,8,16,24,32,48,64,80,96,112,128};
    HVProtocol independent;independent.lossless_tail=true;independent.positional_ids=true;independent.public_dummy=true;int best_count=0;double best=std::numeric_limits<double>::infinity();
    for(int count:counts){auto bits=hv_downgrade_bits(xb.d,order,count);auto e=hv_protocol_evaluate(xb,trainq,train.state,split,cfg.train_queries,tree.lists,quantizer,bits,identity,tail,independent,1,false);
        double leaf=e.total-e.routing;if(leaf<best){best=leaf;best_count=count;}}
    auto all_order=hv_downgrade_order(xb,trainq,train.state,0,cfg.train_queries,tree.lists,quantizer);auto bits=hv_downgrade_bits(xb.d,all_order,best_count);
    auto independent_train=hv_protocol_evaluate(xb,trainq,train.state,0,cfg.train_queries,tree.lists,quantizer,bits,identity,tail,independent,1,false);
    HVProtocol fused=independent;fused.fused_commitment=true;auto fused_train=hv_protocol_evaluate(xb,trainq,train.state,0,cfg.train_queries,tree.lists,quantizer,bits,identity,tail,fused,1,false);
    const bool use_fused=(fused_train.total-fused_train.routing)<(independent_train.total-independent_train.routing);const auto&protocol=use_fused?fused:independent;
    auto leaf=hv_protocol_evaluate(xb,testq,test.state,0,cfg.test_queries,tree.lists,quantizer,bits,identity,tail,protocol,cfg.repeats,true);double leaf_total=leaf.total-leaf.routing;
    double route_parameters=96.0+codec.parameter_bytes();double route_records=test.expansions*2.0*(codec.code_bytes()+41.0);double route=route_parameters+route_records,total=route+leaf_total;
    std::ofstream out(cfg.output);if(!out)throw std::runtime_error("cannot open output");out<<std::setprecision(12);
    out<<"codec,base,nlist,nprobe,beam,train_queries,test_queries,recall,avg_expanded_internal,avg_routing_records,avg_visited,avg_refined,avg_exact,base_bits,base_payload,refine_payload,exact_payload,local_merkle,routing_parameters,routing_records,routing_total,leaf_total,total_vo,ann_query_ms,vo_generation_ms,violations\n";
    out<<cfg.codec<<','<<cfg.base<<','<<cfg.nlist<<','<<cfg.nprobe<<','<<cfg.beam<<','<<cfg.train_queries<<','<<cfg.test_queries<<','<<test.recall<<','<<test.expansions<<','<<test.expansions*2<<','<<leaf.visited<<','<<leaf.after4<<','<<leaf.exact<<','<<bit_sum(bits)<<','<<leaf.base_payload<<','<<leaf.refine_payload<<','<<leaf.exact_payload<<','<<leaf.local<<','<<route_parameters<<','<<route_records<<','<<route<<','<<leaf_total<<','<<total<<','<<test.query_ms<<','<<leaf.ms<<','<<leaf.object_violations<<'\n';
    std::cout<<"codec="<<cfg.codec<<" recall="<<test.recall<<" expanded="<<test.expansions<<" visited="<<leaf.visited<<" route="<<route/1024<<" KiB leaf="<<leaf_total/1024<<" KiB total="<<total/1024<<" KiB query="<<test.query_ms<<" ms violations="<<leaf.object_violations<<'\n';return leaf.object_violations?2:0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

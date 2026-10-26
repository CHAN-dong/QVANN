#define COMPLETE_ROUTING_VO_NO_MAIN
#include "complete_routing_vo.cpp"
#undef COMPLETE_ROUTING_VO_NO_MAIN

#include <sys/resource.h>

// Single-parameter authenticated residual search.  The public efVO parameter
// is a charged byte budget, not a lookup key for a per-level probe vector.

struct BCfg {
    std::string data="../dataset/sift",output="budgeted_residual_vo.csv";
    int base=100000,train_queries=120,test_queries=200,k=100,iters=4,pq_train=12000,pq_iters=4,repeats=1;
    std::vector<int>counts{256,4096,65536};std::vector<std::string>modes{"sq4","pq24","pq16"};
    std::vector<double>budgets_kib{256,384,512,640,768,1024};double radius_alpha=0,cost_lambda=0;bool per_node_radius=false;
    uint64_t seed=20260901;
};

static std::vector<int> b_ints(const std::string&s){std::vector<int>v;std::stringstream ss(s);std::string x;while(std::getline(ss,x,','))v.push_back(std::stoi(x));return v;}
static std::vector<double> b_doubles(const std::string&s){std::vector<double>v;std::stringstream ss(s);std::string x;while(std::getline(ss,x,','))v.push_back(std::stod(x));return v;}
static std::vector<std::string> b_strings(const std::string&s){std::vector<std::string>v;std::stringstream ss(s);std::string x;while(std::getline(ss,x,','))v.push_back(x);return v;}

static BCfg b_args(int argc,char**argv){
    BCfg c;for(int i=1;i<argc;++i){std::string a=argv[i];auto v=[&](){if(++i==argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
        if(a=="--dataset")c.data=v();else if(a=="--output")c.output=v();else if(a=="--base")c.base=std::stoi(v());
        else if(a=="--train-queries")c.train_queries=std::stoi(v());else if(a=="--test-queries")c.test_queries=std::stoi(v());else if(a=="--k")c.k=std::stoi(v());
        else if(a=="--counts")c.counts=b_ints(v());else if(a=="--codecs")c.modes=b_strings(v());else if(a=="--budgets-kib")c.budgets_kib=b_doubles(v());
        else if(a=="--radius-alpha")c.radius_alpha=std::stod(v());else if(a=="--cost-lambda")c.cost_lambda=std::stod(v());
        else if(a=="--per-node-radius")c.per_node_radius=true;
        else if(a=="--iters")c.iters=std::stoi(v());else if(a=="--pq-train")c.pq_train=std::stoi(v());else if(a=="--pq-iters")c.pq_iters=std::stoi(v());
        else if(a=="--repeats")c.repeats=std::stoi(v());else if(a=="--seed")c.seed=std::stoull(v());else throw std::runtime_error("unknown option "+a);}
    if(c.counts.empty()||c.counts.size()>4||c.modes.size()!=c.counts.size()||c.budgets_kib.empty()||c.base<c.counts.back()||c.train_queries<2||c.test_queries<1||c.k<1||c.radius_alpha<0||c.cost_lambda<0)throw std::runtime_error("invalid configuration");
    for(size_t i=1;i<c.counts.size();++i)if(c.counts[i]<=c.counts[i-1])throw std::runtime_error("counts must increase by level");
    std::sort(c.budgets_kib.begin(),c.budgets_kib.end());return c;
}

static int b_log2ceil(int n){int z=0,x=1;while(x<std::max(1,n)){x<<=1;++z;}return z;}

struct BIndex {
    const Matrix*xb=nullptr;int depth=0,d=0;std::vector<int>counts;
    std::vector<std::vector<std::vector<int>>>children;
    std::vector<std::vector<int>>parent,position;
    std::vector<Matrix>center,encoded;
    std::vector<std::unique_ptr<CRCodec>>codec;
    std::vector<std::vector<double>>cover;
    std::vector<double>level_cover;
    std::vector<std::vector<int>>lists;
    double build_s=0,static_charge=0,leaf_unit=0;
};

static BIndex b_build(const Matrix&xb,const BCfg&c,const std::vector<std::vector<int>>*reuse_lists=nullptr,const Matrix*reuse_fine_center=nullptr){
    auto begin=std::chrono::steady_clock::now();BIndex t;t.xb=&xb;t.depth=c.counts.size();t.d=xb.d;t.counts=c.counts;t.center.resize(t.depth);t.encoded.resize(t.depth);t.children.resize(std::max(0,t.depth-1));t.parent.resize(t.depth);t.position.resize(t.depth);t.cover.resize(t.depth);
    int last=t.depth-1,K=t.counts[last];if(reuse_lists&&reuse_fine_center){if((int)reuse_lists->size()!=K||reuse_fine_center->rows()!=(size_t)K)throw std::runtime_error("incompatible reused fine clustering");std::cerr<<"Reuse fine leaves="<<K<<"...\n";t.lists=*reuse_lists;t.center[last]=*reuse_fine_center;}else{std::cerr<<"Fine leaves="<<K<<"...\n";t.lists=hkmeans(xb,K,c.iters);t.center[last]=cr_matrix(centers(xb,t.lists));}
    for(int l=last-1;l>=0;--l){std::cerr<<"Residual parents level="<<l<<" count="<<t.counts[l]<<"...\n";t.children[l]=hkmeans_balanced(t.center[l+1],t.counts[l],c.iters);t.center[l]=cr_matrix(centers(t.center[l+1],t.children[l]));t.parent[l+1].assign(t.counts[l+1],-1);t.position[l+1].assign(t.counts[l+1],-1);
        for(size_t p=0;p<t.children[l].size();++p)for(size_t z=0;z<t.children[l][p].size();++z){int child=t.children[l][p][z];t.parent[l+1][child]=p;t.position[l+1][child]=z;}}
    t.position[0].resize(t.counts[0]);std::iota(t.position[0].begin(),t.position[0].end(),0);t.encoded[0]=t.center[0];
    for(int l=1;l<t.depth;++l){t.encoded[l].d=t.d;t.encoded[l].x.resize((size_t)t.counts[l]*t.d);for(int id=0;id<t.counts[l];++id)for(int j=0;j<t.d;++j)t.encoded[l].x[(size_t)id*t.d+j]=t.center[l][id][j]-t.center[l-1][t.parent[l][id]][j];}
    Matrix empty;t.codec.reserve(t.depth);for(int l=0;l<t.depth;++l){uint64_t seed=c.seed+t.counts[l]+(l==last&&l>0?(uint64_t)t.counts[l]/t.counts[l-1]:0);std::cerr<<"Codec level="<<l<<' '<<c.modes[l]<<"...\n";t.codec.emplace_back(new CRCodec(c.modes[l],t.encoded[l],empty,std::min(c.pq_train,t.counts[l]),c.pq_iters,seed));}
    std::vector<double>leaf_r2(K);for(int f=0;f<K;++f){double sum=0;for(int id:t.lists[f])sum+=l2(xb[id],t.center[last][f],t.d);leaf_r2[f]=t.lists[f].empty()?0:sum/t.lists[f].size();}
    for(int l=0;l<t.depth;++l)t.cover[l].assign(t.counts[l],0);std::vector<std::vector<double>>weight(t.depth);for(int l=0;l<t.depth;++l)weight[l].assign(t.counts[l],0);
    for(int f=0;f<K;++f){double w=std::max<size_t>(1,t.lists[f].size());t.cover[last][f]+=leaf_r2[f]*w;weight[last][f]+=w;int a=f;for(int l=last-1;l>=0;--l){a=t.parent[l+1][a];double v=l2(t.center[last][f],t.center[l][a],t.d)+leaf_r2[f];t.cover[l][a]+=v*w;weight[l][a]+=w;}}
    t.level_cover.resize(t.depth);for(int l=0;l<t.depth;++l){for(int id=0;id<t.counts[l];++id)t.cover[l][id]=std::sqrt(t.cover[l][id]/std::max(1.0,weight[l][id]));auto values=t.cover[l];std::nth_element(values.begin(),values.begin()+values.size()/2,values.end());t.level_cover[l]=values[values.size()/2];}
    t.leaf_unit=t.d/2.0+32.0+4.0+8.0;t.static_charge=96.0;for(const auto&x:t.codec)t.static_charge+=x->parameter_bytes();t.static_charge+=t.counts[0]*(double)t.codec[0]->code_bytes();
    t.build_s=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();return t;
}

struct BAction {
    int level=0,id=0;double score=0;
    int decoded_slot=-1,parent_slot=-1;
};
struct BActionCmp {bool operator()(const BAction&a,const BAction&b)const{if(a.score!=b.score)return a.score>b.score;if(a.level!=b.level)return a.level<b.level;return a.id>b.id;}};

static double b_action_cost(const BIndex&t,int level,int id){
    int siblings=level? t.children[level-1][t.parent[level][id]].size():t.counts[0];double proof=32.0*b_log2ceil(siblings),meta=level==t.depth-1?4.0:68.0;
    if(level==t.depth-1)return 4.0+meta+proof+t.lists[id].size()*t.leaf_unit;
    return 4.0+meta+proof+t.children[level][id].size()*(double)t.codec[level+1]->code_bytes();
}

static double b_score_distance(const BIndex&t,double squared_distance,int level,int id,double alpha,double cost_lambda,bool per_node_radius){
    // Typical-radius correction is only a look-ahead for unopened subtrees.
    // Rewarding a wide leaf makes leaf ranking worse and wastes object VO.
    double correction=level<t.depth-1?alpha*(per_node_radius?t.cover[level][id]:t.level_cover[level]):0.0;
    double distance=std::sqrt(std::max(0.0,squared_distance)),geometric=std::max(0.0,distance-correction);
    return geometric+cost_lambda*std::log1p(b_action_cost(t,level,id)/1024.0);
}

struct BTrace {VLState leaf;std::vector<std::vector<int>>selected;std::vector<std::pair<int,int>>actions;double charged=0;bool forced=false;};
struct BStates {std::vector<BTrace>trace;double recall=0,query_ms=0,charged=0,visited=0;std::vector<double>expanded;int forced=0;};

static BStates b_query(const BIndex&t,const Matrix&q,double budget,const BCfg&c,const std::vector<std::vector<int>>*gt){
    BStates out;out.trace.resize(q.rows());out.expanded.assign(t.depth,0);uint64_t hits=0;auto begin=std::chrono::steady_clock::now();
    for(size_t qi=0;qi<q.rows();++qi){auto&tr=out.trace[qi];tr.selected.resize(t.depth);tr.charged=t.static_charge;std::priority_queue<BAction,std::vector<BAction>,BActionCmp>frontier;std::vector<float>decoded_pool;decoded_pool.reserve((size_t)(t.counts[0]+1024)*t.d);
        for(int id=0;id<t.counts[0];++id){auto decoded=t.codec[0]->decode(t.encoded[0],id);int slot=decoded_pool.size()/t.d;decoded_pool.insert(decoded_pool.end(),decoded.begin(),decoded.end());double distance=l2(q[qi],decoded_pool.data()+(size_t)slot*t.d,t.d);frontier.push({0,id,b_score_distance(t,distance,0,id,c.radius_alpha,c.cost_lambda,c.per_node_radius),slot,-1});}
        size_t candidate_objects=0;while(!frontier.empty()){const auto&next=frontier.top();double charge=b_action_cost(t,next.level,next.id);if(tr.charged+charge>budget&&candidate_objects>=(size_t)c.k)break;if(tr.charged+charge>budget)tr.forced=true;
            BAction u=frontier.top();frontier.pop();tr.charged+=charge;tr.selected[u.level].push_back(u.id);tr.actions.push_back({u.level,u.id});
            if(u.level==t.depth-1){tr.leaf.lists.push_back(u.id);candidate_objects+=t.lists[u.id].size();continue;}
            if(u.decoded_slot<0){auto residual=t.codec[u.level]->decode(t.encoded[u.level],u.id);size_t parent_offset=(size_t)u.parent_slot*t.d;u.decoded_slot=decoded_pool.size()/t.d;size_t old=decoded_pool.size();decoded_pool.resize(old+t.d);for(int j=0;j<t.d;++j)decoded_pool[old+j]=decoded_pool[parent_offset+j]+residual[j];}
            const float*decoded=decoded_pool.data()+(size_t)u.decoded_slot*t.d;std::vector<float>qr(t.d);for(int j=0;j<t.d;++j)qr[j]=q[qi][j]-decoded[j];auto lut=t.codec[u.level+1]->prepare(qr.data());
            for(int child:t.children[u.level][u.id]){double distance=t.codec[u.level+1]->distance(qr.data(),lut,t.encoded[u.level+1],child,false);double score=b_score_distance(t,distance,u.level+1,child,c.radius_alpha,c.cost_lambda,c.per_node_radius);frontier.push({u.level+1,child,score,-1,u.decoded_slot});}}
        if(candidate_objects<(size_t)c.k)throw std::runtime_error("efVO traversal produced fewer than k candidates");std::priority_queue<std::pair<float,int>>top;
        for(int f:tr.leaf.lists)for(int id:t.lists[f]){float d=l2(q[qi],(*t.xb)[id],t.d);if((int)top.size()<c.k)top.push({d,id});else if(d<top.top().first||(d==top.top().first&&id<top.top().second)){top.pop();top.push({d,id});}}
        tr.leaf.tau2=top.top().first;while(!top.empty()){tr.leaf.answer.push_back(top.top().second);top.pop();}if(gt){std::unordered_set<int>a(tr.leaf.answer.begin(),tr.leaf.answer.end());for(int id:(*gt)[qi])hits+=a.count(id);}
        out.charged+=tr.charged;out.visited+=candidate_objects;out.forced+=tr.forced;for(int l=0;l<t.depth;++l)out.expanded[l]+=tr.selected[l].size();}
    out.query_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/q.rows();out.recall=gt?hits/(q.rows()*(double)c.k):0;out.charged/=q.rows();out.visited/=q.rows();for(double&x:out.expanded)x/=q.rows();return out;
}

struct BRoute {double parameters=0,fixed=0,huffman=0,sequence=0;};

static BRoute b_route(const BIndex&t,const BStates&s){
    BRoute out;out.parameters=96.0;for(const auto&x:t.codec)out.parameters+=x->parameter_bytes();double fixed=out.parameters+t.counts[0]*(double)t.codec[0]->code_bytes(),huff=out.parameters+16.0;
    std::vector<int>all_root(t.counts[0]);std::iota(all_root.begin(),all_root.end(),0);for(const auto&x:t.codec)huff+=x->huffman_model_bytes();huff+=t.codec[0]->huffman_bytes(all_root);double variable=0,hvariable=0;
    for(const auto&tr:s.trace){double shared=tr.actions.size()*4.0;out.sequence+=tr.actions.size()*4.0;for(int l=0;l<t.depth;++l){shared+=tr.selected[l].size()*(l==t.depth-1?4.0:68.0);if(l==0){shared+=hv_dummy_proof_count(t.counts[0],tr.selected[0])*32.0;}else{
                    std::vector<std::vector<int>>picked(t.counts[l-1]);for(int id:tr.selected[l])picked[t.parent[l][id]].push_back(t.position[l][id]);for(size_t p=0;p<picked.size();++p)if(!picked[p].empty())shared+=hv_dummy_proof_count(t.children[l-1][p].size(),picked[p])*32.0;}}
            double blocks=0,hblocks=0;for(int l=0;l<t.depth-1;++l)for(int parent:tr.selected[l]){blocks+=t.children[l][parent].size()*(double)t.codec[l+1]->code_bytes();hblocks+=t.codec[l+1]->huffman_bytes(t.children[l][parent]);}
            variable+=shared+blocks;hvariable+=shared+hblocks;}
    double nq=s.trace.size();out.sequence/=nq;out.fixed=fixed+variable/nq;out.huffman=huff+hvariable/nq;return out;
}

static int b_prefix_violations(const BStates&a,const BStates&b){int bad=0;for(size_t qi=0;qi<a.trace.size();++qi){const auto&x=a.trace[qi].actions;const auto&y=b.trace[qi].actions;if(x.size()>y.size()||!std::equal(x.begin(),x.end(),y.begin()))++bad;}return bad;}

static std::vector<VLState> b_leaf_states(const BStates&s){std::vector<VLState>v;v.reserve(s.trace.size());for(const auto&t:s.trace)v.push_back(t.leaf);return v;}

#ifndef BUDGETED_RESIDUAL_VO_NO_MAIN
int main(int argc,char**argv)try{
    auto c=b_args(argc,argv);std::cerr<<"Loading data...\n";Matrix xb=fvecs(c.data+"/sift_base.fvecs",c.base),trainq=fvecs(c.data+"/sift_learn.fvecs",c.train_queries),testq=fvecs(c.data+"/sift_query.fvecs",c.test_queries);auto tree=b_build(xb,c);std::cerr<<"Exact ground truth...\n";auto calgt=exact_gt(xb,trainq,c.k),gt=exact_gt(xb,testq,c.k);
    std::ofstream out(c.output);if(!out)throw std::runtime_error("cannot open output");out<<std::setprecision(12);out<<"base,depth,counts,codecs,efvo_kib,radius_alpha,cost_lambda,cal_recall,recall,charged_kib,actual_route_kib,leaf_kib,total_kib,avg_visited,expanded_by_level,ann_query_ms,vo_generation_ms,index_build_s,forced_queries,prefix_violations,object_violations\n";
    BStates previous;bool have_previous=false;for(double budget_kib:c.budgets_kib){double budget=budget_kib*1024.0;std::cerr<<"efVO="<<budget_kib<<" KiB...\n";auto train=b_query(tree,trainq,budget,c,&calgt),test=b_query(tree,testq,budget,c,&gt);int prefix=have_previous?b_prefix_violations(previous,test):0;previous=test;have_previous=true;
        auto train_leaf=b_leaf_states(train),test_leaf=b_leaf_states(test);auto boundaries=verification_tree_bounds(xb,trainq,tree.lists,train_leaf,0,c.train_queries);NestedRule quantizer(xb,std::move(boundaries));if(!quantizer.contains(xb))throw std::runtime_error("quantizer containment failure");auto tail=hv_lossless_tail(xb,quantizer);
        HVLayout identity;identity.name="identity";identity.routing_position.resize(tree.lists.size());std::iota(identity.routing_position.begin(),identity.routing_position.end(),0);int split=c.train_queries/2;auto order=hv_downgrade_order(xb,trainq,train_leaf,0,split,tree.lists,quantizer);std::vector<int>choices{0,8,16,24,32,48,64,80,96,112,128};HVProtocol independent;independent.lossless_tail=true;independent.positional_ids=true;independent.public_dummy=true;int best_count=0;double best=std::numeric_limits<double>::infinity();
        for(int count:choices){auto bits=hv_downgrade_bits(xb.d,order,count);auto e=hv_protocol_evaluate(xb,trainq,train_leaf,split,c.train_queries,tree.lists,quantizer,bits,identity,tail,independent,1,false);double leaf=e.total-e.routing;if(leaf<best){best=leaf;best_count=count;}}
        auto full_order=hv_downgrade_order(xb,trainq,train_leaf,0,c.train_queries,tree.lists,quantizer);auto bits=hv_downgrade_bits(xb.d,full_order,best_count);auto ie=hv_protocol_evaluate(xb,trainq,train_leaf,0,c.train_queries,tree.lists,quantizer,bits,identity,tail,independent,1,false);HVProtocol fused=independent;fused.fused_commitment=true;auto fe=hv_protocol_evaluate(xb,trainq,train_leaf,0,c.train_queries,tree.lists,quantizer,bits,identity,tail,fused,1,false);const auto&protocol=(fe.total-fe.routing)<(ie.total-ie.routing)?fused:independent;
        auto leaf=hv_protocol_evaluate(xb,testq,test_leaf,0,c.test_queries,tree.lists,quantizer,bits,identity,tail,protocol,c.repeats,true);double leaf_total=leaf.total-leaf.routing;auto route=b_route(tree,test);double leaf_parameters=cr_leaf_parameter_bytes(quantizer,bits);route.fixed+=leaf_parameters;route.huffman+=leaf_parameters;double routing=std::min(route.fixed,route.huffman),total=routing+leaf_total;
        std::ostringstream counts,codecs,expanded;for(size_t i=0;i<c.counts.size();++i){if(i){counts<<'/';codecs<<'/';expanded<<'/';}counts<<c.counts[i];codecs<<c.modes[i];expanded<<test.expanded[i];}
        out<<c.base<<','<<tree.depth<<','<<counts.str()<<','<<codecs.str()<<','<<budget_kib<<','<<c.radius_alpha<<','<<c.cost_lambda<<','<<train.recall<<','<<test.recall<<','<<test.charged/1024.0<<','<<routing/1024.0<<','<<leaf_total/1024.0<<','<<total/1024.0<<','<<test.visited<<','<<expanded.str()<<','<<test.query_ms<<','<<leaf.ms<<','<<tree.build_s<<','<<test.forced<<','<<prefix<<','<<leaf.object_violations<<'\n';
        std::cout<<"efVO="<<budget_kib<<" cal_recall="<<train.recall<<" recall="<<test.recall<<" charged="<<test.charged/1024<<" KiB actual="<<total/1024<<" KiB visited="<<test.visited<<" expanded="<<expanded.str()<<" query="<<test.query_ms<<" ms forced="<<test.forced<<" prefix_bad="<<prefix<<" violations="<<leaf.object_violations<<'\n';}
    struct rusage ru{};getrusage(RUSAGE_SELF,&ru);std::cout<<"build="<<tree.build_s<<" s peak="<<ru.ru_maxrss/1024.0<<" MiB\n";return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
#endif

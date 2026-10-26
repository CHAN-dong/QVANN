#define COMPLETE_ROUTING_VO_NO_MAIN
#include "complete_routing_vo.cpp"
#undef COMPLETE_ROUTING_VO_NO_MAIN

#include <sys/resource.h>

// Proof-aware adaptive authentication tree experiment.
// Construction uses only base-data geometry; independent learn queries are
// reserved for mapping a requested Recall target to gamma.

struct AATCfg {
    std::string data="../dataset/sift", output="adaptive_auth_tree.csv", codec="pq16";
    int base=100000, build_samples=64, cal_queries=160, test_queries=300, k=100;
    int atomic_bytes=2048, iters=3, pq_train=12000, pq_iters=4;
    int merge_dims=4, merge_window=4, max_fanout=64;
    double margin=0.001, dp_tau_inflate=1.10, dp_min_gain=0.02;
    uint64_t seed=20260831;
    std::vector<double> targets{0.90,0.95,0.99};
};

static std::vector<double> aat_doubles(const std::string&s){
    std::vector<double>v;std::stringstream z(s);std::string x;
    while(std::getline(z,x,','))v.push_back(std::stod(x));return v;
}

static AATCfg aat_args(int ac,char**av){
    AATCfg c;for(int i=1;i<ac;++i){std::string a=av[i];auto v=[&](){if(++i==ac)throw std::runtime_error("missing value for "+a);return std::string(av[i]);};
        if(a=="--dataset")c.data=v();else if(a=="--output")c.output=v();else if(a=="--codec")c.codec=v();
        else if(a=="--base")c.base=std::stoi(v());else if(a=="--build-samples")c.build_samples=std::stoi(v());
        else if(a=="--cal-queries")c.cal_queries=std::stoi(v());else if(a=="--test-queries")c.test_queries=std::stoi(v());
        else if(a=="--k")c.k=std::stoi(v());else if(a=="--atomic-bytes")c.atomic_bytes=std::stoi(v());
        else if(a=="--iters")c.iters=std::stoi(v());
        else if(a=="--pq-train")c.pq_train=std::stoi(v());else if(a=="--pq-iters")c.pq_iters=std::stoi(v());
        else if(a=="--merge-dims")c.merge_dims=std::stoi(v());else if(a=="--merge-window")c.merge_window=std::stoi(v());
        else if(a=="--max-fanout")c.max_fanout=std::stoi(v());
        else if(a=="--margin")c.margin=std::stod(v());else if(a=="--dp-tau-inflate")c.dp_tau_inflate=std::stod(v());
        else if(a=="--dp-min-gain")c.dp_min_gain=std::stod(v());else if(a=="--seed")c.seed=std::stoull(v());
        else if(a=="--targets")c.targets=aat_doubles(v());else throw std::runtime_error("unknown option "+a);
    }
    if(c.base<c.k||c.build_samples<1||c.cal_queries<1||c.test_queries<1||c.atomic_bytes<72||
       c.iters<1||c.merge_dims<1||c.merge_window<1||c.max_fanout<2||c.margin<0)
        throw std::runtime_error("invalid configuration");
    for(double x:c.targets)if(x<=0||x>1)throw std::runtime_error("target must be in (0,1]");
    return c;
}

struct AATNode {
    std::vector<int> members;
    std::vector<float> center,decoded;
    float radius=0,qerr=0;
    int left=-1,right=-1;
    bool atomic()const{return left<0;}
};

struct AATTree {
    std::vector<AATNode> node;
    int root=-1,micro=0,atomic_cap=0;
    double micro_ms=0,merge_ms=0,codec_ms=0;
};

static std::vector<float> aat_center(const Matrix&x,const std::vector<int>&ids){
    std::vector<float>c(x.d);for(int id:ids)for(int j=0;j<x.d;++j)c[j]+=x[id][j];
    for(float&v:c)v/=ids.size();return c;
}

static float aat_radius(const Matrix&x,const std::vector<int>&ids,const std::vector<float>&c){
    double r=0;for(int id:ids)r=std::max(r,std::sqrt((double)l2(x[id],c.data(),x.d)));
    return std::nextafter((float)(r*(1.0+1e-6)+1e-5),std::numeric_limits<float>::infinity());
}

static int aat_next_pow2(int x){int p=1;while(p<x&&p<=std::numeric_limits<int>::max()/2)p*=2;return p;}

static AATNode aat_parent(const AATNode&a,const AATNode&b,int left,int right){
    AATNode p;p.left=left;p.right=right;const double na=a.members.size(),nb=b.members.size();p.center.resize(a.center.size());
    for(size_t j=0;j<p.center.size();++j)p.center[j]=(float)((na*a.center[j]+nb*b.center[j])/(na+nb));
    double ra=std::sqrt((double)l2(p.center.data(),a.center.data(),p.center.size()))+a.radius;
    double rb=std::sqrt((double)l2(p.center.data(),b.center.data(),p.center.size()))+b.radius;
    p.radius=std::nextafter((float)(std::max(ra,rb)*(1.0+1e-6)+1e-5),std::numeric_limits<float>::infinity());
    p.members.reserve(a.members.size()+b.members.size());p.members.insert(p.members.end(),a.members.begin(),a.members.end());p.members.insert(p.members.end(),b.members.begin(),b.members.end());
    return p;
}

struct AATEdge {double score=0;int a=-1,b=-1;};

static AATTree aat_build_tree(const Matrix&x,const AATCfg&cfg,int descriptor_bytes){
    AATTree t;const int object_summary=(x.d+1)/2+8;t.atomic_cap=std::max(1,cfg.atomic_bytes/object_summary);
    int wanted=(int)((x.rows()+t.atomic_cap-1)/t.atomic_cap);t.micro=std::min<int>(x.rows(),aat_next_pow2(wanted));
    auto begin=std::chrono::steady_clock::now();auto lists=hkmeans_balanced(x,t.micro,cfg.iters);
    for(auto&ids:lists){AATNode n;n.members=std::move(ids);n.center=aat_center(x,n.members);n.radius=aat_radius(x,n.members,n.center);t.node.push_back(std::move(n));}
    t.micro_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    begin=std::chrono::steady_clock::now();std::vector<int>active(t.micro);std::iota(active.begin(),active.end(),0);
    double global_scale=0;for(int id:active)global_scale+=l2(t.node[id].center.data(),t.node[active[0]].center.data(),x.d);
    global_scale=std::sqrt(global_scale/std::max<size_t>(1,active.size()));if(global_scale<1e-9)global_scale=1;
    while(active.size()>1){
        std::vector<double>mean(x.d),var(x.d);double total=0;for(int id:active){double w=t.node[id].members.size();total+=w;for(int j=0;j<x.d;++j)mean[j]+=w*t.node[id].center[j];}
        for(double&v:mean)v/=total;for(int id:active){double w=t.node[id].members.size();for(int j=0;j<x.d;++j){double z=t.node[id].center[j]-mean[j];var[j]+=w*z*z;}}
        std::vector<int>dims(x.d);std::iota(dims.begin(),dims.end(),0);int nd=std::min(cfg.merge_dims,x.d);std::partial_sort(dims.begin(),dims.begin()+nd,dims.end(),[&](int a,int b){return var[a]>var[b];});dims.resize(nd);
        std::unordered_set<uint64_t>seen;std::vector<AATEdge>edges;edges.reserve(active.size()*nd*cfg.merge_window);
        auto add=[&](int a,int b){if(a==b)return;if(a>b)std::swap(a,b);uint64_t key=((uint64_t)(uint32_t)a<<32)|(uint32_t)b;if(!seen.insert(key).second)return;
            const auto&A=t.node[a];const auto&B=t.node[b];double na=A.members.size(),nb=B.members.size();std::vector<float>c(x.d);for(int j=0;j<x.d;++j)c[j]=(float)((na*A.center[j]+nb*B.center[j])/(na+nb));
            double r=std::max(std::sqrt((double)l2(c.data(),A.center.data(),x.d))+A.radius,std::sqrt((double)l2(c.data(),B.center.data(),x.d))+B.radius);
            double balance=std::abs(std::log2(na/nb));double byte_term=descriptor_bytes/(double)(object_summary*(na+nb));edges.push_back({r/global_scale+0.05*balance+byte_term,a,b});};
        for(int dim:dims){auto order=active;std::sort(order.begin(),order.end(),[&](int a,int b){float u=t.node[a].center[dim],v=t.node[b].center[dim];return u==v?a<b:u<v;});
            for(size_t i=0;i<order.size();++i)for(int z=1;z<=cfg.merge_window&&i+z<order.size();++z)add(order[i],order[i+z]);}
        std::sort(edges.begin(),edges.end(),[](const auto&a,const auto&b){return a.score==b.score?(a.a==b.a?a.b<b.b:a.a<b.a):a.score<b.score;});
        std::unordered_set<int>used;std::vector<std::pair<int,int>>pairs;for(const auto&e:edges)if(!used.count(e.a)&&!used.count(e.b)){used.insert(e.a);used.insert(e.b);pairs.push_back({e.a,e.b});}
        std::vector<int>leftover;for(int id:active)if(!used.count(id))leftover.push_back(id);std::sort(leftover.begin(),leftover.end(),[&](int a,int b){float u=t.node[a].center[dims[0]],v=t.node[b].center[dims[0]];return u==v?a<b:u<v;});
        for(size_t i=0;i+1<leftover.size();i+=2)pairs.push_back({leftover[i],leftover[i+1]});
        std::vector<int>next;next.reserve((active.size()+1)/2);for(auto [a,b]:pairs){int id=t.node.size();t.node.push_back(aat_parent(t.node[a],t.node[b],a,b));next.push_back(id);}
        if(leftover.size()%2)next.push_back(leftover.back());active.swap(next);
    }
    t.root=active[0];t.merge_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return t;
}

static float aat_lb(const float*q,const AATNode&n,int d){
    double z=std::sqrt((double)l2(q,n.decoded.data(),d))-n.qerr-n.radius;return z>0?(float)z:0.0f;
}

static std::vector<int> aat_sample_ids(size_t n,int count){
    count=std::min<int>(count,n);std::vector<int>v(count);for(int i=0;i<count;++i)v[i]=(uint64_t)(2*i+1)*n/(2*count);return v;
}

static std::vector<double> aat_sample_tau(const Matrix&x,const std::vector<int>&qid,int k){
    std::vector<double>tau(qid.size());for(size_t qi=0;qi<qid.size();++qi){std::priority_queue<float>h;for(size_t id=0;id<x.rows();++id)if((int)id!=qid[qi]){float z=l2(x[qid[qi]],x[id],x.d);if((int)h.size()<k)h.push(z);else if(z<h.top()){h.pop();h.push(z);}}tau[qi]=std::sqrt(h.top());}return tau;
}

struct AATSample {int q=0;double gamma=1;};
struct AATPrune {
    std::vector<std::vector<int>> child;
    double ms=0;
    int effective_leaves=0,internal=0,max_depth=0;
    double avg_depth=0;
};

static AATPrune aat_prune_tree(const Matrix&x,AATTree&t,const AATCfg&cfg,int descriptor_bytes){
    auto begin=std::chrono::steady_clock::now();auto qid=aat_sample_ids(x.rows(),cfg.build_samples);auto tau=aat_sample_tau(x,qid,cfg.k);int nq=qid.size();
    // Target-independent gamma portfolio avoids overfitting the topology to
    // only high-gamma/exact queries.
    std::vector<AATSample>root_work;for(int qi=0;qi<nq;++qi)for(double g:{0.0,0.2,0.4,0.6,0.8,1.0})root_work.push_back({qi,g});
    AATPrune out;out.child.resize(t.node.size());
    auto plan=[&](auto&&self,int u,const std::vector<AATSample>&work)->std::vector<int>{
        const auto&n=t.node[u];if(n.atomic())return {u};
        auto left_flat=self(self,n.left,work),right_flat=self(self,n.right,work);std::vector<int>flat=left_flat;flat.insert(flat.end(),right_flat.begin(),right_flat.end());
        std::vector<AATSample>active;active.reserve(work.size());for(const auto&s:work){double th=s.gamma*tau[s.q]*cfg.dp_tau_inflate;
            if(aat_lb(x[qid[s.q]],n,x.d)<=th)active.push_back(s);}
        double collapse_cost=work.size()*(double)flat.size()*descriptor_bytes;
        double keep_cost=work.size()*(double)descriptor_bytes+active.size()*(double)flat.size()*descriptor_bytes;
        bool retain=flat.size()>(size_t)std::max(2,cfg.max_fanout/2)||keep_cost<collapse_cost*(1.0-cfg.dp_min_gain);
        if(!retain){out.child[u].clear();return flat;}
        auto left_body=self(self,n.left,active),right_body=self(self,n.right,active);out.child[u]=std::move(left_body);out.child[u].insert(out.child[u].end(),right_body.begin(),right_body.end());
        return {u};
    };
    const auto&root=t.node[t.root];auto L=plan(plan,root.left,root_work),R=plan(plan,root.right,root_work);out.child[t.root]=std::move(L);out.child[t.root].insert(out.child[t.root].end(),R.begin(),R.end());
    if(out.child[t.root].size()>(size_t)cfg.max_fanout)throw std::runtime_error("root fanout cap violated");
    auto count=[&](auto&&self,int u,int depth)->std::pair<long long,long long>{out.max_depth=std::max(out.max_depth,depth);
        if(out.child[u].empty()){if(!t.node[u].atomic())throw std::runtime_error("reachable collapsed internal node");++out.effective_leaves;return {depth,1};}
        ++out.internal;long long sum=0,n=0;for(int v:out.child[u]){auto z=self(self,v,depth+1);sum+=z.first;n+=z.second;}return {sum,n};};
    auto depths=count(count,t.root,0);out.avg_depth=depths.second?depths.first/(double)depths.second:0;out.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return out;
}

struct AATCommit {std::vector<Hash>body,commit;Hash root{};double ms=0;};

static Hash aat_header_hash(const AATNode&n,const Hash&body){
    std::vector<uint8_t>b(n.decoded.size()*sizeof(float)+sizeof(float)*2+sizeof(uint32_t)+32);size_t o=0;
    std::memcpy(b.data()+o,n.decoded.data(),n.decoded.size()*sizeof(float));o+=n.decoded.size()*sizeof(float);
    std::memcpy(b.data()+o,&n.radius,sizeof(float));o+=sizeof(float);std::memcpy(b.data()+o,&n.qerr,sizeof(float));o+=sizeof(float);
    uint32_t count=n.members.size();std::memcpy(b.data()+o,&count,sizeof(count));o+=sizeof(count);std::memcpy(b.data()+o,body.data(),32);return sha(b.data(),b.size());
}

static AATCommit aat_commit(const Matrix&x,const AATTree&t,const std::vector<std::vector<int>>&child){
    auto begin=std::chrono::steady_clock::now();std::vector<Hash>object(x.rows());std::vector<uint8_t>buf(sizeof(uint32_t)+x.d*sizeof(float));
    for(size_t id=0;id<x.rows();++id){uint32_t z=id;std::memcpy(buf.data(),&z,4);std::memcpy(buf.data()+4,x[id],x.d*sizeof(float));object[id]=sha(buf.data(),buf.size());}
    AATCommit out;out.body.resize(t.node.size());out.commit.resize(t.node.size());auto rec=[&](auto&&self,int u)->Hash{const auto&n=t.node[u];
        if(!child[u].empty()){std::vector<Hash>v;v.reserve(child[u].size());for(int z:child[u])v.push_back(self(self,z));out.body[u]=merkle(std::move(v));}
        else{if(!n.atomic())throw std::runtime_error("commit reached collapsed internal node");std::vector<Hash>v;v.reserve(n.members.size());for(int id:n.members)v.push_back(object[id]);out.body[u]=merkle(std::move(v));}
        out.commit[u]=aat_header_hash(n,out.body[u]);return out.commit[u];};out.root=rec(rec,t.root);out.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return out;
}

struct AATEntry {float lb=0,center_distance=0;int node=0;};
struct AATGreater {bool operator()(const AATEntry&a,const AATEntry&b)const{return a.lb==b.lb?(a.center_distance==b.center_distance?a.node>b.node:a.center_distance>b.center_distance):a.lb>b.lb;}};

struct AATRun {
    double recall=0,recall_se=0,recall_lcb=0,query_ms=0,vo_ms=0,verify_ms=0,total_vo=0,route_vo=0,leaf_vo=0;
    double expanded=0,blocks=0,scanned=0,exact_open=0,stop_slack=0;
    uint64_t transcript_violations=0;
};

static AATRun aat_run(const Matrix&x,const Matrix&q,const std::vector<std::vector<int>>&gt,const AATTree&t,const std::vector<std::vector<int>>&child,
        const AATCommit&commit,const CRCodec&codec,const SQ4&object_sq,double gamma,int k,int descriptor_bytes){
    AATRun out;uint64_t hits=0;double recall_sum=0,recall_sq=0,query_ns=0,vo_ns=0,verify_ns=0;uint8_t hash_sink=0;const int object_record=(x.d+1)/2+8,exact_record=x.d*4+4;
    const double parameters=96.0+codec.parameter_bytes()+x.d*8.0+descriptor_bytes+16.0;
    for(size_t qi=0;qi<q.rows();++qi){auto qbegin=std::chrono::steady_clock::now();std::priority_queue<AATEntry,std::vector<AATEntry>,AATGreater>frontier;
        frontier.push({0,0,t.root});std::priority_queue<std::pair<float,int>>top;std::vector<int>expanded,blocks;double stop_lb=0;
        while(!frontier.empty()){
            if((int)top.size()==k){double tau=std::sqrt((double)top.top().first);if(frontier.top().lb>=gamma*tau){stop_lb=frontier.top().lb;break;}}
            auto e=frontier.top();frontier.pop();const auto&n=t.node[e.node];if(!child[e.node].empty()){expanded.push_back(e.node);for(int z:child[e.node]){
                    const auto&c=t.node[z];float cd=std::sqrt(std::max(0.0f,l2(q[qi],c.decoded.data(),x.d)));frontier.push({aat_lb(q[qi],c,x.d),cd,z});}}
            else{blocks.push_back(e.node);for(int id:n.members){float d=l2(q[qi],x[id],x.d);if((int)top.size()<k)top.push({d,id});else if(d<top.top().first||(d==top.top().first&&id<top.top().second)){top.pop();top.push({d,id});}}}
        }
        if((int)top.size()!=k)throw std::runtime_error("query produced fewer than k answers");double tau=std::sqrt((double)top.top().first);query_ns+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-qbegin).count();
        std::unordered_set<int>answer;auto h=top;while(!h.empty()){answer.insert(h.top().second);h.pop();}int query_hits=0;for(int id:gt[qi])query_hits+=answer.count(id);
        hits+=query_hits;double query_recall=query_hits/(double)k;recall_sum+=query_recall;recall_sq+=query_recall*query_recall;
        auto vbegin=std::chrono::steady_clock::now();double routing_records=0;for(int u:expanded)routing_records+=child[u].size();
        double route=parameters+routing_records*descriptor_bytes,leaf=0,opened_total=0,scanned=0;
        std::vector<std::vector<int>>opened(blocks.size());for(size_t bi=0;bi<blocks.size();++bi){const auto&n=t.node[blocks[bi]];scanned+=n.members.size();auto&o=opened[bi];
            for(size_t p=0;p<n.members.size();++p)if(object_sq.lb(q[qi],n.members[p])<=tau)o.push_back(p);opened_total+=o.size();
            leaf+=n.members.size()*(double)object_record+o.size()*(double)exact_record+proof_count(n.members.size(),o)*32.0+16.0;}
        vo_ns+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-vbegin).count();
        auto verify=std::chrono::steady_clock::now();for(int u:expanded){std::vector<Hash>headers;headers.reserve(child[u].size());for(int z:child[u])headers.push_back(aat_header_hash(t.node[z],commit.body[z]));
            Hash body=merkle(std::move(headers));if(body!=commit.body[u])++out.transcript_violations;hash_sink^=body[0];}
        std::vector<uint8_t>exact_buf(sizeof(uint32_t)+x.d*sizeof(float));for(size_t bi=0;bi<blocks.size();++bi){const auto&n=t.node[blocks[bi]];for(int p:opened[bi]){int id=n.members[p];uint32_t z=id;std::memcpy(exact_buf.data(),&z,4);std::memcpy(exact_buf.data()+4,x[id],x.d*sizeof(float));auto hh=sha(exact_buf.data(),exact_buf.size());hash_sink^=hh[0];}
            Hash dummy{};for(size_t z=0;z<proof_count(n.members.size(),opened[bi]);++z){dummy=pair_hash(dummy,dummy);hash_sink^=dummy[0];}}
        if(stop_lb+1e-5<gamma*tau&&!frontier.empty())++out.transcript_violations;verify_ns+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-verify).count();
        out.route_vo+=route;out.leaf_vo+=leaf;out.total_vo+=route+leaf;out.expanded+=expanded.size();out.blocks+=blocks.size();out.scanned+=scanned;out.exact_open+=opened_total;out.stop_slack+=stop_lb-gamma*tau;
    }
    if(hash_sink==255)std::cerr<<"hash sink\n";double nq=q.rows();out.recall=hits/(nq*k);double variance=nq>1?std::max(0.0,(recall_sq-recall_sum*recall_sum/nq)/(nq-1)):0;
    out.recall_se=std::sqrt(variance/nq);out.recall_lcb=std::max(0.0,out.recall-2.326347874*out.recall_se);
    out.query_ms=query_ns/nq/1e6;out.vo_ms=vo_ns/nq/1e6;out.verify_ms=verify_ns/nq/1e6;
    out.route_vo/=nq;out.leaf_vo/=nq;out.total_vo/=nq;out.expanded/=nq;out.blocks/=nq;out.scanned/=nq;out.exact_open/=nq;out.stop_slack/=nq;return out;
}

static std::vector<double> aat_gamma_grid(){
    std::vector<double>g;for(int z=0;z<=10;++z)g.push_back(z/1000.0);for(int z=15;z<=100;z+=5)g.push_back(z/1000.0);
    for(int z=11;z<=20;++z)g.push_back(z/100.0);for(int z=25;z<=80;z+=5)g.push_back(z/100.0);for(int z=82;z<=90;z+=2)g.push_back(z/100.0);for(int z=91;z<=100;++z)g.push_back(z/100.0);
    std::sort(g.begin(),g.end());g.erase(std::unique(g.begin(),g.end()),g.end());
    return g;
}

static double aat_peak_mb(){struct rusage r{};getrusage(RUSAGE_SELF,&r);return r.ru_maxrss/1024.0;}

int main(int ac,char**av)try{
    auto cfg=aat_args(ac,av);auto total_begin=std::chrono::steady_clock::now();std::cerr<<"Loading base/calibration/test data...\n";
    Matrix x=fvecs(cfg.data+"/sift_base.fvecs",cfg.base),cal=fvecs(cfg.data+"/sift_learn.fvecs",cfg.cal_queries),test=fvecs(cfg.data+"/sift_query.fvecs",cfg.test_queries);
    if(x.d%32)throw std::runtime_error("current PQ codec requires dimension divisible by 32");
    int descriptor_guess=(cfg.codec=="exact"?x.d*4:16)+52;std::cerr<<"Building atomic micro-clusters and bottom-up dendrogram...\n";auto tree=aat_build_tree(x,cfg,descriptor_guess);
    std::cerr<<"Encoding "<<tree.node.size()<<" node summaries using "<<cfg.codec<<"...\n";auto cb=std::chrono::steady_clock::now();Matrix centers;centers.d=x.d;centers.x.reserve(tree.node.size()*(size_t)x.d);for(const auto&n:tree.node)centers.x.insert(centers.x.end(),n.center.begin(),n.center.end());Matrix empty;CRCodec codec(cfg.codec,centers,empty,cfg.pq_train,cfg.pq_iters,cfg.seed);
    for(size_t u=0;u<tree.node.size();++u){tree.node[u].decoded=codec.decode(centers,u);double e=std::sqrt((double)l2(tree.node[u].center.data(),tree.node[u].decoded.data(),x.d));tree.node[u].qerr=std::nextafter((float)(e*(1+1e-6)+1e-5),std::numeric_limits<float>::infinity());}
    tree.codec_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cb).count();int descriptor_bytes=codec.code_bytes()+52;
    std::cerr<<"Building object summaries and proof-aware collapse DP...\n";auto sq_begin=std::chrono::steady_clock::now();SQ4 object_sq(x);double object_sq_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-sq_begin).count();auto prune=aat_prune_tree(x,tree,cfg,descriptor_bytes);
    uint64_t radius_violations=0;double max_excess=0;for(const auto&n:tree.node)for(int id:n.members){double excess=std::sqrt((double)l2(x[id],n.center.data(),x.d))-n.radius;if(excess>1e-4){++radius_violations;max_excess=std::max(max_excess,excess);}}
    std::vector<std::vector<int>>full(tree.node.size());for(size_t u=0;u<tree.node.size();++u)if(!tree.node[u].atomic())full[u]={tree.node[u].left,tree.node[u].right};
    std::cerr<<"Building SHA-256 commitments...\n";auto pruned_commit=aat_commit(x,tree,prune.child),full_commit=aat_commit(x,tree,full);
    std::cerr<<"Computing independent exact ground truth...\n";auto cal_gt=exact_gt(x,cal,cfg.k),test_gt=exact_gt(x,test,cfg.k);auto grid=aat_gamma_grid();
    struct Variant {std::string name;const std::vector<std::vector<int>>*child;const AATCommit*commit;int leaves,internals,max_depth;double avg_depth;};
    std::vector<Variant>variants{{"adaptive",&prune.child,&pruned_commit,prune.effective_leaves,prune.internal,prune.max_depth,prune.avg_depth},{"full_dendrogram",&full,&full_commit,tree.micro,tree.micro-1,(int)std::ceil(std::log2(tree.micro)),std::log2(tree.micro)}};
    std::ofstream curve(cfg.output+".calibration.csv");curve<<std::setprecision(12)<<"variant,gamma,cal_recall,cal_recall_se,cal_recall_lcb99,total_vo,query_ms,expanded,scanned\n";
    std::ofstream out(cfg.output);if(!out)throw std::runtime_error("cannot open output");out<<std::setprecision(12);
    out<<"variant,base,dimension,target,margin,gamma,cal_recall,cal_recall_lcb99,test_recall,test_recall_se,recall_error,route_vo,leaf_vo,total_vo,query_ms,vo_generation_ms,verify_ms,avg_expanded,avg_scan_blocks,avg_scanned_objects,avg_exact_open,micro_clusters,effective_leaves,retained_internal,max_depth,avg_depth,atomic_cap,descriptor_bytes,micro_build_ms,dendrogram_ms,codec_ms,object_sq_ms,cost_dp_ms,commit_ms,total_build_ms,peak_rss_mb,radius_violations,max_radius_excess,transcript_violations,exact_mode_recall\n";
    struct Choice {double target=0,gamma=1,cal_recall=1,cal_lcb=1;};std::vector<Choice>choices;
    std::cerr<<"Calibrating adaptive gamma...\n";std::vector<AATRun>cal_runs;std::vector<double>cal_grid;double max_need=0;for(double z:cfg.targets)max_need=std::max(max_need,std::min(1.0,z+cfg.margin));
    for(double g:grid){auto r=aat_run(x,cal,cal_gt,tree,prune.child,pruned_commit,codec,object_sq,g,cfg.k,descriptor_bytes);cal_runs.push_back(r);cal_grid.push_back(g);
        curve<<"adaptive"<<','<<g<<','<<r.recall<<','<<r.recall_se<<','<<r.recall_lcb<<','<<r.total_vo<<','<<r.query_ms<<','<<r.expanded<<','<<r.scanned<<'\n';
        std::cerr<<" gamma="<<g<<" recall="<<r.recall<<" lcb99="<<r.recall_lcb<<" VO="<<r.total_vo/1024<<" KiB\n";if(r.recall_lcb>=max_need)break;}
    for(double target:cfg.targets){Choice c;c.target=target;double need=std::min(1.0,target+cfg.margin);c.cal_recall=cal_runs.back().recall;c.cal_lcb=cal_runs.back().recall_lcb;
        for(size_t i=0;i<cal_grid.size();++i)if(cal_runs[i].recall_lcb>=need){c.gamma=cal_grid[i];c.cal_recall=cal_runs[i].recall;c.cal_lcb=cal_runs[i].recall_lcb;break;}choices.push_back(c);}
    for(const auto&v:variants){auto exact=aat_run(x,test,test_gt,tree,*v.child,*v.commit,codec,object_sq,1.0,cfg.k,descriptor_bytes);
        for(const auto&c:choices){double target=c.target,chosen=c.gamma,cal_recall=c.cal_recall,cal_lcb=c.cal_lcb;
            auto r=aat_run(x,test,test_gt,tree,*v.child,*v.commit,codec,object_sq,chosen,cfg.k,descriptor_bytes);double total_build=tree.micro_ms+tree.merge_ms+tree.codec_ms+object_sq_ms+prune.ms+(v.name=="adaptive"?pruned_commit.ms:full_commit.ms);
            out<<v.name<<','<<x.rows()<<','<<x.d<<','<<target<<','<<cfg.margin<<','<<chosen<<','<<cal_recall<<','<<cal_lcb<<','<<r.recall<<','<<r.recall_se<<','<<r.recall-target<<','<<r.route_vo<<','<<r.leaf_vo<<','<<r.total_vo<<','<<r.query_ms<<','<<r.vo_ms<<','<<r.verify_ms<<','<<r.expanded<<','<<r.blocks<<','<<r.scanned<<','<<r.exact_open<<','<<tree.micro<<','<<v.leaves<<','<<v.internals<<','<<v.max_depth<<','<<v.avg_depth<<','<<tree.atomic_cap<<','<<descriptor_bytes<<','<<tree.micro_ms<<','<<tree.merge_ms<<','<<tree.codec_ms<<','<<object_sq_ms<<','<<prune.ms<<','<<(v.name=="adaptive"?pruned_commit.ms:full_commit.ms)<<','<<total_build<<','<<aat_peak_mb()<<','<<radius_violations<<','<<max_excess<<','<<r.transcript_violations<<','<<exact.recall<<'\n';
            std::cout<<v.name<<" target="<<target<<" gamma="<<chosen<<" cal="<<cal_recall<<" lcb99="<<cal_lcb<<" test="<<r.recall<<" error="<<r.recall-target<<" VO="<<r.total_vo/1024<<" KiB query="<<r.query_ms<<" ms build="<<total_build/1000<<" s exact="<<exact.recall<<" violations="<<(radius_violations+r.transcript_violations)<<'\n';}
    }
    double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-total_begin).count();std::cerr<<"Wrote "<<cfg.output<<" wall="<<wall<<" s\n";return radius_violations?2:0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

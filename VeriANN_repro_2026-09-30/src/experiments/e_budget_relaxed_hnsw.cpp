#define main relaxed_hnsw_embedded_main
#include "relaxed_hnsw_harvest.cpp"
#undef main

namespace {
using namespace vann;

class ESQ {
public:
    explicit ESQ(const HnswIndex&h):d_(h.dimension()),lo_(d_,INFINITY),span_(d_),code_(h.size()*d_){std::vector<float>hi(d_,-INFINITY);for(size_t id=0;id<h.size();++id)for(int j=0;j<d_;++j){lo_[j]=std::min(lo_[j],h.vector(id)[j]);hi[j]=std::max(hi[j],h.vector(id)[j]);}for(int j=0;j<d_;++j)span_[j]=hi[j]-lo_[j];for(size_t id=0;id<h.size();++id)for(int j=0;j<d_;++j){double u=span_[j]>0?(h.vector(id)[j]-lo_[j])/span_[j]:0;code_[id*d_+j]=uint8_t(std::clamp(int(std::floor(u*64)),0,63));}}
    float score(const float*q,int id,int bits)const{int bins=1<<bits,shift=6-bits;float s=0;for(int j=0;j<d_;++j){int p=code_[size_t(id)*d_+j]>>shift;float x=lo_[j]+span_[j]*((p+.5F)/bins),z=q[j]-x;s+=z*z;}return s;}
    float lower(const float*q,int id,int bits)const{int bins=1<<bits,shift=6-bits;float s=0;for(int j=0;j<d_;++j){int p=code_[size_t(id)*d_+j]>>shift;float a=std::nextafter(lo_[j]+span_[j]*(float(p)/bins),-INFINITY),b=std::nextafter(lo_[j]+span_[j]*(float(p+1)/bins),INFINITY);float z=q[j]<a?a-q[j]:(q[j]>b?q[j]-b:0);s+=z*z;}return std::nextafter(s,0.F);}
    float upper(const float*q,int id,int bits)const{int bins=1<<bits,shift=6-bits;float s=0;for(int j=0;j<d_;++j){int p=code_[size_t(id)*d_+j]>>shift;float a=lo_[j]+span_[j]*(float(p)/bins),b=lo_[j]+span_[j]*(float(p+1)/bins);float z=std::max(std::abs(q[j]-a),std::abs(q[j]-b));s+=z*z;}return std::nextafter(s,INFINITY);}
private:int d_;std::vector<float>lo_,span_;std::vector<uint8_t>code_;
};

struct ETrace{std::vector<int>disclosed,expanded,answer,refined,raw;std::vector<OpenedAdjacency>opened;double sq4=0,delta=0,raw_bytes=0,adjacency=0,metadata=0,route_ms=0,exact_ms=0,vo_ms=0;};

ETrace run_e(const HnswIndex&h,const ESQ&sq,const float*q,int k,int E,const std::string&mode){auto start=std::chrono::steady_clock::now();size_t n=h.size();ETrace t;std::vector<uint8_t>seen(n),expanded(n),expanded0(n),refined(n),have6(n);std::vector<float>s4(n),s6(n),lb4(n),ub4(n);std::set<std::pair<int,int>>opened;
    auto discover=[&](int id){if(!seen[id]){seen[id]=1;s4[id]=sq.score(q,id,4);lb4[id]=sq.lower(q,id,4);ub4[id]=sq.upper(q,id,4);}};
    auto refine=[&](int id){if(!have6[id]){have6[id]=1;s6[id]=sq.score(q,id,6);}refined[id]=1;};
    auto choose=[&](std::vector<int>pool){std::sort(pool.begin(),pool.end(),[&](int a,int b){return s4[a]<s4[b]||(s4[a]==s4[b]&&a<b);});if(mode=="sq4"||pool.size()==1)return pool[0];if(mode=="top2-overlap"){int a=pool[0],b=pool[1];bool overlap=lb4[a]<=ub4[b]&&lb4[b]<=ub4[a];if(!overlap)return a;refine(a);refine(b);return s6[a]<s6[b]||(s6[a]==s6[b]&&a<b)?a:b;}float min_ub=INFINITY;for(int id:pool)min_ub=std::min(min_ub,ub4[id]);std::vector<int>eligible;for(int id:pool)if(lb4[id]<=min_ub){refine(id);eligible.push_back(id);}return *std::min_element(eligible.begin(),eligible.end(),[&](int a,int b){return s6[a]<s6[b]||(s6[a]==s6[b]&&a<b);});};
    auto open=[&](int id,int level){if(opened.count({id,level}))return std::vector<int>{};if(!expanded[id]){if(int(t.expanded.size())>=E)return std::vector<int>{};expanded[id]=1;t.expanded.push_back(id);}opened.insert({id,level});t.opened.push_back({id,level});return h.neighbors(id,level);};
    int entry=h.index->hnsw.entry_point;discover(entry);bool budget=false;
    for(int level=h.index->hnsw.max_level;level>0&&!budget;--level)while(true){auto ns=open(entry,level);if(ns.empty()){budget=int(t.expanded.size())>=E;break;}for(int id:ns)discover(id);ns.push_back(entry);int next=choose(ns);if(next==entry)break;entry=next;}
    std::set<std::pair<float,int>> frontier;
    for(size_t id=0;id<n;++id)if(seen[id])frontier.insert({s4[id],int(id)});
    while(int(t.expanded.size())<E&&!frontier.empty()){
        int id=-1;
        if(mode=="sq4")id=frontier.begin()->second;
        else{std::vector<int>pool;if(mode=="top2-overlap"){auto it=frontier.begin();for(int z=0;z<2&&it!=frontier.end();++z,++it)pool.push_back(it->second);}else for(auto x:frontier)pool.push_back(x.second);id=choose(pool);}
        frontier.erase({s4[id],id});expanded0[id]=1;auto ns=open(id,0);
        for(int nb:ns){bool fresh=!seen[nb];discover(nb);if(fresh)frontier.insert({s4[nb],nb});}
    }
    for(size_t id=0;id<n;++id)if(seen[id])t.disclosed.push_back(id);t.route_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    start=std::chrono::steady_clock::now();std::vector<DistanceId>exact;for(int id:t.disclosed)exact.push_back({squared_l2(q,h.vector(id),h.dimension()),id});std::partial_sort(exact.begin(),exact.begin()+k,exact.end(),better);exact.resize(k);float tau=exact.back().distance;for(auto z:exact)t.answer.push_back(z.id);t.exact_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    start=std::chrono::steady_clock::now();for(int id:t.disclosed)if(lb4[id]<=tau){refine(id);t.refined.push_back(id);if(sq.lower(q,id,6)<=tau)t.raw.push_back(id);}std::sort(t.expanded.begin(),t.expanded.end());std::sort(t.opened.begin(),t.opened.end(),[](auto a,auto b){return a.local_id<b.local_id||(a.local_id==b.local_id&&a.level<b.level);});t.sq4=t.disclosed.size()*64.0;t.delta=t.refined.size()*32.0;t.raw_bytes=t.raw.size()*512.0;t.metadata=32+t.disclosed.size()*4.0+t.refined.size()*4.0+t.raw.size()*4.0;for(int id:t.expanded){t.metadata+=8;int levels=h.index->hnsw.levels[id];for(int l=0;l<levels;++l)t.metadata+=5;}for(auto x:t.opened)t.adjacency+=h.neighbors(x.local_id,x.level).size()*4.0;t.vo_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return t;}
}

#ifndef E_BUDGET_NO_MAIN
int main()try{using namespace vann;Config c;c.queries=100;Matrix base=read_fvecs(c.dataset+"/sift_base.fvecs",c.base),queries=read_fvecs(c.dataset+"/sift_query.fvecs",c.queries);HnswIndex h=build_hnsw(base,c.M,c.efc,c.cache);base.values.clear();base.values.shrink_to_fit();ESQ sq(h);auto gt=ground_truth(h,queries,c.k);std::vector<std::string>modes{"sq4","top2-overlap","certified"};std::vector<int>budgets{32,48,64,80};std::ofstream out("e_budget_relaxed_hnsw_q100.csv");out<<"mode,E,recall_at_10,avg_expanded,avg_disclosed,avg_sq4_only,avg_refined_sq6,avg_raw,sq4_payload_kib,delta46_payload_kib,raw_payload_kib,adjacency_payload_kib,ids_metadata_kib,mac_vo_kib,total_vo_kib,routing_ms,exact_topk_ms,vo_generation_ms,total_query_compute_ms\n";
for(const auto&mode:modes)for(int E:budgets){double hits=0,exp=0,disc=0,ref=0,raw=0,sq4b=0,delta=0,rawb=0,adj=0,meta=0,route=0,exact=0,vo=0;for(int qi=0;qi<c.queries;++qi){auto t=run_e(h,sq,queries.row(qi),c.k,E,mode);std::unordered_set<int>a(t.answer.begin(),t.answer.end());for(int id:gt[qi])hits+=a.count(id);exp+=t.expanded.size();disc+=t.disclosed.size();ref+=t.refined.size();raw+=t.raw.size();sq4b+=t.sq4;delta+=t.delta;rawb+=t.raw_bytes;adj+=t.adjacency;meta+=t.metadata;route+=t.route_ms;exact+=t.exact_ms;vo+=t.vo_ms;}double nq=c.queries,mac=64.0/1024,total=(sq4b+delta+rawb+adj+meta)/nq/1024+mac;out<<mode<<','<<E<<','<<hits/(nq*c.k)<<','<<exp/nq<<','<<disc/nq<<','<<(disc-ref)/nq<<','<<ref/nq<<','<<raw/nq<<','<<sq4b/nq/1024<<','<<delta/nq/1024<<','<<rawb/nq/1024<<','<<adj/nq/1024<<','<<meta/nq/1024<<','<<mac<<','<<total<<','<<route/nq<<','<<exact/nq<<','<<vo/nq<<','<<(route+exact+vo)/nq<<'\n';std::cout<<mode<<" E="<<E<<" recall="<<hits/(nq*c.k)<<" exp="<<exp/nq<<" disc="<<disc/nq<<" refine="<<ref/nq<<" raw="<<raw/nq<<" VO="<<total<<" KiB time="<<(route+exact+vo)/nq<<" ms\n";}return 0;}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
#endif

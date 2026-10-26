#define main relaxed_hnsw_embedded_main
#include "relaxed_hnsw_harvest.cpp"
#undef main

namespace {
using namespace vann;

class ProgressiveSQ {
public:
    explicit ProgressiveSQ(const HnswIndex& h)
        : d_(h.dimension()), lo_(d_, INFINITY), span_(d_), code_(h.size()*d_) {
        std::vector<float> hi(d_, -INFINITY);
        for (size_t id=0;id<h.size();++id) for(int j=0;j<d_;++j){lo_[j]=std::min(lo_[j],h.vector(id)[j]);hi[j]=std::max(hi[j],h.vector(id)[j]);}
        for(int j=0;j<d_;++j)span_[j]=hi[j]-lo_[j];
        for(size_t id=0;id<h.size();++id)for(int j=0;j<d_;++j){double u=span_[j]>0?(h.vector(id)[j]-lo_[j])/span_[j]:0;code_[id*d_+j]=uint8_t(std::clamp(int(std::floor(u*64)),0,63));}
    }
    float score(const float*q,int id,int bits)const{int bins=1<<bits,shift=6-bits;float s=0;for(int j=0;j<d_;++j){int p=code_[size_t(id)*d_+j]>>shift;float x=lo_[j]+span_[j]*((p+.5F)/bins),z=q[j]-x;s+=z*z;}return s;}
    float lower(const float*q,int id,int bits)const{int bins=1<<bits,shift=6-bits;float s=0;for(int j=0;j<d_;++j){int p=code_[size_t(id)*d_+j]>>shift;float a=std::nextafter(lo_[j]+span_[j]*(float(p)/bins),-INFINITY),b=std::nextafter(lo_[j]+span_[j]*(float(p+1)/bins),INFINITY);float z=q[j]<a?a-q[j]:(q[j]>b?q[j]-b:0);s+=z*z;}return std::nextafter(s,0.F);}
    float upper(const float*q,int id,int bits)const{int bins=1<<bits,shift=6-bits;float s=0;for(int j=0;j<d_;++j){int p=code_[size_t(id)*d_+j]>>shift;float a=lo_[j]+span_[j]*(float(p)/bins),b=lo_[j]+span_[j]*(float(p+1)/bins);float z=std::max(std::abs(q[j]-a),std::abs(q[j]-b));s+=z*z;}return std::nextafter(s,INFINITY);}
private:int d_;std::vector<float>lo_,span_;std::vector<uint8_t>code_;
};

struct PTrace{std::vector<int>discovered,expanded,answer,refined,raw;std::vector<OpenedAdjacency>opened;double adjacency_payload=0,metadata=0;};

PTrace progressive_search(const HnswIndex&h,const ProgressiveSQ&sq,const float*q,int k,int beam,int rounds,int upper_steps,const std::string&mode){
    const size_t n=h.size();PTrace t;std::vector<uint8_t>seen(n),expanded(n),expanded0(n),refined(n);std::vector<float>s4(n),s6(n),lb4(n),ub4(n);std::set<std::pair<int,int>>opened;
    auto discover=[&](int id){if(!seen[id]){seen[id]=1;s4[id]=sq.score(q,id,4);s6[id]=sq.score(q,id,6);lb4[id]=sq.lower(q,id,4);ub4[id]=sq.upper(q,id,4);}};
    auto open=[&](int id,int level){if(!opened.insert({id,level}).second)return std::vector<int>{};if(!expanded[id]){expanded[id]=1;t.expanded.push_back(id);}t.opened.push_back({id,level});return h.neighbors(id,level);};
    auto choose=[&](std::vector<int>pool,int want){want=std::min(want,int(pool.size()));if(want<=0)return std::vector<int>{};std::vector<int>eligible;
        if(mode=="sq4")eligible=pool;
        else if(mode=="certified") {std::vector<float>u;for(int id:pool)u.push_back(ub4[id]);std::nth_element(u.begin(),u.begin()+want-1,u.end());float threshold=u[want-1];for(int id:pool)if(lb4[id]<=threshold){eligible.push_back(id);refined[id]=1;}}
        else {int factor=std::stoi(mode.substr(9));std::sort(pool.begin(),pool.end(),[&](int a,int b){return s4[a]<s4[b]||(s4[a]==s4[b]&&a<b);});pool.resize(std::min(pool.size(),size_t(want*factor)));eligible=pool;for(int id:eligible)refined[id]=1;}
        auto&score=mode=="sq4"?s4:s6;std::partial_sort(eligible.begin(),eligible.begin()+want,eligible.end(),[&](int a,int b){return score[a]<score[b]||(score[a]==score[b]&&a<b);});eligible.resize(want);return eligible;};
    int entry=h.index->hnsw.entry_point;discover(entry);
    for(int level=h.index->hnsw.max_level;level>0;--level)for(int step=0;step<upper_steps;++step){auto ns=open(entry,level);if(ns.empty())break;for(int id:ns)discover(id);ns.push_back(entry);int next=choose(ns,1)[0];if(next==entry)break;entry=next;}
    for(int round=0;round<rounds;++round){std::vector<int>frontier;for(size_t id=0;id<n;++id)if(seen[id]&&!expanded0[id])frontier.push_back(id);if(frontier.empty())break;for(int id:choose(frontier,beam)){if(expanded0[id])continue;expanded0[id]=1;for(int nb:open(id,0))discover(nb);}}
    for(size_t id=0;id<n;++id)if(seen[id])t.discovered.push_back(id);
    std::vector<DistanceId>exact;for(int id:t.discovered)exact.push_back({squared_l2(q,h.vector(id),h.dimension()),id});std::partial_sort(exact.begin(),exact.begin()+k,exact.end(),better);exact.resize(k);float tau=exact.back().distance;for(auto z:exact)t.answer.push_back(z.id);
    for(int id:t.discovered)if(sq.lower(q,id,4)<=tau)refined[id]=1;
    for(int id:t.discovered)if(refined[id]){t.refined.push_back(id);if(sq.lower(q,id,6)<=tau)t.raw.push_back(id);}
    std::sort(t.expanded.begin(),t.expanded.end());std::sort(t.opened.begin(),t.opened.end(),[](auto a,auto b){return a.local_id<b.local_id||(a.local_id==b.local_id&&a.level<b.level);});
    t.metadata=32+t.discovered.size()*4.0+t.refined.size()*4.0+t.raw.size()*4.0;
    for(int id:t.expanded){t.metadata+=8;int levels=h.index->hnsw.levels[id];for(int l=0;l<levels;++l)t.metadata+=5;}
    for(auto x:t.opened)t.adjacency_payload+=h.neighbors(x.local_id,x.level).size()*4.0;
    return t;
}

bool same_trace(const PTrace&a,const Trace&b){if(a.discovered!=b.discovered||a.expanded!=b.expanded||a.answer!=b.answer||a.opened.size()!=b.opened.size())return false;for(size_t i=0;i<a.opened.size();++i)if(a.opened[i].local_id!=b.opened[i].local_id||a.opened[i].level!=b.opened[i].level)return false;return true;}
}

int main()try{using namespace vann;Config c;c.queries=100;Matrix base=read_fvecs(c.dataset+"/sift_base.fvecs",c.base),queries=read_fvecs(c.dataset+"/sift_query.fvecs",c.queries);HnswIndex h=build_hnsw(base,c.M,c.efc,c.cache);base.values.clear();base.values.shrink_to_fit();ProgressiveSQ sq(h);NestedSQ baseline_sq(h);auto gt=ground_truth(h,queries,c.k);std::vector<std::string>modes{"sq4","shortlist2","shortlist4","certified"};std::ofstream out("progressive_relaxed_hnsw_q100.csv");out<<"mode,recall_at_10,avg_expanded,avg_disclosed,avg_sq4_only,avg_refined_sq6,avg_raw,sq4_payload_kib,delta46_payload_kib,raw_payload_kib,adjacency_payload_kib,ids_metadata_kib,mac_vo_kib,total_vo_kib,trace_matches_sq6\n";
for(const auto&mode:modes){double hits=0,exp=0,disc=0,ref=0,raw=0,adj=0,meta=0,matches=0;for(int qi=0;qi<c.queries;++qi){PTrace t=progressive_search(h,sq,queries.row(qi),c.k,5,12,c.upper_steps,mode);std::unordered_set<int>a(t.answer.begin(),t.answer.end());for(int id:gt[qi])hits+=a.count(id);exp+=t.expanded.size();disc+=t.discovered.size();ref+=t.refined.size();raw+=t.raw.size();adj+=t.adjacency_payload;meta+=t.metadata;if(mode=="certified")matches+=same_trace(t,search(h,baseline_sq,queries.row(qi),c.k,6,5,12,c.upper_steps));}double nq=c.queries,sq4=disc/nq*64/1024,delta=ref/nq*32/1024,rawk=raw/nq*512/1024,adjk=adj/nq/1024,metak=meta/nq/1024,mac=64.0/1024,total=sq4+delta+rawk+adjk+metak+mac;out<<mode<<','<<hits/(nq*c.k)<<','<<exp/nq<<','<<disc/nq<<','<<(disc-ref)/nq<<','<<ref/nq<<','<<raw/nq<<','<<sq4<<','<<delta<<','<<rawk<<','<<adjk<<','<<metak<<','<<mac<<','<<total<<','<<(mode=="certified"?matches/nq:0)<<'\n';std::cout<<mode<<" recall="<<hits/(nq*c.k)<<" exp="<<exp/nq<<" disc="<<disc/nq<<" SQ4-only="<<(disc-ref)/nq<<" refined="<<ref/nq<<" raw="<<raw/nq<<" total="<<total<<" KiB match="<<matches/nq<<'\n';}return 0;}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

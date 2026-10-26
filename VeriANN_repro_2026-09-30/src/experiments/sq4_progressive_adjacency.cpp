#define E_BUDGET_NO_MAIN
#include "e_budget_relaxed_hnsw.cpp"

namespace {
using namespace vann;

class EdgeSQ4 {
public:
 explicit EdgeSQ4(const HnswIndex&h):d_(h.dimension()),lo_(d_,INFINITY),span_(d_),code_(h.size()*d_){std::vector<float>hi(d_,-INFINITY);for(size_t id=0;id<h.size();++id)for(int j=0;j<d_;++j){lo_[j]=std::min(lo_[j],h.vector(id)[j]);hi[j]=std::max(hi[j],h.vector(id)[j]);}for(int j=0;j<d_;++j)span_[j]=hi[j]-lo_[j];for(size_t id=0;id<h.size();++id)for(int j=0;j<d_;++j){double u=span_[j]>0?(h.vector(id)[j]-lo_[j])/span_[j]:0;code_[id*d_+j]=uint8_t(std::clamp(int(std::floor(u*16)),0,15));}}
 float distance(int a,int b)const{float s=0;const uint8_t*x=&code_[size_t(a)*d_],*y=&code_[size_t(b)*d_];for(int j=0;j<d_;++j){float z=float(x[j])-y[j];s+=z*z;}return s;}
private:int d_;std::vector<float>lo_,span_;std::vector<uint8_t>code_;
};

struct NodeMetrics{bool ready=false;std::vector<int>neighbors;std::vector<float>root,pair;};
struct Selection{std::vector<int>primary,remaining;};

class EdgeSelector{
public:
 EdgeSelector(const HnswIndex&h,const EdgeSQ4&sq):h_(h),sq_(sq),metrics_(h.size()),cache_(15,std::vector<Selection>(h.size())),cached_(15,std::vector<uint8_t>(h.size())){}
 const Selection&get(int id,int method,int dp){int di=dp_index(dp),key=method*5+di;if(cached_[key][id])return cache_[key][id];auto&m=metric(id);Selection z;int n=m.neighbors.size(),want=std::min(dp,n);std::vector<int>picked;
  if(method==0){std::vector<int>ord(n);std::iota(ord.begin(),ord.end(),0);std::sort(ord.begin(),ord.end(),[&](int a,int b){return m.root[a]<m.root[b]||(m.root[a]==m.root[b]&&a<b);});picked.assign(ord.begin(),ord.begin()+want);}
  else if(method==1){std::vector<int>ord(n);std::iota(ord.begin(),ord.end(),0);std::sort(ord.begin(),ord.end(),[&](int a,int b){return m.root[a]<m.root[b]||(m.root[a]==m.root[b]&&a<b);});std::vector<uint8_t>used(n);for(int c:ord){bool diverse=true;for(int s:picked)if(m.pair[c*n+s]<m.root[c]){diverse=false;break;}if(diverse){picked.push_back(c);used[c]=1;if(int(picked.size())==want)break;}}for(int c:ord)if(!used[c]&&int(picked.size())<want)picked.push_back(c);}
  else if(n){int first=0;double best=INFINITY;for(int c=0;c<n;++c){double sum=0;for(int x=0;x<n;++x)sum+=m.pair[c*n+x];if(sum<best)best=sum,first=c;}picked.push_back(first);std::vector<float>nearest(n);for(int x=0;x<n;++x)nearest[x]=m.pair[first*n+x];while(int(picked.size())<want){int choose=-1;double gain=-1;for(int c=0;c<n;++c){if(std::find(picked.begin(),picked.end(),c)!=picked.end())continue;double g=0;for(int x=0;x<n;++x)g+=nearest[x]-std::min(nearest[x],m.pair[c*n+x]);if(g>gain)gain=g,choose=c;}picked.push_back(choose);for(int x=0;x<n;++x)nearest[x]=std::min(nearest[x],m.pair[choose*n+x]);}}
  std::vector<uint8_t>is(n);for(int x:picked)is[x]=1;for(int x:picked)z.primary.push_back(m.neighbors[x]);for(int x=0;x<n;++x)if(!is[x])z.remaining.push_back(m.neighbors[x]);cache_[key][id]=std::move(z);cached_[key][id]=1;return cache_[key][id];}
private:
 int dp_index(int dp)const{if(dp==4)return 0;if(dp==6)return 1;if(dp==8)return 2;if(dp==10)return 3;return 4;}
 NodeMetrics&metric(int id){auto&m=metrics_[id];if(m.ready)return m;m.ready=true;m.neighbors=h_.neighbors(id,0);int n=m.neighbors.size();m.root.resize(n);m.pair.resize(n*n);for(int i=0;i<n;++i){m.root[i]=sq_.distance(id,m.neighbors[i]);for(int j=0;j<=i;++j)m.pair[i*n+j]=m.pair[j*n+i]=sq_.distance(m.neighbors[i],m.neighbors[j]);}return m;}
 const HnswIndex&h_;const EdgeSQ4&sq_;std::vector<NodeMetrics>metrics_;std::vector<std::vector<Selection>>cache_;std::vector<std::vector<uint8_t>>cached_;
};

struct ATrace{double hits=0,expanded=0,disclosed=0,fallback=0,level0=0,refined=0,raw=0,sq4=0,delta=0,rawb=0,adj=0,meta=0,ms=0;};

ATrace query(const HnswIndex&h,const ESQ&sq,EdgeSelector&selector,const float*q,const std::vector<int>&gt,int k,int method,int dp,int dr,int E){auto begin=std::chrono::steady_clock::now();size_t n=h.size();std::vector<uint8_t>seen(n),expanded(n),expanded0(n);std::vector<float>score(n),lb4(n);std::vector<int>expanded_ids,disclosed;std::set<std::pair<float,int>>frontier;double adjacency=0,metadata_levels=0,fallback=0,level0=0;
 auto discover=[&](int id){if(!seen[id]){seen[id]=1;score[id]=sq.score(q,id,4);lb4[id]=sq.lower(q,id,4);}};
 auto mark_expand=[&](int id){if(!expanded[id]){if(int(expanded_ids.size())>=E)return false;expanded[id]=1;expanded_ids.push_back(id);}return true;};
 int entry=h.index->hnsw.entry_point;discover(entry);bool budget=false;for(int level=h.index->hnsw.max_level;level>0&&!budget;--level)while(true){if(!mark_expand(entry)){budget=true;break;}auto ns=h.neighbors(entry,level);adjacency+=ns.size()*4.0;for(int id:ns)discover(id);int next=entry;for(int id:ns)if(score[id]<score[next]||(score[id]==score[next]&&id<next))next=id;if(next==entry)break;entry=next;}
 for(size_t id=0;id<n;++id)if(seen[id])frontier.insert({score[id],int(id)});while(int(expanded_ids.size())<E&&!frontier.empty()){int v=frontier.begin()->second;frontier.erase(frontier.begin());if(expanded0[v])continue;expanded0[v]=1;if(!mark_expand(v))break;++level0;const auto&sel=selector.get(v,method,dp);float best=INFINITY;for(int u:sel.primary){bool fresh=!seen[u];discover(u);if(fresh)frontier.insert({score[u],u});best=std::min(best,score[u]);}adjacency+=sel.primary.size()*4.0;bool stalled=sel.primary.empty()||best>=score[v];if(stalled){++fallback;int count=std::min(dr,int(sel.remaining.size()));for(int i=0;i<count;++i){int u=sel.remaining[i];bool fresh=!seen[u];discover(u);if(fresh)frontier.insert({score[u],u});}adjacency+=count*4.0;}}
 for(size_t id=0;id<n;++id)if(seen[id])disclosed.push_back(id);std::vector<DistanceId>exact;for(int id:disclosed)exact.push_back({squared_l2(q,h.vector(id),h.dimension()),id});std::partial_sort(exact.begin(),exact.begin()+k,exact.end(),better);exact.resize(k);float tau=exact.back().distance;std::unordered_set<int>answer;for(auto z:exact)answer.insert(z.id);ATrace out;for(int id:gt)out.hits+=answer.count(id);for(int id:disclosed)if(lb4[id]<=tau){++out.refined;if(sq.lower(q,id,6)<=tau)++out.raw;}for(int id:expanded_ids){int levels=h.index->hnsw.levels[id];metadata_levels+=levels*5.0;}out.expanded=expanded_ids.size();out.disclosed=disclosed.size();out.fallback=fallback;out.level0=level0;out.sq4=disclosed.size()*64.0;out.delta=out.refined*32.0;out.rawb=out.raw*512.0;out.adj=adjacency;out.meta=32+disclosed.size()*4.0+out.refined*4.0+out.raw*4.0+expanded_ids.size()*8.0+metadata_levels+level0*5.0;out.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return out;}
}

int main()try{using namespace vann;Config c;c.queries=100;Matrix base=read_fvecs(c.dataset+"/sift_base.fvecs",c.base),queries=read_fvecs(c.dataset+"/sift_query.fvecs",c.queries);HnswIndex h=build_hnsw(base,c.M,c.efc,c.cache);base.values.clear();base.values.shrink_to_fit();ESQ sq(h);EdgeSQ4 edge_sq(h);EdgeSelector selector(h,edge_sq);auto gt=ground_truth(h,queries,c.k);std::ofstream out("sq4_progressive_adjacency_q100.csv");out<<"method,DP,DR,E,recall_at_10,avg_expanded,avg_disclosed,fallback_ratio,avg_refined_sq6,avg_raw,sq4_payload_kib,delta46_payload_kib,raw_payload_kib,adjacency_payload_kib,ids_metadata_kib,mac_kib,total_vo_kib,query_ms\n";std::vector<std::string>names{"topD","robust","neighbor-cover"};
for(int method=0;method<3;++method)for(int dp:{4,6,8,10,12})for(int dr:{8,12,16})for(int E:{64,80,96,128}){ATrace a;for(int qi=0;qi<c.queries;++qi){auto z=query(h,sq,selector,queries.row(qi),gt[qi],c.k,method,dp,dr,E);a.hits+=z.hits;a.expanded+=z.expanded;a.disclosed+=z.disclosed;a.fallback+=z.fallback;a.level0+=z.level0;a.refined+=z.refined;a.raw+=z.raw;a.sq4+=z.sq4;a.delta+=z.delta;a.rawb+=z.rawb;a.adj+=z.adj;a.meta+=z.meta;a.ms+=z.ms;}double nq=c.queries,mac=64.0/1024,total=(a.sq4+a.delta+a.rawb+a.adj+a.meta)/nq/1024+mac;out<<names[method]<<','<<dp<<','<<dr<<','<<E<<','<<a.hits/(nq*c.k)<<','<<a.expanded/nq<<','<<a.disclosed/nq<<','<<a.fallback/a.level0<<','<<a.refined/nq<<','<<a.raw/nq<<','<<a.sq4/nq/1024<<','<<a.delta/nq/1024<<','<<a.rawb/nq/1024<<','<<a.adj/nq/1024<<','<<a.meta/nq/1024<<','<<mac<<','<<total<<','<<a.ms/nq<<'\n';}std::cerr<<"Wrote sq4_progressive_adjacency_q100.csv\n";return 0;}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

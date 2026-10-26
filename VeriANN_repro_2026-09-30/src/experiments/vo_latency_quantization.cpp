#define IVF_GRANULARITY_NO_MAIN
#include "ivf_granularity.cpp"
#undef IVF_GRANULARITY_NO_MAIN

#include <chrono>
#include <map>
#include <set>
#include <tuple>

struct VLConfig {
    std::string data="../dataset/sift", out="vo_latency_quantization.csv";
    int base=100000,test_q=50,train_q=50,k=100,K=16384,np=640,iters=6,reps=3;
};
static VLConfig vl_args(int ac,char**av){VLConfig c;for(int i=1;i<ac;++i){std::string a=av[i];auto v=[&](){if(++i==ac)throw std::runtime_error("missing value");return std::string(av[i]);};if(a=="--dataset")c.data=v();else if(a=="--output")c.out=v();else if(a=="--base")c.base=std::stoi(v());else if(a=="--test-queries")c.test_q=std::stoi(v());else if(a=="--train-queries")c.train_q=std::stoi(v());else if(a=="--k")c.k=std::stoi(v());else if(a=="--nlist")c.K=std::stoi(v());else if(a=="--nprobe")c.np=std::stoi(v());else if(a=="--iters")c.iters=std::stoi(v());else if(a=="--repeats")c.reps=std::stoi(v());else throw std::runtime_error("unknown option "+a);}return c;}

struct VLState { std::vector<int> lists,answer; float tau2=0; double query_ms=0; };

// Produce data-quantile and workload-weighted-quantile 6-bit boundaries in one
// sort per dimension. Coarser cells are unions of consecutive fine cells.
static std::pair<std::vector<double>,std::vector<double>> quantile_bounds(
        const Matrix&x,const std::vector<double>&weight){
    const int bins=64,d=x.d;const size_t n=x.rows();
    std::vector<double>data((size_t)d*(bins+1)),work(data.size());
    std::vector<int>ord(n);std::iota(ord.begin(),ord.end(),0);
    for(int j=0;j<d;++j){
        std::sort(ord.begin(),ord.end(),[&](int a,int b){return x[a][j]<x[b][j]||(x[a][j]==x[b][j]&&a<b);});
        data[(size_t)j*(bins+1)]=work[(size_t)j*(bins+1)]=x[ord.front()][j];
        data[(size_t)j*(bins+1)+bins]=work[(size_t)j*(bins+1)+bins]=x[ord.back()][j];
        for(int c=1;c<bins;++c){size_t p=std::min(n-1,(size_t)((long double)c*n/bins));data[(size_t)j*(bins+1)+c]=x[ord[p]][j];}
        double total=0;for(int id:ord)total+=weight[id];size_t p=0;double acc=weight[ord[0]];
        for(int c=1;c<bins;++c){double target=total*c/bins;while(p+1<n&&acc<target){++p;acc+=weight[ord[p]];}work[(size_t)j*(bins+1)+c]=x[ord[p]][j];}
    }
    return {std::move(data),std::move(work)};
}

static std::vector<double> uniform_bounds(const Matrix&x){
    const int bins=64,d=x.d;std::vector<double>b((size_t)d*(bins+1));
    for(int j=0;j<d;++j){double lo=std::numeric_limits<double>::infinity(),hi=-lo;for(size_t i=0;i<x.rows();++i){lo=std::min(lo,(double)x[i][j]);hi=std::max(hi,(double)x[i][j]);}for(int c=0;c<=bins;++c)b[(size_t)j*(bins+1)+c]=lo+(hi-lo)*c/bins;}return b;
}

// Greedily split every scalar cell at each depth to minimize train-only
// certified-distance slack. SIFT coordinates are integers in [0,255].
static std::vector<double> verification_tree_bounds(const Matrix&x,const Matrix&xq,
        const std::vector<std::vector<int>>&ls,const std::vector<VLState>&st,int begin,int end){
    const int d=x.d,R=256;std::vector<float>count((size_t)d*R*R);std::vector<double>total(d);
    for(int qi=begin;qi<end;++qi){double tau=std::sqrt(st[qi].tau2),scale=.05*tau+1e-6;for(int l:st[qi].lists)for(int id:ls[l]){double dist=std::sqrt(l2(xq[qi],x[id],d));float w=std::exp(-std::abs(dist-tau)/scale);for(int j=0;j<d;++j){int xv=std::clamp((int)std::lround(x[id][j]),0,255),qv=std::clamp((int)std::lround(xq[qi][j]),0,255);count[((size_t)j*R+xv)*R+qv]+=w;total[j]+=w;}}}
    // A 10% product-marginal prior regularizes sparse workload cells.
    std::vector<std::array<double,R>>hx(d),hq(d);for(size_t i=0;i<x.rows();++i)for(int j=0;j<d;++j)++hx[j][std::clamp((int)std::lround(x[i][j]),0,255)];for(int qi=begin;qi<end;++qi)for(int j=0;j<d;++j)++hq[j][std::clamp((int)std::lround(xq[qi][j]),0,255)];
    for(int j=0;j<d;++j)for(int xv=0;xv<R;++xv)for(int qv=0;qv<R;++qv)count[((size_t)j*R+xv)*R+qv]+=.1*total[j]*(hx[j][xv]/x.rows())*(hq[j][qv]/(end-begin));
    std::vector<double>bounds((size_t)d*65),pw((R+1)*R),pe((R+1)*R);
    for(int j=0;j<d;++j){int minv=255,maxv=0;for(size_t i=0;i<x.rows();++i){int v=std::clamp((int)std::lround(x[i][j]),0,255);minv=std::min(minv,v);maxv=std::max(maxv,v);}std::fill(pw.begin(),pw.end(),0);std::fill(pe.begin(),pe.end(),0);
        for(int xv=0;xv<R;++xv)for(int qv=0;qv<R;++qv){double w=count[((size_t)j*R+xv)*R+qv];pw[(xv+1)*R+qv]=pw[xv*R+qv]+w;pe[(xv+1)*R+qv]=pe[xv*R+qv]+w*(qv-xv)*(qv-xv);}
        auto cost=[&](int l,int r){double a=l==minv?l:l-.5,b=r==maxv?r:r+.5,z=0;for(int qv=0;qv<R;++qv){double w=pw[(r+1)*R+qv]-pw[l*R+qv],exact=pe[(r+1)*R+qv]-pe[l*R+qv],v=qv<a?a-qv:(qv>b?qv-b:0);z+=exact-w*v*v;}return z;};
        std::vector<std::pair<int,int>>cells{{minv,maxv}};for(int depth=0;depth<6;++depth){std::vector<std::pair<int,int>>next;for(auto cell:cells){int l=cell.first,r=cell.second;if(l>=r){next.push_back(cell);next.push_back(cell);continue;}double best=std::numeric_limits<double>::infinity();int split=(l+r)/2;for(int m=l;m<r;++m){double v=cost(l,m)+cost(m+1,r);if(v<best){best=v;split=m;}}next.push_back({l,split});next.push_back({split+1,r});}cells.swap(next);}
        bounds[(size_t)j*65]=minv;bounds[(size_t)j*65+64]=maxv;for(int k=1;k<64;++k)bounds[(size_t)j*65+k]=.5*(cells[k-1].second+cells[k].first);
    }
    return bounds;
}

struct NestedRule {
    struct LUT {std::vector<int>bits,bins,shift,off,order;std::vector<float>term;};
    int d=0;std::vector<double>boundary;std::vector<uint8_t>code;std::vector<uint32_t>hist;
    NestedRule(const Matrix&x,std::vector<double>b):d(x.d),boundary(std::move(b)),code(x.rows()*(size_t)d),hist((size_t)d*64){
        for(size_t i=0;i<x.rows();++i)for(int j=0;j<d;++j){const double*lo=boundary.data()+(size_t)j*65+1,*hi=boundary.data()+(size_t)j*65+64;int c=(int)(std::upper_bound(lo,hi,(double)x[i][j])-lo);c=std::clamp(c,0,63);code[i*(size_t)d+j]=c;++hist[(size_t)j*64+c];}
    }
    bool contains(const Matrix&x)const{for(size_t i=0;i<x.rows();++i)for(int j=0;j<d;++j){int c=code[i*(size_t)d+j];double a=boundary[(size_t)j*65+c],b=boundary[(size_t)j*65+c+1];if((double)x[i][j]<a||(double)x[i][j]>b)return false;}return true;}
    LUT prepare(const float*q,const std::vector<int>&bits)const{
        LUT z;z.bits=bits;z.bins.resize(d);z.shift.resize(d);z.off.resize(d+1);z.order.resize(d);std::iota(z.order.begin(),z.order.end(),0);
        for(int j=0;j<d;++j){z.bins[j]=1<<bits[j];z.shift[j]=6-bits[j];z.off[j+1]=z.off[j]+z.bins[j];}z.term.resize(z.off[d]);std::vector<double>score(d);
        for(int j=0;j<d;++j)for(int c=0;c<z.bins[j];++c){int l=c<<z.shift[j],r=(c+1)<<z.shift[j];double a=std::nextafter(boundary[(size_t)j*65+l],-std::numeric_limits<double>::infinity()),b=std::nextafter(boundary[(size_t)j*65+r],std::numeric_limits<double>::infinity());double v=q[j]<a?a-q[j]:(q[j]>b?q[j]-b:0),e=v*v;float f=(float)e;if((double)f>e)f=std::nextafter(f,0.0f);f=std::nextafter(f,0.0f);z.term[z.off[j]+c]=f;}
        for(int j=0;j<d;++j)for(int c=0;c<64;++c)score[j]+=(double)hist[(size_t)j*64+c]*z.term[z.off[j]+(c>>z.shift[j])];
        std::stable_sort(z.order.begin(),z.order.end(),[&](int a,int b){return score[a]>score[b];});return z;
    }
    bool overlaps(int id,double tau,const LUT&z)const{double s=0;size_t o=(size_t)id*d;for(int j:z.order){s+=z.term[z.off[j]+(code[o+j]>>z.shift[j])];if(s>tau)return false;}return true;}
    double lower2(int id,const LUT&z)const{double s=0;size_t o=(size_t)id*d;for(int j=0;j<d;++j)s+=z.term[z.off[j]+(code[o+j]>>z.shift[j])];return s;}
};

static std::vector<int> uniform_bits(int d,int b){return std::vector<int>(d,b);}
static int bit_sum(const std::vector<int>&b){return std::accumulate(b.begin(),b.end(),0);}
static std::vector<int> next_level(std::vector<int>b){for(int&x:b)if(x<6)++x;return b;}

struct Scheme {std::string name,rule;const NestedRule*q=nullptr;std::vector<std::vector<int>>level;};
struct Eval {double vo=0,payload=0,local=0,routing=0,ms=0,proof_ms=0,exact=0,s1=0,s2=0,visited=0;};

static Eval evaluate(const Matrix&xq,const std::vector<std::vector<int>>&ls,const std::vector<VLState>&st,
        int begin,int end,const Scheme&s,int reps){
    Eval out;
    for(int rep=0;rep<reps;++rep){double payload=0,local=0,routing=0,bns=0,pns=0;uint64_t exact=0,s1=0,s2=0,visited=0;
        for(int qi=begin;qi<end;++qi){auto b0=std::chrono::steady_clock::now();std::vector<NestedRule::LUT>lut;for(auto&b:s.level)lut.push_back(s.q->prepare(xq[qi],b));auto b1=std::chrono::steady_clock::now();bns+=std::chrono::duration<double,std::nano>(b1-b0).count();auto p0=std::chrono::steady_clock::now();routing+=proof_count(ls.size(),st[qi].lists)*32.0+st[qi].lists.size()*8.0;auto p1=std::chrono::steady_clock::now();pns+=std::chrono::duration<double,std::nano>(p1-p0).count();
            for(int c:st[qi].lists){const auto&L=ls[c];visited+=L.size();std::vector<int>active,next;active.reserve(L.size());next.reserve(L.size());b0=std::chrono::steady_clock::now();payload+=L.size()*(4.0+std::ceil(bit_sum(s.level[0])/8.0));for(size_t p=0;p<L.size();++p)if(s.q->overlaps(L[p],st[qi].tau2,lut[0]))active.push_back(p);s1+=active.size();
                for(size_t z=1;z<s.level.size();++z){b1=std::chrono::steady_clock::now();bns+=std::chrono::duration<double,std::nano>(b1-b0).count();p0=std::chrono::steady_clock::now();local+=proof_count(L.size(),active)*32.0;p1=std::chrono::steady_clock::now();pns+=std::chrono::duration<double,std::nano>(p1-p0).count();payload+=active.size()*std::ceil((bit_sum(s.level[z])-bit_sum(s.level[z-1]))/8.0);b0=std::chrono::steady_clock::now();next.clear();for(int p:active)if(s.q->overlaps(L[p],st[qi].tau2,lut[z]))next.push_back(p);active.swap(next);if(z==1)s2+=active.size();}
                payload+=active.size()*512.0;exact+=active.size();b1=std::chrono::steady_clock::now();bns+=std::chrono::duration<double,std::nano>(b1-b0).count();p0=std::chrono::steady_clock::now();local+=proof_count(L.size(),active)*32.0;p1=std::chrono::steady_clock::now();pns+=std::chrono::duration<double,std::nano>(p1-p0).count();
            }
        }
        double n=end-begin;out.payload+=payload/n;out.local+=local/n;out.routing+=routing/n;out.ms+=bns/n/1e6;out.proof_ms+=pns/n/1e6;out.exact+=exact/n;out.s1+=s1/n;out.s2+=s2/n;out.visited+=visited/n;
    }
    double r=reps;out.payload/=r;out.local/=r;out.routing/=r;out.ms/=r;out.proof_ms/=r;out.exact/=r;out.s1/=r;out.s2/=r;out.visited/=r;out.vo=out.payload+out.local+out.routing;return out;
}

// Rank dimensions for a fixed 512-bit base level. Upgrades use the dimensions
// whose extra bit most often closes a near-threshold gap; downgrades use those
// whose removed bit least often destroys an existing certificate.
static std::vector<std::vector<int>> allocation_candidates(const Matrix&xq,const std::vector<std::vector<int>>&ls,
        const std::vector<VLState>&st,int begin,int end,const NestedRule&q){
    const int d=q.d;std::vector<double>up(d),harm(d);auto b3=uniform_bits(d,3),b4=uniform_bits(d,4),b5=uniform_bits(d,5);
    for(int qi=begin;qi<end;++qi){auto z3=q.prepare(xq[qi],b3),z4=q.prepare(xq[qi],b4),z5=q.prepare(xq[qi],b5);std::vector<double>c3(d),c4(d),c5(d);
        for(int l:st[qi].lists)for(int id:ls[l]){size_t o=(size_t)id*d;double sum=0;for(int j=0;j<d;++j){int c=q.code[o+j];c3[j]=z3.term[z3.off[j]+(c>>z3.shift[j])];c4[j]=z4.term[z4.off[j]+(c>>z4.shift[j])];c5[j]=z5.term[z5.off[j]+(c>>z5.shift[j])];sum+=c4[j];}double gap=st[qi].tau2-sum;if(gap>=0){for(int j=0;j<d;++j){double delta=std::max(0.0,c5[j]-c4[j]);up[j]+=delta/(gap+delta+1.0);}}else{double margin=-gap;for(int j=0;j<d;++j){double loss=std::max(0.0,c4[j]-c3[j]);harm[j]+=loss/(margin+loss+1.0);}}}
    }
    std::vector<int>U(d),D(d);std::iota(U.begin(),U.end(),0);std::iota(D.begin(),D.end(),0);std::sort(U.begin(),U.end(),[&](int a,int b){return up[a]>up[b];});std::sort(D.begin(),D.end(),[&](int a,int b){return harm[a]<harm[b];});
    std::vector<std::vector<int>>out;for(int target:{8,16,24,32,48,64}){auto bits=b4;std::set<int>used;int ui=0,di=0,pairs=0;while(pairs<target&&ui<d&&di<d){while(ui<d&&used.count(U[ui]))++ui;while(di<d&&(used.count(D[di])||D[di]==U[ui]))++di;if(ui>=d||di>=d)break;bits[U[ui]]=5;bits[D[di]]=3;used.insert(U[ui]);used.insert(D[di]);++ui;++di;++pairs;}out.push_back(std::move(bits));}return out;
}

#ifndef VO_LATENCY_QUANTIZATION_NO_MAIN
int main(int ac,char**av)try{
    auto c=vl_args(ac,av);int nq=c.test_q+c.train_q;std::cerr<<"Loading data and queries...\n";Matrix xb=fvecs(c.data+"/sift_base.fvecs",c.base),xq=fvecs(c.data+"/sift_query.fvecs",nq);auto gt=exact_gt(xb,xq,c.k);std::cerr<<"Training hierarchical IVF nlist="<<c.K<<"...\n";auto ls=hkmeans(xb,c.K,c.iters);auto centers_=centers(xb,ls);
    std::vector<VLState>st(nq);for(int qi=0;qi<nq;++qi){auto t0=std::chrono::steady_clock::now();std::vector<int>ord(c.K);std::iota(ord.begin(),ord.end(),0);std::partial_sort(ord.begin(),ord.begin()+c.np,ord.end(),[&](int a,int b){return l2(xq[qi],centers_[a].data(),xb.d)<l2(xq[qi],centers_[b].data(),xb.d);});st[qi].lists.assign(ord.begin(),ord.begin()+c.np);std::priority_queue<std::pair<float,int>>top;for(int l:st[qi].lists)for(int id:ls[l]){float d=l2(xq[qi],xb[id],xb.d);if((int)top.size()<c.k)top.push({d,id});else if(d<top.top().first){top.pop();top.push({d,id});}}st[qi].tau2=top.top().first;while(!top.empty()){st[qi].answer.push_back(top.top().second);top.pop();}auto t1=std::chrono::steady_clock::now();st[qi].query_ms=std::chrono::duration<double,std::milli>(t1-t0).count();}
    double recall=0;for(int qi=0;qi<c.test_q;++qi){std::unordered_set<int>a(st[qi].answer.begin(),st[qi].answer.end());for(int id:gt[qi])recall+=a.count(id);}recall/=c.test_q*(double)c.k;
    std::cerr<<"Building train-only workload weights...\n";std::vector<double>w(xb.rows(),0.05);for(int qi=c.test_q;qi<nq;++qi){double tau=std::sqrt(st[qi].tau2),scale=0.05*tau+1e-6;for(int l:st[qi].lists)for(int id:ls[l]){double dist=std::sqrt(l2(xq[qi],xb[id],xb.d));w[id]+=std::exp(-std::abs(dist-tau)/scale);}}
    std::cerr<<"Learning nested quantile and verification-slack boundaries...\n";auto qb=quantile_bounds(xb,w);auto vb=verification_tree_bounds(xb,xq,ls,st,c.test_q,nq);NestedRule U(xb,uniform_bounds(xb)),DQ(xb,std::move(qb.first)),WQ(xb,std::move(qb.second)),VT(xb,std::move(vb));if(!U.contains(xb)||!DQ.contains(xb)||!WQ.contains(xb)||!VT.contains(xb))throw std::runtime_error("quantization cell containment failed");
    auto all6=uniform_bits(xb.d,6);std::vector<Scheme>schemes;auto add_fixed=[&](const std::string&n,const std::string&r,const NestedRule&q){schemes.push_back({n+"-SQ6",r,&q,{all6}});schemes.push_back({n+"-4->6",r,&q,{uniform_bits(xb.d,4),all6}});schemes.push_back({n+"-4->5->6",r,&q,{uniform_bits(xb.d,4),uniform_bits(xb.d,5),all6}});schemes.push_back({n+"-3->4->6",r,&q,{uniform_bits(xb.d,3),uniform_bits(xb.d,4),all6}});schemes.push_back({n+"-5->6",r,&q,{uniform_bits(xb.d,5),all6}});};add_fixed("U","uniform",U);add_fixed("DQ","data_quantile",DQ);add_fixed("WQ","workload_quantile",WQ);add_fixed("VT","verification_tree",VT);
    std::cerr<<"Learning per-dimension bit allocations from training queries...\n";for(auto item:{std::make_pair("DQ",&DQ),std::make_pair("WQ",&WQ),std::make_pair("VT",&VT)}){auto cand=allocation_candidates(xq,ls,st,c.test_q,nq,*item.second);for(size_t i=0;i<cand.size();++i){std::string n=std::string(item.first)+"-VA"+std::to_string(i);auto mid=next_level(cand[i]);schemes.push_back({n+"->6",item.first,item.second,{cand[i],all6}});schemes.push_back({n+"->mid->6",item.first,item.second,{cand[i],mid,all6}});}}
    std::cerr<<"Evaluating "<<schemes.size()<<" candidate protocols on train/test splits...\n";std::vector<Eval>train,test;for(auto&s:schemes){train.push_back(evaluate(xq,ls,st,c.test_q,nq,s,1));test.push_back(evaluate(xq,ls,st,0,c.test_q,s,c.reps));}
    std::map<std::string,double>lambda={{"100Mbps",12500.0},{"1Gbps",125000.0},{"10Gbps",1250000.0}};std::map<std::string,int>chosen;for(auto&kv:lambda){double best=std::numeric_limits<double>::infinity();int pick=-1;for(size_t i=0;i<schemes.size();++i){double cost=train[i].vo+kv.second*(train[i].ms+train[i].proof_ms);if(cost<best){best=cost;pick=i;}}chosen[kv.first]=pick;}
    std::ofstream out(c.out);out<<"scheme,rule,levels,train_vo_bytes,test_vo_bytes,test_payload_bytes,test_local_merkle_bytes,test_routing_merkle_bytes,test_exact,test_stage1,test_stage2,test_bound_ms,test_proof_ms,test_total_ms,recall,selected_100Mbps,selected_1Gbps,selected_10Gbps\n";double qms=0;for(int qi=0;qi<c.test_q;++qi)qms+=st[qi].query_ms;qms/=c.test_q;std::cout<<"scheme train_KiB test_KiB exact proof_ms selected\n";
    for(size_t i=0;i<schemes.size();++i){std::string levels;for(size_t z=0;z<schemes[i].level.size();++z){if(z)levels+='>';levels+=std::to_string(bit_sum(schemes[i].level[z]));}bool s100=chosen["100Mbps"]==(int)i,s1=chosen["1Gbps"]==(int)i,s10=chosen["10Gbps"]==(int)i;out<<schemes[i].name<<','<<schemes[i].rule<<','<<levels<<','<<train[i].vo<<','<<test[i].vo<<','<<test[i].payload<<','<<test[i].local<<','<<test[i].routing<<','<<test[i].exact<<','<<test[i].s1<<','<<test[i].s2<<','<<test[i].ms<<','<<test[i].proof_ms<<','<<qms+test[i].ms+test[i].proof_ms<<','<<recall<<','<<s100<<','<<s1<<','<<s10<<'\n';if(s100||s1||s10||schemes[i].name=="U-4->6"||schemes[i].name=="U-SQ6")std::cout<<schemes[i].name<<' '<<train[i].vo/1024<<' '<<test[i].vo/1024<<' '<<test[i].exact<<' '<<test[i].proof_ms<<' '<<(s100?"100M ":"")<<(s1?"1G ":"")<<(s10?"10G":"")<<'\n';}
    // Numerical lower-bound check on held-out queries for every selected scheme.
    uint64_t violations=0;std::set<int>check;for(auto&kv:chosen)check.insert(kv.second);for(int si:check)for(int qi=0;qi<std::min(c.test_q,10);++qi){std::vector<NestedRule::LUT>lut;for(auto&b:schemes[si].level)lut.push_back(schemes[si].q->prepare(xq[qi],b));for(int l:st[qi].lists)for(int id:ls[l]){double exact=l2(xq[qi],xb[id],xb.d);for(auto&z:lut)violations+=schemes[si].q->lower2(id,z)>exact;}}if(violations)throw std::runtime_error("unsafe lower bounds="+std::to_string(violations));
    std::cerr<<"Held-out recall="<<recall<<", query="<<qms<<" ms, bound violations="<<violations<<". Wrote "<<c.out<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
#endif

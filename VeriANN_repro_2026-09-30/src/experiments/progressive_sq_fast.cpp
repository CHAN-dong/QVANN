#define IVF_GRANULARITY_NO_MAIN
#include "ivf_granularity.cpp"
#undef IVF_GRANULARITY_NO_MAIN

#include <array>
#include <chrono>
#include <tuple>

// Optimized, matched benchmark for verification-aware progressive SQ.
// A max-resolution code is committed once. Its b-bit prefix denotes an
// outward-rounded cell, so every refinement is a sound nested certificate.
struct NestedSQFast {
    struct LUT {
        int bits=0,bins=0,shift=0;
        std::vector<float> term; // [dimension][prefix]
        std::vector<int> order;
    };
    int d,max_bits,max_bins;
    std::vector<double> lo,hi,span;
    std::vector<uint8_t> code;
    std::vector<uint32_t> hist;

    NestedSQFast(const Matrix&x,int mb):d(x.d),max_bits(mb),max_bins(1<<mb),
      lo(d,std::numeric_limits<double>::infinity()),hi(d,-std::numeric_limits<double>::infinity()),span(d),
      code(x.rows()*static_cast<size_t>(d)),hist(static_cast<size_t>(d)*max_bins){
        for(size_t i=0;i<x.rows();++i)for(int j=0;j<d;++j){lo[j]=std::min(lo[j],(double)x[i][j]);hi[j]=std::max(hi[j],(double)x[i][j]);}
        for(int j=0;j<d;++j)span[j]=hi[j]-lo[j];
        for(size_t i=0;i<x.rows();++i)for(int j=0;j<d;++j){int c=0;if(span[j]>0){double u=((double)x[i][j]-lo[j])/span[j];c=std::clamp((int)std::floor(u*max_bins),0,max_bins-1);}code[i*(size_t)d+j]=c;++hist[(size_t)j*max_bins+c];}
    }
    LUT prepare(const float*q,int bits,bool reorder=true)const{
        LUT z;z.bits=bits;z.bins=1<<bits;z.shift=max_bits-bits;z.term.resize((size_t)d*z.bins);z.order.resize(d);std::iota(z.order.begin(),z.order.end(),0);std::vector<double>score(d);
        for(int j=0;j<d;++j){
            for(int c=0;c<z.bins;++c){
                double a=lo[j]+span[j]*c/z.bins,b=lo[j]+span[j]*(c+1)/z.bins;
                a=std::nextafter(a,-std::numeric_limits<double>::infinity());b=std::nextafter(b,std::numeric_limits<double>::infinity());
                double v=q[j]<a?a-q[j]:(q[j]>b?q[j]-b:0.0),e=v*v;float f=(float)e;if((double)f>e)f=std::nextafter(f,0.0f);f=std::nextafter(f,0.0f);z.term[(size_t)j*z.bins+c]=f;
            }
            if(reorder)for(int c=0;c<max_bins;++c)score[j]+=(double)hist[(size_t)j*max_bins+c]*z.term[(size_t)j*z.bins+(c>>z.shift)];
        }
        if(reorder)std::stable_sort(z.order.begin(),z.order.end(),[&](int a,int b){return score[a]>score[b];});
        return z;
    }
    bool overlaps(size_t id,double tau2,const LUT&z)const{
        double s=0;size_t o=id*(size_t)d;for(int j:z.order){s+=z.term[(size_t)j*z.bins+(code[o+j]>>z.shift)];if(s>tau2)return false;}return true;
    }
    double lower2(size_t id,const LUT&z)const{
        double s=0;size_t o=id*(size_t)d;for(int j=0;j<d;++j)s+=z.term[(size_t)j*z.bins+(code[o+j]>>z.shift)];return s;
    }
    int bytes(int bits)const{return(d*bits+7)/8;}
};

struct PCfg{std::string data="../dataset/sift",out="progressive_sq_fast.csv";int base=100000,nq=50,k=100,K=32768,np=384,iters=6,reps=5;};
static PCfg pargs(int ac,char**av){PCfg c;for(int i=1;i<ac;++i){std::string a=av[i];auto v=[&](){if(++i==ac)throw std::runtime_error("missing value");return std::string(av[i]);};if(a=="--dataset")c.data=v();else if(a=="--output")c.out=v();else if(a=="--base")c.base=std::stoi(v());else if(a=="--queries")c.nq=std::stoi(v());else if(a=="--k")c.k=std::stoi(v());else if(a=="--nlist")c.K=std::stoi(v());else if(a=="--nprobe")c.np=std::stoi(v());else if(a=="--iters")c.iters=std::stoi(v());else if(a=="--repeats")c.reps=std::stoi(v());else throw std::runtime_error("unknown option "+a);}return c;}

struct QState{std::vector<int>lists,answer;float tau2=0;double route_ms=0,scan_ms=0;};

// Same count as block_auth.cpp::proof_count, but O(opened log n) and allocation-light.
static size_t sparse_proof_count(size_t n,const std::vector<int>&sel){
    if(!n)return 0;
    size_t padded=1;while(padded<n)padded<<=1;std::vector<size_t>cur;cur.reserve(sel.size());for(int x:sel)if(x>=0&&(size_t)x<n)cur.push_back(x);std::sort(cur.begin(),cur.end());cur.erase(std::unique(cur.begin(),cur.end()),cur.end());if(cur.empty())return 1;size_t ans=0;
    while(padded>1){std::vector<size_t>up;up.reserve((cur.size()+1)/2);for(size_t i=0;i<cur.size();){size_t x=cur[i];if(i+1<cur.size()&&cur[i+1]==(x^1U))i+=2;else{++ans;++i;}size_t p=x>>1;if(up.empty()||up.back()!=p)up.push_back(p);}cur.swap(up);padded>>=1;}return ans;
}
static void proof_self_test(){for(size_t n=1;n<=64;++n)for(unsigned m=0;m<256;++m){std::vector<int>s;for(size_t i=0;i<n&&i<8;++i)if(m&(1U<<i))s.push_back(i);if(sparse_proof_count(n,s)!=proof_count(n,s))throw std::runtime_error("proof counter mismatch");}}

struct Result{std::string name;double a1=0,a2=0,exact=0,payload=0,local=0,routing=0,total=0,bound_ms=0,proof_ms=0;};
static Result evaluate(const Matrix&xq,const std::vector<std::vector<int>>&ls,const std::vector<QState>&st,const NestedSQFast&sq,const SQ4&legacy,const std::string&name,const std::vector<int>&levels,bool old,int reps){
    Result r;r.name=name;
    for(int rep=0;rep<reps;++rep){uint64_t a1=0,a2=0,exact=0;double payload=0,local=0,routing=0,bns=0,pns=0;
        for(size_t qi=0;qi<xq.rows();++qi){auto b0=std::chrono::steady_clock::now();std::vector<NestedSQFast::LUT>lut;if(!old)for(int b:levels)lut.push_back(sq.prepare(xq[qi],b));auto b1=std::chrono::steady_clock::now();bns+=std::chrono::duration<double,std::nano>(b1-b0).count();auto p0=std::chrono::steady_clock::now();routing+=(sparse_proof_count(ls.size(),st[qi].lists)*32.0+st[qi].lists.size()*8.0);auto p1=std::chrono::steady_clock::now();pns+=std::chrono::duration<double,std::nano>(p1-p0).count();
            for(int c:st[qi].lists){const auto&L=ls[c];std::vector<int>active,next;active.reserve(L.size());next.reserve(L.size());b0=std::chrono::steady_clock::now();
                if(old){float tau=std::sqrt(st[qi].tau2);for(size_t p=0;p<L.size();++p)if(legacy.lb(xq[qi],L[p])<=tau)active.push_back(p);payload+=L.size()*72.0+active.size()*512.0;a1+=active.size();exact+=active.size();}
                else{payload+=L.size()*(4.0+sq.bytes(levels[0]));for(size_t p=0;p<L.size();++p)if(sq.overlaps(L[p],st[qi].tau2,lut[0]))active.push_back(p);a1+=active.size();for(size_t z=1;z<levels.size();++z){b1=std::chrono::steady_clock::now();bns+=std::chrono::duration<double,std::nano>(b1-b0).count();p0=std::chrono::steady_clock::now();local+=sparse_proof_count(L.size(),active)*32.0;p1=std::chrono::steady_clock::now();pns+=std::chrono::duration<double,std::nano>(p1-p0).count();payload+=active.size()*sq.bytes(levels[z]-levels[z-1]);b0=std::chrono::steady_clock::now();next.clear();for(int p:active)if(sq.overlaps(L[p],st[qi].tau2,lut[z]))next.push_back(p);active.swap(next);if(z==1)a2+=active.size();}payload+=active.size()*512.0;exact+=active.size();}
                b1=std::chrono::steady_clock::now();bns+=std::chrono::duration<double,std::nano>(b1-b0).count();p0=std::chrono::steady_clock::now();local+=sparse_proof_count(L.size(),active)*32.0;p1=std::chrono::steady_clock::now();pns+=std::chrono::duration<double,std::nano>(p1-p0).count();
            }
        }
        double q=xq.rows();r.a1+=a1/q;r.a2+=a2/q;r.exact+=exact/q;r.payload+=payload/q;r.local+=local/q;r.routing+=routing/q;r.bound_ms+=bns/q/1e6;r.proof_ms+=pns/q/1e6;
    }
    double z=reps;r.a1/=z;r.a2/=z;r.exact/=z;r.payload/=z;r.local/=z;r.routing/=z;r.bound_ms/=z;r.proof_ms/=z;r.total=r.payload+r.local+r.routing;return r;
}

struct Cascade{double scan_ms=0,exact=0;uint64_t checksum=0;};
static Cascade cascade(const Matrix&xb,const Matrix&xq,const std::vector<std::vector<int>>&ls,const std::vector<QState>&st,const NestedSQFast&sq,const std::vector<int>&levels,int k,int reps){
    Cascade z;for(int rep=0;rep<reps;++rep){uint64_t ex=0,sum=0;auto t0=std::chrono::steady_clock::now();for(size_t qi=0;qi<xq.rows();++qi){std::vector<NestedSQFast::LUT>lut;for(int b:levels)lut.push_back(sq.prepare(xq[qi],b));std::priority_queue<std::pair<float,int>>top;for(int c:st[qi].lists)for(int id:ls[c]){bool open=(int)top.size()<k;if(!open){open=true;for(auto&u:lut)if(!sq.overlaps(id,top.top().first,u)){open=false;break;}}if(!open)continue;++ex;float d=l2(xq[qi],xb[id],xb.d);if((int)top.size()<k)top.push({d,id});else if(d<top.top().first){top.pop();top.push({d,id});}}std::vector<int>answer;while(!top.empty()){answer.push_back(top.top().second);sum+=(uint64_t)top.top().second+1;top.pop();}if(answer!=st[qi].answer)throw std::runtime_error("cascade query result mismatch at query "+std::to_string(qi));}auto t1=std::chrono::steady_clock::now();z.scan_ms+=std::chrono::duration<double,std::milli>(t1-t0).count()/xq.rows();z.exact+=(double)ex/xq.rows();z.checksum=sum;}z.scan_ms/=reps;z.exact/=reps;return z;
}

#ifndef PROGRESSIVE_SQ_FAST_NO_MAIN
int main(int ac,char**av)try{
    PCfg c=pargs(ac,av);proof_self_test();std::cerr<<"Loading data and exact ground truth...\n";Matrix xb=fvecs(c.data+"/sift_base.fvecs",c.base),xq=fvecs(c.data+"/sift_query.fvecs",c.nq);auto gt=exact_gt(xb,xq,c.k);std::cerr<<"Training hierarchical IVF nlist="<<c.K<<"...\n";auto ls=hkmeans(xb,c.K,c.iters);auto cs=centers(xb,ls);SQ4 legacy(xb);NestedSQFast sq(xb,6);
    std::vector<QState>st(xq.rows());uint64_t visited=0,hits=0,checksum=0;for(size_t qi=0;qi<xq.rows();++qi){auto t0=std::chrono::steady_clock::now();std::vector<int>ord(c.K);std::iota(ord.begin(),ord.end(),0);std::partial_sort(ord.begin(),ord.begin()+c.np,ord.end(),[&](int a,int b){return l2(xq[qi],cs[a].data(),xb.d)<l2(xq[qi],cs[b].data(),xb.d);});st[qi].lists.assign(ord.begin(),ord.begin()+c.np);auto t1=std::chrono::steady_clock::now();st[qi].route_ms=std::chrono::duration<double,std::milli>(t1-t0).count();std::priority_queue<std::pair<float,int>>top;t0=std::chrono::steady_clock::now();for(int z:st[qi].lists)for(int id:ls[z]){float d=l2(xq[qi],xb[id],xb.d);++visited;if((int)top.size()<c.k)top.push({d,id});else if(d<top.top().first){top.pop();top.push({d,id});}}t1=std::chrono::steady_clock::now();st[qi].scan_ms=std::chrono::duration<double,std::milli>(t1-t0).count();st[qi].tau2=top.top().first;while(!top.empty()){st[qi].answer.push_back(top.top().second);checksum+=(uint64_t)top.top().second+1;top.pop();}std::unordered_set<int>a(st[qi].answer.begin(),st[qi].answer.end());for(int id:gt[qi])hits+=a.count(id);}
    uint64_t bad=0;for(size_t qi=0;qi<xq.rows();++qi){auto q4=sq.prepare(xq[qi],4,false),q6=sq.prepare(xq[qi],6,false);for(int z:st[qi].lists)for(int id:ls[z]){double exact=l2(xq[qi],xb[id],xb.d);bad+=(sq.lower2(id,q4)>exact||sq.lower2(id,q6)>exact);}}if(bad)throw std::runtime_error("unsafe bounds="+std::to_string(bad));
    double query_ms=0,route_ms=0;for(auto&s:st){query_ms+=s.route_ms+s.scan_ms;route_ms+=s.route_ms;}query_ms/=xq.rows();route_ms/=xq.rows();double recall=hits/(xq.rows()*(double)c.k),avgvis=visited/(double)xq.rows();
    auto c4=cascade(xb,xq,ls,st,sq,{4},c.k,c.reps),c46=cascade(xb,xq,ls,st,sq,{4,6},c.k,c.reps);if(c4.checksum!=checksum||c46.checksum!=checksum)throw std::runtime_error("cascade answer mismatch");
    std::vector<std::tuple<std::string,std::vector<int>,bool>>schemes={{"legacy-SQ4",{},true},{"interval-SQ4",{4},false},{"interval-SQ6",{6},false},{"progressive-SQ4->SQ6",{4,6},false},{"progressive-SQ3->SQ4->SQ6",{3,4,6},false}};
    std::ofstream out(c.out);out<<"base,nlist,nprobe,scheme,recall,avg_visited,avg_after_l1,avg_after_l2,avg_exact,payload_bytes,local_merkle_bytes,routing_merkle_bytes,total_vo_bytes,query_ms,bound_ms,proof_layout_ms,compute_total_ms,overhead_vs_query,routing_ms,exact_scan_ms,cascade_sq4_scan_ms,cascade_sq46_scan_ms,cascade_sq4_exact,cascade_sq46_exact\n";std::cout<<"scheme recall visited exact VO_KiB query_ms vo_ms total_ms\n";
    for(auto&s:schemes){auto r=evaluate(xq,ls,st,sq,legacy,std::get<0>(s),std::get<1>(s),std::get<2>(s),c.reps);double vo=r.bound_ms+r.proof_ms;out<<c.base<<','<<c.K<<','<<c.np<<','<<r.name<<','<<recall<<','<<avgvis<<','<<r.a1<<','<<r.a2<<','<<r.exact<<','<<r.payload<<','<<r.local<<','<<r.routing<<','<<r.total<<','<<query_ms<<','<<r.bound_ms<<','<<r.proof_ms<<','<<query_ms+vo<<','<<vo/query_ms<<','<<route_ms<<','<<query_ms-route_ms<<','<<c4.scan_ms<<','<<c46.scan_ms<<','<<c4.exact<<','<<c46.exact<<'\n';std::cout<<r.name<<' '<<std::fixed<<std::setprecision(4)<<recall<<' '<<std::setprecision(1)<<avgvis<<' '<<r.exact<<' '<<r.total/1024<<' '<<std::setprecision(3)<<query_ms<<' '<<vo<<' '<<query_ms+vo<<'\n';}
    std::cerr<<"Integrated exact-result cascade: routing="<<route_ms<<" ms, SQ4 scan="<<c4.scan_ms<<" ms/exact="<<c4.exact<<", SQ4->SQ6 scan="<<c46.scan_ms<<" ms/exact="<<c46.exact<<"\n";std::cerr<<"Wrote "<<c.out<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
#endif

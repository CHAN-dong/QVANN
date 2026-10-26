#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

struct Matrix {
    int d = 0; std::vector<float> x;
    size_t rows() const { return d ? x.size()/d : 0; }
    const float* operator[](size_t i) const { return x.data()+i*d; }
};
static Matrix fvecs(const std::string& p, size_t limit=0) {
    std::ifstream f(p,std::ios::binary); if(!f) throw std::runtime_error("cannot open "+p); Matrix a;
    while(!limit||a.rows()<limit){int32_t d;if(!f.read((char*)&d,4))break;if(a.d&&a.d!=d)throw std::runtime_error("bad fvecs");a.d=d;size_t o=a.x.size();a.x.resize(o+d);if(!f.read((char*)a.x.data()+o*4,4LL*d))throw std::runtime_error("truncated fvecs");} return a;
}
static std::vector<std::vector<int>> ivecs(const std::string& p,size_t limit){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot open gt");std::vector<std::vector<int>> a;while(a.size()<limit){int32_t d;if(!f.read((char*)&d,4))break;a.emplace_back(d);if(!f.read((char*)a.back().data(),4LL*d))throw std::runtime_error("truncated gt");}return a;}
static float l2(const float*a,const float*b,int d){float s=0;for(int j=0;j<d;++j){float v=a[j]-b[j];s+=v*v;}return s;}

struct Config { std::string data="../dataset/sift",assign="../populated_index/PriorMax_SIFT1M_C100_clusterid.tsv",out="residual_pq_results.csv";int nlist=100,k=100,nq=50,train=20000,iters=10;std::vector<int> probes{1,2,4,8,16,32,64,100};};
static std::vector<int> nums(const std::string&s){std::vector<int>v;size_t p=0;while(p<s.size()){size_t q=s.find(',',p);v.push_back(std::stoi(s.substr(p,q-p)));if(q==std::string::npos)break;p=q+1;}return v;}
static Config parse(int ac,char**av){Config c;for(int i=1;i<ac;++i){std::string a=av[i];auto v=[&](){if(++i==ac)throw std::runtime_error("missing value");return std::string(av[i]);};if(a=="--dataset")c.data=v();else if(a=="--assignments")c.assign=v();else if(a=="--output")c.out=v();else if(a=="--queries")c.nq=std::stoi(v());else if(a=="--k")c.k=std::stoi(v());else if(a=="--train-size")c.train=std::stoi(v());else if(a=="--iterations")c.iters=std::stoi(v());else if(a=="--nprobes")c.probes=nums(v());else if(a=="--help"){std::cout<<"Usage: residual_pq_experiment [--queries 50] [--k 100] [--train-size 20000] [--iterations 10]\n";std::exit(0);}else throw std::runtime_error("unknown option "+a);}return c;}

static std::vector<std::vector<int>> load_lists(const std::string&p,int nl,size_t n,std::vector<int>&cid){std::ifstream f(p);if(!f)throw std::runtime_error("cannot open assignments");std::vector<std::vector<int>>ls(nl);cid.assign(n,-1);long id,c;size_t z=0;while(f>>id>>c){if(id<0||(size_t)id>=n||c<0||c>=nl)throw std::runtime_error("bad assignment");ls[c].push_back(id);cid[id]=c;++z;}if(z!=n||std::find(cid.begin(),cid.end(),-1)!=cid.end())throw std::runtime_error("assignment mismatch");return ls;}
static std::vector<std::vector<float>> centers(const Matrix&xb,const std::vector<std::vector<int>>&ls){std::vector<std::vector<float>>c(ls.size(),std::vector<float>(xb.d));for(size_t z=0;z<ls.size();++z){for(int id:ls[z])for(int j=0;j<xb.d;++j)c[z][j]+=xb[id][j];for(float&v:c[z])v/=ls[z].size();}return c;}

// M=16 subspaces, K=16 centroids: 16 four-bit indices = 8 packed bytes.
struct PQ4 {
    int d,m=16,ds,k=16; std::vector<float> cb;
    explicit PQ4(int dim):d(dim),ds(dim/m),cb(m*k*ds){if(dim%m)throw std::runtime_error("dimension must divide 16");}
    void train(const Matrix&a,int iters,uint32_t seed){if(a.d!=d||a.rows()<static_cast<size_t>(k))throw std::runtime_error("bad PQ training matrix");std::mt19937 rng(seed);std::uniform_int_distribution<size_t>pick(0,a.rows()-1);std::vector<double>sum(k*ds);std::vector<int>cnt(k);
        for(int s=0;s<m;++s){for(int z=0;z<k;++z){const float*x=a[pick(rng)]+s*ds;std::copy(x,x+ds,cb.begin()+(s*k+z)*ds);}for(int it=0;it<iters;++it){std::fill(sum.begin(),sum.end(),0);std::fill(cnt.begin(),cnt.end(),0);for(size_t i=0;i<a.rows();++i){const float*x=a[i]+s*ds;int best=0;float bd=std::numeric_limits<float>::infinity();for(int z=0;z<k;++z){float dis=l2(x,&cb[(s*k+z)*ds],ds);if(dis<bd){bd=dis;best=z;}}++cnt[best];for(int j=0;j<ds;++j)sum[best*ds+j]+=x[j];}for(int z=0;z<k;++z){if(!cnt[z]){const float*x=a[pick(rng)]+s*ds;std::copy(x,x+ds,cb.begin()+(s*k+z)*ds);}else for(int j=0;j<ds;++j)cb[(s*k+z)*ds+j]=sum[z*ds+j]/cnt[z];}}}}
    void encode(const float*x,uint8_t*code,float*recon)const{for(int s=0;s<m;++s){int best=0;float bd=std::numeric_limits<float>::infinity();for(int z=0;z<k;++z){float dis=l2(x+s*ds,&cb[(s*k+z)*ds],ds);if(dis<bd){bd=dis;best=z;}}code[s]=best;std::copy(&cb[(s*k+best)*ds],&cb[(s*k+best+1)*ds],recon+s*ds);}}
    void decode(const uint8_t*code,float*out)const{for(int s=0;s<m;++s)std::copy(&cb[(s*k+code[s])*ds],&cb[(s*k+code[s]+1)*ds],out+s*ds);}
};

static Matrix residual_sample(const Matrix&xb,const std::vector<int>&cid,const std::vector<std::vector<float>>&cs,int n){Matrix a;a.d=xb.d;n=std::min<int>(n,xb.rows());a.x.resize((size_t)n*a.d);for(int z=0;z<n;++z){size_t id=(uint64_t)z*xb.rows()/n;for(int j=0;j<a.d;++j)a.x[(size_t)z*a.d+j]=xb[id][j]-cs[cid[id]][j];}return a;}

struct Codes {std::vector<uint8_t> c1,c2;std::vector<float>e1,e2;};
static Codes encode_all(const Matrix&xb,const std::vector<int>&cid,const std::vector<std::vector<float>>&cs,const PQ4&p1,const PQ4&p2){Codes z;z.c1.resize(xb.rows()*16);z.c2.resize(xb.rows()*16);z.e1.resize(xb.rows());z.e2.resize(xb.rows());std::vector<float>r(xb.d),h1(xb.d),rem(xb.d),h2(xb.d);std::cerr<<"Encoding 1M certified two-stage codes...\n";for(size_t i=0;i<xb.rows();++i){for(int j=0;j<xb.d;++j)r[j]=xb[i][j]-cs[cid[i]][j];p1.encode(r.data(),&z.c1[i*16],h1.data());float a=0;for(int j=0;j<xb.d;++j){rem[j]=r[j]-h1[j];a+=rem[j]*rem[j];}z.e1[i]=std::nextafter(std::sqrt(a)*(1+1e-6f)+1e-5f,std::numeric_limits<float>::infinity());p2.encode(rem.data(),&z.c2[i*16],h2.data());a=0;for(int j=0;j<xb.d;++j){float v=rem[j]-h2[j];a+=v*v;}z.e2[i]=std::nextafter(std::sqrt(a)*(1+1e-6f)+1e-5f,std::numeric_limits<float>::infinity());if((i+1)%200000==0)std::cerr<<"  encoded "<<i+1<<"\n";}return z;}
static float bound(const float*q,const std::vector<float>&cent,const PQ4&p1,const PQ4&p2,const Codes&z,size_t id,bool refined){std::vector<float>a(p1.d),b(p1.d);p1.decode(&z.c1[id*16],a.data());if(refined)p2.decode(&z.c2[id*16],b.data());float d2=0;for(int j=0;j<p1.d;++j){float x=cent[j]+a[j]+(refined?b[j]:0);float v=q[j]-x;d2+=v*v;}return std::max(0.0f,std::sqrt(d2)-(refined?z.e2[id]:z.e1[id]));}

int main(int ac,char**av)try{Config cfg=parse(ac,av);std::cerr<<"Loading SIFT1M...\n";Matrix xb=fvecs(cfg.data+"/sift_base.fvecs"),xq=fvecs(cfg.data+"/sift_query.fvecs",cfg.nq);auto gt=ivecs(cfg.data+"/sift_groundtruth.ivecs",xq.rows());std::vector<int>cid;auto ls=load_lists(cfg.assign,cfg.nlist,xb.rows(),cid);auto cs=centers(xb,ls);Matrix s1=residual_sample(xb,cid,cs,cfg.train);PQ4 p1(xb.d);std::cerr<<"Training coarse residual PQ (8-byte packed code)...\n";p1.train(s1,cfg.iters,42);Matrix s2=s1;std::vector<float>h(xb.d);std::vector<uint8_t>code(16);for(size_t i=0;i<s2.rows();++i){p1.encode(s1[i],code.data(),h.data());for(int j=0;j<xb.d;++j)s2.x[i*xb.d+j]-=h[j];}PQ4 p2(xb.d);std::cerr<<"Training refinement residual PQ (+8 bytes)...\n";p2.train(s2,cfg.iters,43);Codes z=encode_all(xb,cid,cs,p1,p2);std::ofstream out(cfg.out);out<<"k,nprobe,recall_at_k,method,avg_visited,level1_filter_ratio,level2_filter_ratio,exact_open_ratio,avg_exact_open,estimated_payload_bytes,baseline_payload_bytes,payload_reduction\n";std::cout<<"nprobe recall method       L1_filter L2_filter exact_open payload_reduction\n";
for(int np:cfg.probes){uint64_t vis=0,hits=0,f1=0,f2=0,exact8=0,exact16=0;for(size_t qi=0;qi<xq.rows();++qi){std::vector<int>pr(cfg.nlist);std::iota(pr.begin(),pr.end(),0);std::partial_sort(pr.begin(),pr.begin()+np,pr.end(),[&](int a,int b){return l2(xq[qi],cs[a].data(),xb.d)<l2(xq[qi],cs[b].data(),xb.d);});using P=std::pair<float,int>;std::priority_queue<P>top;for(int u=0;u<np;++u)for(int id:ls[pr[u]]){float d=l2(xq[qi],xb[id],xb.d);++vis;if((int)top.size()<cfg.k)top.push({d,id});else if(d<top.top().first){top.pop();top.push({d,id});}}float tau=std::sqrt(top.top().first);std::vector<int>ans;while(!top.empty()){ans.push_back(top.top().second);top.pop();}for(int j=0;j<cfg.k&&j<(int)gt[qi].size();++j)hits+=std::find(ans.begin(),ans.end(),gt[qi][j])!=ans.end();for(int u=0;u<np;++u)for(int id:ls[pr[u]]){bool a=bound(xq[qi],cs[cid[id]],p1,p2,z,id,false)>tau;if(a){++f1;continue;}++exact8;bool b=bound(xq[qi],cs[cid[id]],p1,p2,z,id,true)>tau;if(b)++f2;else ++exact16;}}
double base=(double)vis*xb.d*4,rec=(double)hits/(xq.rows()*cfg.k);auto row=[&](const std::string&name,double l1,double l2r,uint64_t ex,double bytes){out<<cfg.k<<','<<np<<','<<rec<<','<<name<<','<<(double)vis/xq.rows()<<','<<l1<<','<<l2r<<','<<(double)ex/vis<<','<<(double)ex/xq.rows()<<','<<bytes/xq.rows()<<','<<base/xq.rows()<<','<<1-bytes/base<<'\n';std::cout<<std::setw(6)<<np<<' '<<std::fixed<<std::setprecision(4)<<rec<<' '<<std::setw(12)<<name<<' '<<std::setw(9)<<l1<<' '<<std::setw(9)<<l2r<<' '<<std::setw(10)<<(double)ex/vis<<' '<<1-bytes/base<<'\n';};
// 4-byte certified error per transmitted level; hashes/IDs are excluded consistently with the SQ experiment.
row("RPQ8",(double)f1/vis,0,exact8,vis*12.0+exact8*512.0);row("RPQ16",0,(double)(f1+f2)/vis,exact16,vis*20.0+exact16*512.0);row("Progressive8to16",(double)f1/vis,(double)f2/vis,exact16,vis*12.0+(vis-f1)*12.0+exact16*512.0);
}std::cerr<<"Wrote "<<cfg.out<<"\n";return 0;}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

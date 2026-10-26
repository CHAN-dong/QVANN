#define IVF_GRANULARITY_NO_MAIN
#include "ivf_granularity.cpp"
#undef IVF_GRANULARITY_NO_MAIN

struct SafeSQ {
    int d,bits,levels;std::vector<float>lo,step,err;std::vector<uint8_t>code;
    SafeSQ(const Matrix&x,int b):d(x.d),bits(b),levels(1<<b),lo(d,std::numeric_limits<float>::infinity()),step(d),err(x.rows()),code(x.rows()*static_cast<size_t>(d)){
        std::vector<float>hi(d,-std::numeric_limits<float>::infinity());for(size_t i=0;i<x.rows();++i)for(int j=0;j<d;++j){lo[j]=std::min(lo[j],x[i][j]);hi[j]=std::max(hi[j],x[i][j]);}for(int j=0;j<d;++j)step[j]=(hi[j]-lo[j])/(levels-1);
        for(size_t i=0;i<x.rows();++i){float e2=0;for(int j=0;j<d;++j){int c=step[j]?std::clamp((int)std::lround((x[i][j]-lo[j])/step[j]),0,levels-1):0;code[i*static_cast<size_t>(d)+j]=c;float z=x[i][j]-(lo[j]+c*step[j]);e2+=z*z;}err[i]=std::nextafter(std::sqrt(e2)*(1+1e-6f)+1e-5f,std::numeric_limits<float>::infinity());}
    }
    float lb2(const float*q,int id)const{float s=0;for(int j=0;j<d;++j){float z=q[j]-(lo[j]+code[static_cast<size_t>(id)*d+j]*step[j]);s+=z*z;}float z=std::max(0.0f,std::sqrt(s)-err[id]);return z*z;}
    int record_bytes()const{return 4+(d*bits+7)/8+4;}
};
struct WQ {std::vector<int>lists;float tau2=0;};
struct Stat {uint32_t visits=0,open4=0,open6=0;};
struct Eval {double total=0,payload=0,local=0,routing=0,exact=0,ms=0;};

int main(int argc,char**argv)try{
    int K=16384,np=640;std::string output="workload_block_sq_ivf16384_np640.csv";for(int i=1;i<argc;++i){std::string a=argv[i];auto v=[&](){if(++i==argc)throw std::runtime_error("missing value");return std::string(argv[i]);};if(a=="--nlist")K=std::stoi(v());else if(a=="--nprobe")np=std::stoi(v());else if(a=="--output")output=v();else throw std::runtime_error("unknown argument "+a);}
    Matrix xb=fvecs("../dataset/sift/sift_base.fvecs",100000),xq=fvecs("../dataset/sift/sift_query.fvecs",350);std::cerr<<"Training IVF"<<K<<"...\n";auto ls=hkmeans(xb,K,6);auto cs=centers(xb,ls);SafeSQ q4(xb,4),q6(xb,6);
    std::vector<WQ>qs(xq.rows());for(size_t qi=0;qi<xq.rows();++qi){std::vector<int>ord(K);std::iota(ord.begin(),ord.end(),0);std::partial_sort(ord.begin(),ord.begin()+np,ord.end(),[&](int a,int b){return l2(xq[qi],cs[a].data(),xb.d)<l2(xq[qi],cs[b].data(),xb.d);});qs[qi].lists.assign(ord.begin(),ord.begin()+np);std::priority_queue<std::pair<float,int>>top;for(int c:qs[qi].lists)for(int id:ls[c]){float d=l2(xq[qi],xb[id],xb.d);if((int)top.size()<100)top.push({d,id});else if(d<top.top().first){top.pop();top.push({d,id});}}qs[qi].tau2=top.top().first;}
    Matrix xt;xt.d=xq.d;xt.x.assign(xq.x.begin(),xq.x.begin()+50*xq.d);auto gt=exact_gt(xb,xt,100);uint64_t hits=0;for(int qi=0;qi<50;++qi){std::priority_queue<std::pair<float,int>>top;for(int c:qs[qi].lists)for(int id:ls[c]){float d=l2(xq[qi],xb[id],xb.d);if((int)top.size()<100)top.push({d,id});else if(d<top.top().first){top.pop();top.push({d,id});}}std::unordered_set<int>a;while(!top.empty())a.insert(top.top().second),top.pop();for(int id:gt[qi])hits+=a.count(id);}double recall=hits/5000.0;
    auto evaluate=[&](int begin,int end,int bs,const std::vector<std::vector<uint8_t>>*choice,int fixed)->Eval{Eval e;auto st=std::chrono::steady_clock::now();for(int qi=begin;qi<end;++qi){e.routing+=proof_count(K,qs[qi].lists)*32.0+np*8.0;for(int c:qs[qi].lists){std::vector<int>opened;for(size_t p=0;p<ls[c].size();++p){int b=fixed?fixed:(*choice)[c][p/bs];const SafeSQ&q=b==4?q4:q6;e.payload+=q.record_bytes();if(q.lb2(xq[qi],ls[c][p])<=qs[qi].tau2)opened.push_back(p);}if(!fixed)e.payload+=(ls[c].size()+bs-1)/bs;e.exact+=opened.size();e.payload+=opened.size()*512.0;e.local+=proof_count(ls[c].size(),opened)*32.0;}}auto en=std::chrono::steady_clock::now();double n=end-begin;e.ms=std::chrono::duration<double,std::milli>(en-st).count()/n;e.payload/=n;e.local/=n;e.routing/=n;e.exact/=n;e.total=e.payload+e.local+e.routing;return e;};
    std::ofstream out(output);out<<"nlist,nprobe,scheme,block_size,margin,recall,sq6_block_ratio,avg_exact,payload_bytes,local_merkle_bytes,routing_merkle_bytes,total_vo_bytes,vo_generation_time_ms\n";
    auto emit=[&](std::string name,int bs,double margin,double ratio,const Eval&e){out<<K<<','<<np<<','<<name<<','<<bs<<','<<margin<<','<<recall<<','<<ratio<<','<<e.exact<<','<<e.payload<<','<<e.local<<','<<e.routing<<','<<e.total<<','<<e.ms<<'\n';std::cout<<name<<" bs="<<bs<<" margin="<<margin<<" sq6="<<ratio<<" exact="<<e.exact<<" VO="<<e.total/1024<<" KiB vo="<<e.ms<<" ms\n";};
    Eval f4=evaluate(0,50,1,nullptr,4),f6=evaluate(0,50,1,nullptr,6);emit("fixed-SQ4",0,0,0,f4);emit("fixed-SQ6",0,0,1,f6);
    for(int bs:{1,2,4,8,16}){std::vector<std::vector<Stat>>stats(K);for(int c=0;c<K;++c)stats[c].resize((ls[c].size()+bs-1)/bs);for(int qi=50;qi<250;++qi)for(int c:qs[qi].lists){for(auto&s:stats[c])++s.visits;for(size_t p=0;p<ls[c].size();++p){int id=ls[c][p];auto&s=stats[c][p/bs];s.open4+=q4.lb2(xq[qi],id)<=qs[qi].tau2;s.open6+=q6.lb2(xq[qi],id)<=qs[qi].tau2;}}
        double best=std::numeric_limits<double>::infinity(),best_margin=0,best_ratio=0;std::vector<std::vector<uint8_t>>best_choice;
        for(double margin:{0.0,8.0,16.0,32.0,64.0,128.0}){std::vector<std::vector<uint8_t>>choice(K);uint64_t six=0,blocks=0;for(int c=0;c<K;++c){choice[c].resize(stats[c].size(),4);for(size_t b=0;b<stats[c].size();++b){auto&s=stats[c][b];size_t count=std::min<size_t>(bs,ls[c].size()-b*bs);double gain=s.visits?512.0*(s.open4-s.open6)/s.visits-32.0*count:-1;if(s.visits>=3&&gain>margin)choice[c][b]=6,++six;++blocks;}}Eval val=evaluate(250,350,bs,&choice,0);if(val.total<best){best=val.total;best_margin=margin;best_ratio=blocks?six/double(blocks):0;best_choice=std::move(choice);}}
        Eval test=evaluate(0,50,bs,&best_choice,0);emit("workload-mixed",bs,best_margin,best_ratio,test);
    }
    std::cerr<<"Wrote "<<output<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

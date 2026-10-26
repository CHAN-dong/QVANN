#define IVF_GRANULARITY_NO_MAIN
#include "ivf_granularity.cpp"
#undef IVF_GRANULARITY_NO_MAIN

struct NestedSQ {
    int d, max_bits;
    std::vector<float> lo, span;
    std::vector<uint8_t> code;
    NestedSQ(const Matrix& x, int bits) : d(x.d), max_bits(bits),
        lo(d, std::numeric_limits<float>::infinity()), span(d),
        code(x.rows() * static_cast<size_t>(d)) {
        std::vector<float> hi(d, -std::numeric_limits<float>::infinity());
        for (size_t i=0;i<x.rows();++i) for (int j=0;j<d;++j) {
            lo[j]=std::min(lo[j],x[i][j]); hi[j]=std::max(hi[j],x[i][j]);
        }
        const int bins=1<<max_bits;
        for(int j=0;j<d;++j) span[j]=hi[j]-lo[j];
        for(size_t i=0;i<x.rows();++i) for(int j=0;j<d;++j){
            int c=0;
            if(span[j]>0){double u=(x[i][j]-lo[j])/span[j];c=std::clamp(static_cast<int>(std::floor(u*bins)),0,bins-1);}
            code[i*static_cast<size_t>(d)+j]=static_cast<uint8_t>(c);
        }
    }
    float lb2(const float* q,int id,int bits) const {
        const int shift=max_bits-bits,bins=1<<bits;float s=0;
        for(int j=0;j<d;++j){
            int p=code[static_cast<size_t>(id)*d+j]>>shift;
            float a=lo[j]+span[j]*(static_cast<float>(p)/bins);
            float b=lo[j]+span[j]*(static_cast<float>(p+1)/bins);
            float z=q[j]<a?a-q[j]:(q[j]>b?q[j]-b:0.0f);s+=z*z;
        }
        return std::nextafter(s,0.0f);
    }
};

struct QueryState { std::vector<int> lists; float tau2=0; };

int main(int argc,char**argv) try {
    std::string data="../dataset/sift",output="progressive_sq_ivf16384_np640.csv";const int base=100000,nq=50,k=100;int K=16384,np=640;
    for(int i=1;i<argc;++i){std::string a=argv[i];auto value=[&](){if(++i==argc)throw std::runtime_error("missing argument value");return std::string(argv[i]);};if(a=="--nlist")K=std::stoi(value());else if(a=="--nprobe")np=std::stoi(value());else if(a=="--output")output=value();else if(a=="--dataset")data=value();else throw std::runtime_error("unknown argument "+a);}
    Matrix xb=fvecs(data+"/sift_base.fvecs",base),xq=fvecs(data+"/sift_query.fvecs",nq);
    auto gt=exact_gt(xb,xq,k);std::cerr<<"Training IVF"<<K<<"...\n";auto ls=hkmeans(xb,K,6);auto cs=centers(xb,ls);
    SQ4 sq4(xb);NestedSQ nested(xb,6);std::vector<QueryState> qs(xq.rows());uint64_t hits=0,visited=0;
    for(size_t qi=0;qi<xq.rows();++qi){std::vector<int>ord(K);std::iota(ord.begin(),ord.end(),0);std::partial_sort(ord.begin(),ord.begin()+np,ord.end(),[&](int a,int b){return l2(xq[qi],cs[a].data(),xb.d)<l2(xq[qi],cs[b].data(),xb.d);});qs[qi].lists.assign(ord.begin(),ord.begin()+np);std::priority_queue<std::pair<float,int>>top;for(int c:qs[qi].lists)for(int id:ls[c]){float d=l2(xq[qi],xb[id],xb.d);++visited;if((int)top.size()<k)top.push({d,id});else if(d<top.top().first){top.pop();top.push({d,id});}}qs[qi].tau2=top.top().first;std::unordered_set<int>ans;while(!top.empty())ans.insert(top.top().second),top.pop();for(int id:gt[qi])hits+=ans.count(id);}
    uint64_t bound_violations=0;float max_excess=0;for(size_t qi=0;qi<xq.rows();++qi)for(int c:qs[qi].lists)for(int id:ls[c]){float exact=l2(xq[qi],xb[id],xb.d);for(int bits=2;bits<=6;++bits){float lb=nested.lb2(xq[qi],id,bits);if(lb>exact){++bound_violations;max_excess=std::max(max_excess,lb-exact);}}}if(bound_violations)throw std::runtime_error("unsafe nested bound: violations="+std::to_string(bound_violations)+", max_excess="+std::to_string(max_excess));std::cerr<<"Nested-bound safety check passed.\n";
    const double recall=hits/(xq.rows()*double(k)),avg_vis=visited/double(xq.rows());
    std::ofstream out(output);out<<"nlist,nprobe,scheme,recall,avg_visited,avg_after_l1,avg_after_l2,avg_after_l3,avg_exact,payload_bytes,local_merkle_bytes,routing_merkle_bytes,total_vo_bytes,vo_generation_time_ms\n";
    auto run_nested=[&](std::string name,std::vector<int>levels){double payload=0,local=0,routing=0,vo_ns=0;std::vector<uint64_t>after(levels.size());
        for(size_t qi=0;qi<xq.rows();++qi){auto st=std::chrono::steady_clock::now();routing+=proof_count(K,qs[qi].lists)*32.0+np*8.0;for(int c:qs[qi].lists){const auto&list=ls[c];std::vector<int>active(list.size());std::iota(active.begin(),active.end(),0);payload+=list.size()*(4.0+std::ceil(xb.d*levels[0]/8.0));std::vector<int>next;for(int p:active)if(nested.lb2(xq[qi],list[p],levels[0])<=qs[qi].tau2)next.push_back(p);active.swap(next);after[0]+=active.size();for(size_t l=1;l<levels.size();++l){local+=proof_count(list.size(),active)*32.0;payload+=active.size()*std::ceil(xb.d*(levels[l]-levels[l-1])/8.0);next.clear();for(int p:active)if(nested.lb2(xq[qi],list[p],levels[l])<=qs[qi].tau2)next.push_back(p);active.swap(next);after[l]+=active.size();}local+=proof_count(list.size(),active)*32.0;payload+=active.size()*512.0;}auto en=std::chrono::steady_clock::now();vo_ns+=std::chrono::duration<double,std::nano>(en-st).count();}
        double qn=xq.rows(),total=(payload+local+routing)/qn;out<<K<<','<<np<<','<<name<<','<<recall<<','<<avg_vis;for(int i=0;i<3;++i)out<<','<<(i<(int)after.size()?after[i]/qn:0);out<<','<<after.back()/qn<<','<<payload/qn<<','<<local/qn<<','<<routing/qn<<','<<total<<','<<vo_ns/qn/1e6<<'\n';std::cout<<name<<" exact="<<after.back()/qn<<" VO="<<total/1024<<" KiB vo="<<vo_ns/qn/1e6<<" ms\n";};
    auto run_sq4=[&](){double payload=0,local=0,routing=0,vo_ns=0;uint64_t exact=0;for(size_t qi=0;qi<xq.rows();++qi){auto st=std::chrono::steady_clock::now();routing+=proof_count(K,qs[qi].lists)*32.0+np*8.0;for(int c:qs[qi].lists){std::vector<int>opened;for(size_t p=0;p<ls[c].size();++p)if(sq4.lb(xq[qi],ls[c][p])<=std::sqrt(qs[qi].tau2))opened.push_back(p);exact+=opened.size();payload+=ls[c].size()*72.0+opened.size()*512.0;local+=proof_count(ls[c].size(),opened)*32.0;}auto en=std::chrono::steady_clock::now();vo_ns+=std::chrono::duration<double,std::nano>(en-st).count();}double qn=xq.rows(),total=(payload+local+routing)/qn;out<<K<<','<<np<<",SQ4-flat,"<<recall<<','<<avg_vis<<','<<exact/qn<<",0,0,"<<exact/qn<<','<<payload/qn<<','<<local/qn<<','<<routing/qn<<','<<total<<','<<vo_ns/qn/1e6<<'\n';std::cout<<"SQ4-flat exact="<<exact/qn<<" VO="<<total/1024<<" KiB vo="<<vo_ns/qn/1e6<<" ms\n";};
    run_sq4();run_nested("SQ3->SQ4",{3,4});run_nested("SQ3->SQ4->SQ6",{3,4,6});run_nested("SQ4->SQ6",{4,6});run_nested("SQ2->SQ3->SQ4->SQ6",{2,3,4,6});
    std::cerr<<"Wrote "<<output<<"\n";return 0;
} catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

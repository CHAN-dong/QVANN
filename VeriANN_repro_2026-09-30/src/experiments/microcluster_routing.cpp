#define IVF_GRANULARITY_NO_MAIN
#include "ivf_granularity.cpp"
#undef IVF_GRANULARITY_NO_MAIN

#include <chrono>
#include <sstream>

// Query-protocol experiment for authenticated microcluster IVF.  The flat
// protocol checks every fine centroid.  The two-stage protocol first selects
// coarse centroid groups, then ranks only fine centroids in those groups.
// Both choices are deterministic from public index metadata and the query, so
// unselected microclusters need no per-object exclusion records in the VO.

struct MRCfg {
    std::string data="../dataset/sift",output="microcluster_routing.csv";
    int base=100000,queries=100,k=100,fine=65536,probe=416,coarse=1024,iters=6,repeats=3;
    std::vector<int>coarse_probes{16,32,64,128,256};
};

static std::vector<int> mr_ints(const std::string&s){
    std::vector<int>out;std::stringstream in(s);std::string x;
    while(std::getline(in,x,','))out.push_back(std::stoi(x));
    return out;
}

static MRCfg mr_args(int argc,char**argv){
    MRCfg c;
    for(int i=1;i<argc;++i){
        std::string a=argv[i];auto value=[&](){if(++i==argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
        if(a=="--dataset")c.data=value();else if(a=="--output")c.output=value();
        else if(a=="--base")c.base=std::stoi(value());else if(a=="--queries")c.queries=std::stoi(value());
        else if(a=="--k")c.k=std::stoi(value());else if(a=="--fine")c.fine=std::stoi(value());
        else if(a=="--probe")c.probe=std::stoi(value());else if(a=="--coarse")c.coarse=std::stoi(value());
        else if(a=="--coarse-probes")c.coarse_probes=mr_ints(value());else if(a=="--iters")c.iters=std::stoi(value());
        else if(a=="--repeats")c.repeats=std::stoi(value());else throw std::runtime_error("unknown option "+a);
    }
    if(c.base<c.fine||c.fine<c.probe||c.fine<c.coarse||c.queries<1||c.k<1||c.repeats<1)throw std::runtime_error("invalid configuration");
    return c;
}

struct MRResult {
    std::string scheme;int coarse_probe=0;double recall=0,candidates=0,visited=0,route_ms=0,scan_ms=0;
    uint64_t checksum=0;
};

static MRResult mr_evaluate(const Matrix&xb,const Matrix&xq,const std::vector<std::vector<int>>&fine_lists,
        const std::vector<std::vector<float>>&fine_centers,const std::vector<std::vector<int>>&groups,
        const std::vector<std::vector<float>>&coarse_centers,const std::vector<std::vector<int>>&gt,
        int nprobe,int coarse_probe,int k,int repeats){
    const bool flat=coarse_probe<0;MRResult out;out.scheme=flat?"flat-fine":"two-stage";out.coarse_probe=flat?(int)groups.size():coarse_probe;
    uint64_t reference_checksum=0;
    for(int rep=0;rep<repeats;++rep){
        uint64_t hits=0,visited=0,checksum=0,candidates_total=0;double route_ns=0,scan_ns=0;
        for(size_t qi=0;qi<xq.rows();++qi){
            auto start=std::chrono::steady_clock::now();std::vector<int>selected,candidates;
            if(flat){candidates.resize(fine_lists.size());std::iota(candidates.begin(),candidates.end(),0);}
            else{
                std::vector<int>coarse_order(groups.size());std::iota(coarse_order.begin(),coarse_order.end(),0);
                auto coarse_cmp=[&](int a,int b){float x=l2(xq[qi],coarse_centers[a].data(),xb.d),y=l2(xq[qi],coarse_centers[b].data(),xb.d);return x==y?a<b:x<y;};
                std::partial_sort(coarse_order.begin(),coarse_order.begin()+coarse_probe,coarse_order.end(),coarse_cmp);
                for(int z=0;z<coarse_probe;++z)candidates.insert(candidates.end(),groups[coarse_order[z]].begin(),groups[coarse_order[z]].end());
            }
            if((int)candidates.size()<nprobe)throw std::runtime_error("coarse shortlist is smaller than nprobe");
            auto fine_cmp=[&](int a,int b){float x=l2(xq[qi],fine_centers[a].data(),xb.d),y=l2(xq[qi],fine_centers[b].data(),xb.d);return x==y?a<b:x<y;};
            std::partial_sort(candidates.begin(),candidates.begin()+nprobe,candidates.end(),fine_cmp);
            selected.assign(candidates.begin(),candidates.begin()+nprobe);candidates_total+=candidates.size();
            route_ns+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
            start=std::chrono::steady_clock::now();std::priority_queue<std::pair<float,int>>top;
            for(int c:selected)for(int id:fine_lists[c]){float d=l2(xq[qi],xb[id],xb.d);++visited;
                if((int)top.size()<k)top.push({d,id});else if(d<top.top().first||(d==top.top().first&&id<top.top().second)){top.pop();top.push({d,id});}}
            std::unordered_set<int>answer;while(!top.empty()){answer.insert(top.top().second);checksum+=(uint64_t)top.top().second+1;top.pop();}
            scan_ns+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
            if(rep==0)for(int id:gt[qi])hits+=answer.count(id);
        }
        if(rep==0){out.recall=hits/(xq.rows()*(double)k);reference_checksum=checksum;}
        else if(checksum!=reference_checksum)throw std::runtime_error("non-deterministic routing result");
        out.candidates+=candidates_total/(double)xq.rows();out.visited+=visited/(double)xq.rows();
        out.route_ms+=route_ns/xq.rows()/1e6;out.scan_ms+=scan_ns/xq.rows()/1e6;out.checksum=checksum;
    }
    out.candidates/=repeats;out.visited/=repeats;out.route_ms/=repeats;out.scan_ms/=repeats;return out;
}

int main(int argc,char**argv)try{
    auto cfg=mr_args(argc,argv);Matrix xb=fvecs(cfg.data+"/sift_base.fvecs",cfg.base),xq=fvecs(cfg.data+"/sift_query.fvecs",cfg.queries);
    auto gt=exact_gt(xb,xq,cfg.k);std::cerr<<"Training fine microclusters...\n";auto fine_lists=hkmeans(xb,cfg.fine,cfg.iters);auto fine_centers=centers(xb,fine_lists);
    Matrix center_matrix;center_matrix.d=xb.d;center_matrix.x.reserve((size_t)cfg.fine*xb.d);
    for(const auto&c:fine_centers)center_matrix.x.insert(center_matrix.x.end(),c.begin(),c.end());
    std::cerr<<"Grouping public fine centroids for two-stage routing...\n";auto groups=hkmeans(center_matrix,cfg.coarse,cfg.iters);auto coarse_centers=centers(center_matrix,groups);
    std::vector<MRResult>rows;rows.push_back(mr_evaluate(xb,xq,fine_lists,fine_centers,groups,coarse_centers,gt,cfg.probe,-1,cfg.k,cfg.repeats));
    for(int cp:cfg.coarse_probes)if(cp<=cfg.coarse)rows.push_back(mr_evaluate(xb,xq,fine_lists,fine_centers,groups,coarse_centers,gt,cfg.probe,cp,cfg.k,cfg.repeats));
    std::ofstream out(cfg.output);if(!out)throw std::runtime_error("cannot open output");
    out<<"scheme,base,fine_nlist,nprobe,coarse_nlist,coarse_nprobe,recall,avg_candidate_centroids,avg_visited,route_ms,scan_ms,total_query_ms,checksum\n"<<std::setprecision(12);
    for(const auto&r:rows){out<<r.scheme<<','<<cfg.base<<','<<cfg.fine<<','<<cfg.probe<<','<<cfg.coarse<<','<<r.coarse_probe<<','<<r.recall<<','<<r.candidates<<','<<r.visited<<','<<r.route_ms<<','<<r.scan_ms<<','<<r.route_ms+r.scan_ms<<','<<r.checksum<<'\n';
        std::cout<<r.scheme<<" coarse_probe="<<r.coarse_probe<<" recall="<<std::fixed<<std::setprecision(4)<<r.recall<<" candidates="<<std::setprecision(1)<<r.candidates<<" visited="<<r.visited<<" query="<<std::setprecision(3)<<r.route_ms+r.scan_ms<<" ms\n";}
    std::cerr<<"Wrote "<<cfg.output<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

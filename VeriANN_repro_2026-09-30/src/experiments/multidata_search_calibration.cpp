// Search-only calibration. Includes the exact existing protocol search code;
// timing/VO measurements always come from the unchanged full protocol binaries.
#if PILOT_KIND == 1
#define main existing_protocol_main
#include "../../ANNProof-24FGCS/annproof.cpp"
#undef main
#elif PILOT_KIND == 2
#define main existing_protocol_main
#include "../../VLAH-25TrustCom/vlah.cpp"
#undef main
#elif PILOT_KIND == 3
#include "../../build/multidata_lut_include.cpp"
#endif

int main(int argc,char**argv)try{
    namespace p=paper_repro;
    auto c=p::args(argc,argv);p::keccak=PILOT_KIND==1;p::build_threads=c.buildthreads;
    auto base=vann::read_fvecs(c.dataset+"/sift_base.fvecs",c.base),queries=p::read_queries(c);
    p::require(base.rows()==size_t(c.base)&&base.dimension==queries.dimension,"dataset mismatch");
    auto data=p::data_hash(base);auto gt=p::ground_truth(base,queries,*std::max_element(c.ks.begin(),c.ks.end()));
    std::cout<<"[GT] ready N="<<c.base<<" dim="<<base.dimension<<std::endl;
#if PILOT_KIND == 1
    std::vector<int>ids(c.base);std::iota(ids.begin(),ids.end(),0);
    std::string cache=c.cache+"/"+p::hex(data).substr(0,16)+"_M"+std::to_string(c.M)+"_efc"+std::to_string(c.efc)+".index";
    p::Graph graph(base,ids,c.M,c.efc,cache);
#elif PILOT_KIND == 2
    LSH lsh(base.dimension,c.width,c.seed);
    std::vector<std::pair<uint64_t,int>>order;
    for(int id=0;id<c.base;++id)order.emplace_back(lsh.code(base.row(id)),id);
    std::sort(order.begin(),order.end());std::vector<Bucket>buckets;std::vector<Descriptor>ds;
    for(int begin=0;begin<c.base;begin+=c.bucket){
        int end=std::min(begin+c.bucket,c.base);std::vector<int>ids;p::Bytes idbytes;
        for(int j=begin;j<end;++j){ids.push_back(order[j].second);p::u32(idbytes,order[j].second);}
        std::string cache=c.cache+"/"+p::hex(data).substr(0,16)+"_"+p::hex(p::hash(idbytes)).substr(0,16)+"_M"+std::to_string(c.M)+"_efc"+std::to_string(c.efc)+".index";
        Bucket b;b.graph=std::make_unique<p::Graph>(base,ids,c.M,c.efc,cache);
        ds.push_back({uint32_t(buckets.size()),uint32_t(ids.size()),b.graph->entry,b.graph->maxlevel,order[begin].first,order[end-1].first,{}});
        buckets.push_back(std::move(b));std::cout<<"[bucket] "<<buckets.size()<<std::endl;
    }
#elif PILOT_KIND == 3
    std::string cache=c.cache+"/"+p::hex(data).substr(0,16)+"_M"+std::to_string(c.M)+"_efc"+std::to_string(c.efc)+".index";
    omp_set_num_threads(c.buildthreads);auto h=vann::build_hnsw(base,c.M,c.efc,cache);omp_set_num_threads(1);
    optimized::Codec codec(h);
#endif
    std::cout<<"[index] ready"<<std::endl;
    std::ofstream out(c.output+"/pilot.csv");out<<"query,k,ef_search,probes,recall_at_k,visited,expanded,valid\n"<<std::setprecision(12);
    for(int k:c.ks)for(int ef:c.efs)for(int probes:c.probes){
        for(int qi=0;qi<c.queries;++qi){
            const float*q=queries.row(qi);std::vector<uint32_t>answer;size_t visited=0,expanded=0;
            int actualef=std::max(ef,k),actualprobes=1;
#if PILOT_KIND == 1
            auto s=p::search([&](uint32_t id)->const p::Record&{return graph.records.at(id);},graph.entry,graph.maxlevel,q,base.dimension,k,actualef);
            for(auto[d,id]:s.best)answer.push_back(id);visited=s.touched.size();expanded=s.trace.size();
#elif PILOT_KIND == 2
            auto code=lsh.code(q);auto it=std::upper_bound(ds.begin(),ds.end(),code,[](uint64_t v,const Descriptor&d){return v<d.low;});
            uint32_t main=it==ds.begin()?0:uint32_t(it-ds.begin()-1);auto selected=select_buckets(main,buckets.size(),probes);actualprobes=selected.size();
            std::vector<std::pair<float,uint32_t>>candidates;
            for(auto id:selected){auto&g=*buckets[id].graph;
                auto s=p::search([&](uint32_t local)->const p::Record&{return g.records.at(local);},g.entry,g.maxlevel,q,base.dimension,k,actualef);
                visited+=s.touched.size();expanded+=s.trace.size();for(auto[d,local]:s.best)candidates.emplace_back(d,g.records[local].id);
            }
            std::sort(candidates.begin(),candidates.end());for(int j=0;j<k&&j<int(candidates.size());++j)answer.push_back(candidates[j].second);
#elif PILOT_KIND == 3
            actualef=ef;
            auto t=optimized::walk(codec,q,ef,h.index->hnsw.entry_point,h.index->hnsw.max_level,[&](uint32_t id){return codec.sq(id);},[&](uint32_t id,uint32_t level){return optimized::neighbors(h,id,level);});
            std::vector<std::pair<float,uint32_t>>exact;for(auto id:t.disclosed)exact.emplace_back(vann::squared_l2(q,h.vector(id),h.dimension()),id);
            std::sort(exact.begin(),exact.end());for(int j=0;j<k&&j<int(exact.size());++j)answer.push_back(exact[j].second);
            visited=t.disclosed.size();expanded=t.expanded.size();
#endif
            int hits=0;for(auto id:answer)hits+=std::find(gt[qi].begin(),gt[qi].begin()+k,id)!=gt[qi].begin()+k;
            out<<qi+c.queryoffset<<','<<k<<','<<actualef<<','<<actualprobes<<','<<double(hits)/k<<','<<visited<<','<<expanded<<','<<(answer.size()==size_t(k))<<'\n';
        }
        out.flush();std::cout<<"[pilot] k="<<k<<" ef/E="<<ef<<" probes="<<probes<<std::endl;
    }
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}

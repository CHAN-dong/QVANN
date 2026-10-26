#define main relaxed_hnsw_embedded_main
#include "relaxed_hnsw_harvest.cpp"
#undef main

namespace {
struct Breakdown {
    double sq_payload=0,sq_proof=0,raw_payload=0,raw_proof=0;
    double adjacency_payload=0,adjacency_proof=0,metadata=0;
};

Breakdown account(const vann::HnswIndex& h,const NestedSQ& sq,const Trace& t,int bits){
    Breakdown b;const size_t n=h.size();
    b.sq_payload=t.discovered.size()*sq.bytes(bits);
    b.sq_proof=proof_count(n,t.discovered)*32.0;
    b.raw_payload=t.raw.size()*h.dimension()*sizeof(float);
    b.raw_proof=t.raw.empty()?0:proof_count(n,t.raw)*32.0;
    std::set<std::pair<int,int>> opened;
    for(auto x:t.opened)opened.insert({x.local_id,x.level});
    b.metadata=32+t.discovered.size()*4.0+t.raw.size()*4.0;
    for(int id:t.expanded){
        b.metadata+=8;int levels=h.index->hnsw.levels[id];
        for(int level=0;level<levels;++level){
            b.metadata+=5;
            if(opened.count({id,level}))b.adjacency_payload+=h.neighbors(id,level).size()*4.0;
            else b.adjacency_proof+=32;
        }
    }
    b.adjacency_proof+=proof_count(n,t.expanded)*32.0;
    return b;
}

void add(Breakdown& a,const Breakdown& b){a.sq_payload+=b.sq_payload;a.sq_proof+=b.sq_proof;a.raw_payload+=b.raw_payload;a.raw_proof+=b.raw_proof;a.adjacency_payload+=b.adjacency_payload;a.adjacency_proof+=b.adjacency_proof;a.metadata+=b.metadata;}

void print(const std::string&name,const Breakdown&a,double nq){
    auto k=[&](double x){return x/nq/1024.0;};
    double proofs=a.sq_proof+a.raw_proof+a.adjacency_proof;
    double total=proofs+a.sq_payload+a.raw_payload+a.adjacency_payload+a.metadata;
    std::cout<<name<<','<<k(a.sq_payload)<<','<<k(a.sq_proof)<<','<<k(a.raw_payload)<<','<<k(a.raw_proof)<<','<<k(a.adjacency_payload)<<','<<k(a.adjacency_proof)<<','<<k(a.metadata)<<','<<k(proofs)<<','<<k(total)<<'\n';
}
}

int main()try{
    using namespace vann;Config c;c.queries=100;
    Matrix base=read_fvecs(c.dataset+"/sift_base.fvecs",c.base),queries=read_fvecs(c.dataset+"/sift_query.fvecs",c.queries);
    HnswIndex h=build_hnsw(base,c.M,c.efc,c.cache);base.values.clear();base.values.shrink_to_fit();NestedSQ sq(h);
    Breakdown basic,relaxed;
    for(int qi=0;qi<c.queries;++qi){
        HnswTrace x=search_hnsw(h,queries.row(qi),53,c.k);Trace t;
        t.discovered=x.disclosed;t.opened=x.opened;t.answer=x.answer;
        for(auto o:x.opened)t.expanded.push_back(o.local_id);
        std::sort(t.expanded.begin(),t.expanded.end());t.expanded.erase(std::unique(t.expanded.begin(),t.expanded.end()),t.expanded.end());
        float tau=0;for(int id:t.answer)tau=std::max(tau,squared_l2(queries.row(qi),h.vector(id),h.dimension()));
        for(int id:t.discovered)if(sq.lower2(queries.row(qi),id,6)<=tau)t.raw.push_back(id);
        add(basic,account(h,sq,t,6));
        add(relaxed,account(h,sq,search(h,sq,queries.row(qi),c.k,6,5,12,c.upper_steps),6));
    }
    std::cout<<"scheme,sq6_payload_kib,sq6_proof_kib,raw_payload_kib,raw_proof_kib,adjacency_payload_kib,adjacency_proof_kib,ids_metadata_kib,total_merkle_proof_kib,total_vo_kib\n";
    print("Basic-HNSW",basic,c.queries);print("Relaxed-HNSW-SQ6-b5-r12",relaxed,c.queries);return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

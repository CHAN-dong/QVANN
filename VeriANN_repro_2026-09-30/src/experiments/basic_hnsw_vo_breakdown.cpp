#define main multilevel_embedded_main
#include "../merkle_hnsw/multilevel_quantization.cpp"
#undef main

namespace {
size_t sparse_count(size_t n,const std::vector<int>&sel){if(!n)return 0;size_t p=1;while(p<n)p<<=1;std::vector<size_t>cur;for(int x:sel)if(x>=0&&size_t(x)<n)cur.push_back(x);std::sort(cur.begin(),cur.end());cur.erase(std::unique(cur.begin(),cur.end()),cur.end());if(cur.empty())return 1;size_t ans=0;while(p>1){std::vector<size_t>up;for(size_t i=0;i<cur.size();){size_t x=cur[i];if(i+1<cur.size()&&cur[i+1]==(x^1U))i+=2;else{++ans;++i;}size_t z=x>>1;if(up.empty()||up.back()!=z)up.push_back(z);}cur.swap(up);p>>=1;}return ans;}
}

int main()try{using namespace vann;Config c;c.cache="../merkle_hnsw/sift100k_M16_efc100.index";Matrix base=read_fvecs(c.dataset+"/sift_base.fvecs",c.base),queries=read_fvecs(c.dataset+"/sift_query.fvecs",c.queries);HnswIndex h=build_hnsw(base,c.M,c.efc,c.cache);base.values.clear();base.values.shrink_to_fit();NestedSQ sq(h);double sp=0,sproof=0,rp=0,rproof=0,ap=0,aproof=0,meta=0,exact=0;
for(int qi=0;qi<c.queries;++qi){Trace t=search(h,sq,queries.row(qi),c.ef,c.k,Scheme{"SQ6",{6}},true);sp+=t.disclosed.size()*code_bytes(h.dimension(),6);sproof+=sparse_count(h.size(),t.disclosed)*32.0;rp+=t.exact.size()*h.dimension()*sizeof(float);rproof+=sparse_count(h.size(),t.exact)*32.0;exact+=t.exact.size();meta+=32+t.disclosed.size()*4.0+t.exact.size()*4.0+t.expanded.size()*8.0;for(int id:t.expanded){int levels=h.index->hnsw.levels[id];for(int level=0;level<levels;++level){meta+=5;if(is_opened(t.opened,id,level))ap+=h.neighbors(id,level).size()*4.0;else aproof+=32;}}aproof+=sparse_count(h.size(),t.expanded)*32.0;}
double nq=c.queries,proof=sproof+rproof+aproof,total=proof+sp+rp+ap+meta;auto k=[&](double x){return x/nq/1024.0;};std::cout<<"avg_exact,sq6_payload_kib,sq6_proof_kib,raw_payload_kib,raw_proof_kib,adjacency_payload_kib,adjacency_proof_kib,ids_metadata_kib,total_merkle_proof_kib,total_vo_kib\n"<<exact/nq<<','<<k(sp)<<','<<k(sproof)<<','<<k(rp)<<','<<k(rproof)<<','<<k(ap)<<','<<k(aproof)<<','<<k(meta)<<','<<k(proof)<<','<<k(total)<<'\n';return 0;}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

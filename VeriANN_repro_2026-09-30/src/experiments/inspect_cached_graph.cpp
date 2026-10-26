#include "../verifiable_ann_common.hpp"
int main(int argc,char**argv)try{
    if(argc!=4)throw std::runtime_error("usage: inspect_cached_graph index d N");
    auto index=vann::load_hnsw(argv[1],std::stoi(argv[2]),std::stoull(argv[3]));
    if(!index)throw std::runtime_error("missing index");
    vann::HnswIndex h;h.index=std::move(index);
    for(bool upper:{false,true}){
        std::vector<uint8_t>seen(h.size());std::vector<int>queue{h.index->hnsw.entry_point};seen[queue[0]]=1;
        for(size_t i=0;i<queue.size();++i){int id=queue[i],levels=upper?h.index->hnsw.levels[id]:1;
            for(int level=0;level<levels;++level)for(int nb:h.neighbors(id,level))if(!seen[nb]){seen[nb]=1;queue.push_back(nb);}}
        std::cout<<(upper?"all_levels":"level0")<<" reachable="<<queue.size()<<" total="<<h.size()<<std::endl;
    }
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}

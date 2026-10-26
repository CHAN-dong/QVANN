#define main original_main_unused
#include "/mnt/d/paper_source/work_9/VeriANN_repro_2026-09-30/VeriANN_repro_2026-09-30/ANNProof-24FGCS/annproof.cpp"
#undef main

#include <fstream>
namespace audit {
template<class T> void scalar(std::ostream&f,const T&x){f.write(reinterpret_cast<const char*>(&x),sizeof(x));}
template<class T> void vec(std::ostream&f,const std::vector<T>&v){uint64_t n=v.size();scalar(f,n);f.write(reinterpret_cast<const char*>(v.data()),v.size()*sizeof(T));}
void tree(std::ostream&f,const paper_repro::Tree&t){scalar(f,t.n);uint64_t n=t.levels.size();scalar(f,n);for(auto&l:t.levels)vec(f,l);}
void graph(std::ostream&f,const paper_repro::Graph&g){scalar(f,g.entry);scalar(f,g.maxlevel);uint64_t n=g.records.size();scalar(f,n);for(uint32_t i=0;i<n;++i){paper_repro::Bytes b;paper_repro::write_record(b,i,g.records[i]);vec(f,b);}}
void result(const paper_repro::Config&c,double index,double ads,uint64_t search_bytes,uint64_t auth_bytes){
 struct rusage u{};getrusage(RUSAGE_SELF,&u);std::ofstream f(c.output+"/offline.json");f<<std::setprecision(17)<<"{\"index_seconds\":"<<index/1000<<",\"ads_seconds\":"<<ads/1000<<",\"construct_seconds\":"<<(index+ads)/1000<<",\"search_bytes\":"<<search_bytes<<",\"auth_bytes\":"<<auth_bytes<<",\"total_bytes\":"<<search_bytes+auth_bytes<<",\"peak_rss_KiB\":"<<u.ru_maxrss<<",\"threads\":8}";std::cout<<"OFFLINE_DONE "<<(index+ads)/1000<<std::endl;
}
}

int main(int argc,char**argv)try{
using namespace paper_repro;keccak=true;Config c=args(argc,argv);build_threads=8;
auto base=vann::read_fvecs(c.dataset+"/sift_base.fvecs",c.base);auto data=data_hash(base);
std::vector<int>ids(c.base);std::iota(ids.begin(),ids.end(),0);auto cache=c.cache+"/global.index";require(!std::filesystem::exists(cache),"fresh cache required");
std::cout<<"START graph "<<c.base<<std::endl;auto t=Clock::now();Graph g(base,ids,c.M,c.efc,cache);double ix=ms(t);t=Clock::now();ADS a(g,c.shard,base.dimension);double ad=ms(t);
std::ofstream f(c.output+"/serving.snapshot",std::ios::binary);audit::graph(f,g);uint64_t sb=f.tellp();for(auto*v:{&a.nodes,&a.identifiers}){uint64_t n=v->size();audit::scalar(f,n);for(auto&tr:*v)audit::tree(f,tr);}audit::scalar(f,a.anchor.dim);audit::scalar(f,a.anchor.n);audit::scalar(f,a.anchor.entry);audit::scalar(f,a.anchor.maxlevel);audit::scalar(f,a.anchor.shard);audit::vec(f,a.anchor.nodes);audit::vec(f,a.anchor.identifiers);uint64_t total=f.tellp();f.close();require(bool(f),"snapshot write failed");audit::result(c,ix,ad,sb,total-sb);return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}

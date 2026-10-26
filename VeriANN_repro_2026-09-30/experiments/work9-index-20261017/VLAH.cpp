#define main original_main_unused
#include "/mnt/d/paper_source/work_9/VeriANN_repro_2026-09-30/VeriANN_repro_2026-09-30/VLAH-25TrustCom/vlah.cpp"
#undef main

#include <fstream>
namespace audit {
template<class T> void scalar(std::ostream&f,const T&x){f.write(reinterpret_cast<const char*>(&x),sizeof(x));}
template<class T> void vec(std::ostream&f,const std::vector<T>&v){uint64_t n=v.size();scalar(f,n);f.write(reinterpret_cast<const char*>(v.data()),v.size()*sizeof(T));}
void tree(std::ostream&f,const paper_repro::Tree&t){scalar(f,t.n);uint64_t n=t.levels.size();scalar(f,n);for(auto&l:t.levels)vec(f,l);}
void graph(std::ostream&f,const paper_repro::Graph&g){scalar(f,g.entry);scalar(f,g.maxlevel);uint64_t n=g.records.size();scalar(f,n);for(uint32_t i=0;i<n;++i){paper_repro::Bytes b;paper_repro::write_record(b,i,g.records[i]);vec(f,b);}}
void result(const paper_repro::Config&c,double index,double ads,uint64_t search_bytes,uint64_t auth_bytes){
 struct rusage u{};getrusage(RUSAGE_SELF,&u);std::ofstream f(c.output+"/offline.json");f<<std::setprecision(17)<<"{\"index_seconds\":"<<index/1000<<",\"ads_seconds\":"<<ads/1000<<",\"construct_seconds\":"<<(index+ads)/1000<<",\"search_bytes\":"<<search_bytes<<",\"auth_bytes\":"<<auth_bytes<<",\"total_bytes\":"<<search_bytes+auth_bytes<<",\"peak_rss_KiB\":"<<u.ru_maxrss<<",\"threads\":1}";std::cout<<"OFFLINE_DONE "<<(index+ads)/1000<<std::endl;
}
}

uint64_t trie_save(std::ostream&f,const Trie&t){auto b=f.tellp();paper_repro::Bytes p;paper_repro::string(p,t.prefix);audit::vec(f,p);audit::scalar(f,t.leaf_id);audit::scalar(f,t.root);if(t.left)trie_save(f,*t.left);if(t.right)trie_save(f,*t.right);return uint64_t(f.tellp()-b);}
int main(int argc,char**argv)try{
using namespace paper_repro;keccak=false;Config c=args(argc,argv);build_threads=1;auto base=vann::read_fvecs(c.dataset+"/sift_base.fvecs",c.base);auto data=data_hash(base);
std::cout<<"START buckets "<<c.base<<std::endl;auto t=Clock::now();LSH lsh(base.dimension,c.width,c.seed);std::vector<std::pair<uint64_t,int>>order;for(int id=0;id<c.base;++id)order.emplace_back(lsh.code(base.row(id)),id);std::sort(order.begin(),order.end());std::vector<Bucket>buckets;std::vector<Descriptor>descriptors;double ads_ms=0;
for(int begin=0;begin<c.base;begin+=c.bucket){int end=std::min(begin+c.bucket,c.base);std::vector<int>ids;for(int j=begin;j<end;++j)ids.push_back(order[j].second);std::string cache=c.cache+"/bucket-"+std::to_string(begin)+".index";require(!std::filesystem::exists(cache),"fresh cache required");Bucket b;b.graph=std::make_unique<Graph>(base,ids,c.M,c.efc,cache);auto at=Clock::now();std::vector<Hash>leaves;for(auto&r:b.graph->records)leaves.push_back(leaf(r));b.tree=std::make_unique<Tree>(std::move(leaves));ads_ms+=ms(at);descriptors.push_back({uint32_t(buckets.size()),uint32_t(ids.size()),b.graph->entry,b.graph->maxlevel,order[begin].first,order[end-1].first,b.tree->root()});buckets.push_back(std::move(b));std::cout<<"BUCKET "<<end<<std::endl;}
double ix=ms(t)-ads_ms;t=Clock::now();Trie trie(descriptors,0,descriptors.size());ads_ms+=ms(t);
std::ofstream f(c.output+"/serving.snapshot",std::ios::binary);for(auto&b:buckets)audit::graph(f,*b.graph);audit::scalar(f,lsh.dim);audit::scalar(f,lsh.width);for(auto&a:lsh.a)audit::vec(f,a);audit::vec(f,lsh.b);uint64_t sb=f.tellp();for(auto&b:buckets)audit::tree(f,*b.tree);for(auto&d:descriptors){Bytes x;write_descriptor(x,d);audit::vec(f,x);}trie_save(f,trie);uint64_t total=f.tellp();f.close();require(bool(f),"snapshot write failed");audit::result(c,ix,ads_ms,sb,total-sb);return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}

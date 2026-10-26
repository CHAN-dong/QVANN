"""Fresh single-thread offline index builds; no query proof generation."""
import argparse,csv,hashlib,json,os,resource,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
STUDY=ROOT/'experiments/work9-index-20261017'
OUT=ROOT/'results/work9-index-20261017'
DATA={'sift1m':('SIFT1M',1000000,128,8,100),'nytimes256':('NYTimes-256',289761,256,16,200),'glove100':('GloVe-100',1183514,100,16,200),'deep1m':('DEEP1M',1000000,96,16,200)}
def write(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n')
def sha(p):return hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
def env():
    e=os.environ.copy()
    for k in ['OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','RAYON_NUM_THREADS']:e[k]='1'
    f=ROOT/'third_party/LearnedEarlyTer-20SIGMOD'
    e['LD_LIBRARY_PATH']=':'.join(map(str,[ROOT/'third_party/runtime/lib',f,f/'lib']))
    return e
COMMON=r'''
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
'''
ANN=r'''
int main(int argc,char**argv)try{
using namespace paper_repro;keccak=true;Config c=args(argc,argv);build_threads=1;
auto base=vann::read_fvecs(c.dataset+"/sift_base.fvecs",c.base);auto data=data_hash(base);
std::vector<int>ids(c.base);std::iota(ids.begin(),ids.end(),0);auto cache=c.cache+"/global.index";require(!std::filesystem::exists(cache),"fresh cache required");
std::cout<<"START graph "<<c.base<<std::endl;auto t=Clock::now();Graph g(base,ids,c.M,c.efc,cache);double ix=ms(t);t=Clock::now();ADS a(g,c.shard,base.dimension);double ad=ms(t);
std::ofstream f(c.output+"/serving.snapshot",std::ios::binary);audit::graph(f,g);uint64_t sb=f.tellp();for(auto*v:{&a.nodes,&a.identifiers}){uint64_t n=v->size();audit::scalar(f,n);for(auto&tr:*v)audit::tree(f,tr);}audit::scalar(f,a.anchor.dim);audit::scalar(f,a.anchor.n);audit::scalar(f,a.anchor.entry);audit::scalar(f,a.anchor.maxlevel);audit::scalar(f,a.anchor.shard);audit::vec(f,a.anchor.nodes);audit::vec(f,a.anchor.identifiers);uint64_t total=f.tellp();f.close();require(bool(f),"snapshot write failed");audit::result(c,ix,ad,sb,total-sb);return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
'''
VLAH=r'''
uint64_t trie_save(std::ostream&f,const Trie&t){auto b=f.tellp();paper_repro::Bytes p;paper_repro::string(p,t.prefix);audit::vec(f,p);audit::scalar(f,t.leaf_id);audit::scalar(f,t.root);if(t.left)trie_save(f,*t.left);if(t.right)trie_save(f,*t.right);return uint64_t(f.tellp()-b);}
int main(int argc,char**argv)try{
using namespace paper_repro;keccak=false;Config c=args(argc,argv);build_threads=1;auto base=vann::read_fvecs(c.dataset+"/sift_base.fvecs",c.base);auto data=data_hash(base);
std::cout<<"START buckets "<<c.base<<std::endl;auto t=Clock::now();LSH lsh(base.dimension,c.width,c.seed);std::vector<std::pair<uint64_t,int>>order;for(int id=0;id<c.base;++id)order.emplace_back(lsh.code(base.row(id)),id);std::sort(order.begin(),order.end());std::vector<Bucket>buckets;std::vector<Descriptor>descriptors;double ads_ms=0;
for(int begin=0;begin<c.base;begin+=c.bucket){int end=std::min(begin+c.bucket,c.base);std::vector<int>ids;for(int j=begin;j<end;++j)ids.push_back(order[j].second);std::string cache=c.cache+"/bucket-"+std::to_string(begin)+".index";require(!std::filesystem::exists(cache),"fresh cache required");Bucket b;b.graph=std::make_unique<Graph>(base,ids,c.M,c.efc,cache);auto at=Clock::now();std::vector<Hash>leaves;for(auto&r:b.graph->records)leaves.push_back(leaf(r));b.tree=std::make_unique<Tree>(std::move(leaves));ads_ms+=ms(at);descriptors.push_back({uint32_t(buckets.size()),uint32_t(ids.size()),b.graph->entry,b.graph->maxlevel,order[begin].first,order[end-1].first,b.tree->root()});buckets.push_back(std::move(b));std::cout<<"BUCKET "<<end<<std::endl;}
double ix=ms(t)-ads_ms;t=Clock::now();Trie trie(descriptors,0,descriptors.size());ads_ms+=ms(t);
std::ofstream f(c.output+"/serving.snapshot",std::ios::binary);for(auto&b:buckets)audit::graph(f,*b.graph);audit::scalar(f,lsh.dim);audit::scalar(f,lsh.width);for(auto&a:lsh.a)audit::vec(f,a);audit::vec(f,lsh.b);uint64_t sb=f.tellp();for(auto&b:buckets)audit::tree(f,*b.tree);for(auto&d:descriptors){Bytes x;write_descriptor(x,d);audit::vec(f,x);}trie_save(f,trie);uint64_t total=f.tellp();f.close();require(bool(f),"snapshot write failed");audit::result(c,ix,ads_ms,sb,total-sb);return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
'''
OURS=r'''
int main(int argc,char**argv)try{
using namespace optimized;auto c=p::args(argc,argv);p::keccak=false;
auto base=vann::read_fvecs(c.dataset+"/sift_base.fvecs",c.base);auto data=p::data_hash(base);
auto cache=c.cache+"/global.index";p::require(!std::filesystem::exists(cache),"fresh cache required");omp_set_num_threads(1);std::cout<<"START graph "<<c.base<<std::endl;
auto t=Clock::now();auto h=vann::build_hnsw(base,c.M,c.efc,cache);double ix=p::ms(t);t=Clock::now();Codec codec(h);ADS a(h,codec,data);double ad=p::ms(t);
std::ofstream f(c.output+"/serving.snapshot",std::ios::binary);std::ifstream graph(cache,std::ios::binary);f<<graph.rdbuf();audit::vec(f,codec.lo);audit::vec(f,codec.span);audit::vec(f,codec.high);audit::vec(f,codec.low);uint64_t sb=f.tellp();for(auto*tr:{a.route.get(),a.delta.get(),a.raw.get()})audit::tree(f,*tr);audit::vec(f,a.route_base);audit::vec(f,a.position);audit::scalar(f,a.anchor.n);audit::scalar(f,a.anchor.entry);audit::scalar(f,a.anchor.maxlevel);audit::scalar(f,a.anchor.route_count);audit::scalar(f,a.anchor.dim);audit::scalar(f,a.anchor.snapshot);audit::scalar(f,a.anchor.route);audit::scalar(f,a.anchor.delta);audit::scalar(f,a.anchor.raw);audit::vec(f,a.anchor.entry_sq);uint64_t total=f.tellp();f.close();p::require(bool(f),"snapshot write failed");audit::result(c,ix,ad,sb,total-sb);return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
'''
def build():
    STUDY.mkdir(parents=True,exist_ok=True)
    f=ROOT/'third_party/LearnedEarlyTer-20SIGMOD';u=ROOT/'ANNProof-24FGCS/upstream/source/similarity_search'
    for name,src,main in [('ANNProof',ROOT/'ANNProof-24FGCS/annproof.cpp',ANN),('VLAH',ROOT/'VLAH-25TrustCom/vlah.cpp',VLAH),('Ours',ROOT/'build/veriann_lut_benchmark.cpp',OURS)]:
        target=STUDY/(name+'.cpp')
        if name=='Ours':
            # Embedded legacy experiment headers redefine/undefine main.
            # Keep the unchanged protocol definitions, omit only its CLI main.
            prefix=src.read_text().split('int main(int argc,char**argv)try{')[0]
        else:prefix='#define main original_main_unused\n#include "'+str(src)+'"\n#undef main\n'
        target.write_text(prefix+COMMON+main)
        cmd=['g++','-O3','-DNDEBUG','-std=c++17','-fopenmp','-I'+str(ROOT/'src'),'-I'+str(ROOT/'third_party'),'-I'+str(f),'-I'+str(f/'include'),str(target),str(u/'src/Keccak256.cc'),'-I'+str(u/'include'),'-o',str(STUDY/name),'-L'+str(f),'-L'+str(f/'lib'),'-lfaiss','-l_lightgbm','-lcrypto','-lpthread','-ldl']
        with (STUDY/(name+'-compile.log')).open('w') as log:subprocess.run(cmd,env=env(),stdout=log,stderr=subprocess.STDOUT,check=True)
        write(STUDY/(name+'-build.json'),dict(command=cmd,source_sha256=sha(src),harness_sha256=sha(target),binary_sha256=sha(STUDY/name)))
def run():
    for ds,(title,n,d,m,ef) in DATA.items():
        for method in ['ANNProof','Ours','VLAH']:
            out=OUT/ds/method
            if (out/'measurement.json').exists():continue
            if out.exists():raise RuntimeError('Inspect incomplete fresh run: '+str(out))
            out.mkdir(parents=True);cache=out/'cache'
            width=256
            if ds!='sift1m':
                study=ROOT/'experiments'/('work9-nytimes-20261017' if ds=='nytimes256' else 'work9-multidata-20261015')
                width=json.loads((study/(ds+'-lsh-width.json')).read_text())['width']
            cmd=[str(STUDY/method),'--dataset',str(ROOT/'dataset'/ds),'--base',str(n),'--M',str(m),'--ef-construction',str(ef),'--build-threads','1','--output',str(out),'--cache',str(cache),'--lsh-width',str(width)]
            print('RUN',ds,method,flush=True);start=time.time()
            def limit():resource.setrlimit(resource.RLIMIT_AS,(50*1024**3,50*1024**3))
            with (out/'run.log').open('w') as log:r=subprocess.run(cmd,cwd=ROOT,env=env(),stdout=log,stderr=subprocess.STDOUT,preexec_fn=limit)
            if r.returncode:
                write(out/'failure.json',dict(command=cmd,code=r.returncode,elapsed=time.time()-start));raise RuntimeError(str(out/'run.log'))
            record=json.loads((out/'offline.json').read_text());assert record['total_bytes']==(out/'serving.snapshot').stat().st_size
            record.update(dataset=ds,title=title,method=method,N=n,D=d,M=m,ef_construction=ef,width=width,command=cmd,status='measured',repeats=1,wall_seconds=time.time()-start,snapshot_sha256=sha(out/'serving.snapshot'),binary_sha256=sha(STUDY/method))
            write(out/'measurement.json',record);print('DONE',ds,method,record['construct_seconds'],record['total_bytes'],flush=True)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('action',choices=['build','run']);a=p.parse_args();globals()[a.action]()

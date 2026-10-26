// Controlled full-wire ablations. The unchanged optimized LUT protocol is
// included with its unused main renamed by the build script.
#include "../../build/multidata_lut_include.cpp"
#include "LearnedEarlyTer-20SIGMOD/Clustering.h"

namespace ablation {
namespace p=paper_repro;
namespace o=optimized;
using o::Id;using o::Bytes;using o::Hash;using o::Key;using o::Clock;
using Item=std::pair<float,Id>;
const char* names[]={"","Merkle+IVF","Merkle+HNSW","Merkle+IVF+(i)","Merkle+HNSW+(i)","(i)+(ii)","(i)+(ii)+(iii)"};
bool is_ivf(int v){return v==1||v==3;}
bool is_quant(int v){return v>=3;}
Bytes floats(const float*v,int d){Bytes b;for(int j=0;j<d;++j)p::f32(b,v[j]);return b;}
Hash object_leaf(Id id,const Bytes&code,const std::vector<std::vector<Id>>&links,bool graph,bool quant){
    Bytes b=o::domain_bytes(quant?"ablation/node-sq4/v1":"ablation/node-raw/v1");p::u32(b,id);p::u32(b,graph);
    b.insert(b.end(),code.begin(),code.end());
    if(graph){p::u32(b,links.size());for(auto&layer:links){p::u32(b,layer.size());for(Id x:layer)p::u32(b,x);}}
    return p::hash(b);
}
Hash list_leaf(Id id,const std::vector<Id>&ids){Bytes b=o::domain_bytes("ablation/ivf-list/v1");p::u32(b,id);p::u32(b,ids.size());for(Id x:ids)p::u32(b,x);return p::hash(b);}
Hash center_leaf(const std::vector<float>&cs,int dim,int count){Bytes b=o::domain_bytes("ablation/ivf-centers/v1");p::u32(b,dim);p::u32(b,count);for(float x:cs)p::f32(b,x);return p::hash(b);}

struct IVF {
    int dim=0,K=0;std::vector<float>centers;std::vector<std::vector<Id>>lists;
    IVF(const vann::Matrix&base,int count,const std::string&cache):dim(base.dimension),K(count),lists(count){
        std::vector<Id>assignment(base.rows());
        if(std::filesystem::exists(cache)){
            std::ifstream f(cache,std::ios::binary);uint32_t header[3];f.read(reinterpret_cast<char*>(header),12);
            p::require(header[0]==base.rows()&&header[1]==uint32_t(dim)&&header[2]==uint32_t(K),"IVF cache shape mismatch");
            centers.resize(size_t(K)*dim);f.read(reinterpret_cast<char*>(centers.data()),centers.size()*4);
            f.read(reinterpret_cast<char*>(assignment.data()),assignment.size()*4);p::require(bool(f)&&f.peek()==EOF,"IVF cache truncated/extra bytes");
        }else{
            const size_t nt=std::min<size_t>(65536,base.rows());std::vector<float>train;train.reserve(nt*dim);
            for(size_t i=0;i<nt;++i){size_t id=i*base.rows()/nt;train.insert(train.end(),base.row(id),base.row(id)+dim);}
            faiss::ClusteringParameters cp;cp.niter=12;cp.nredo=1;cp.seed=42;cp.verbose=false;cp.max_points_per_centroid=256;
            omp_set_num_threads(p::build_threads);faiss::IndexFlatL2 index(dim);faiss::Clustering cl(dim,K,cp);
            cl.train(nt,train.data(),index);centers=cl.centroids;
            std::cout<<"[IVF] centers trained K="<<K<<" ntrain="<<nt<<std::endl;
            faiss::IndexFlatL2 assign(dim);assign.add(K,centers.data());
            std::vector<float>dist(8192);std::vector<faiss::Index::idx_t>ids(8192);
            for(size_t begin=0;begin<base.rows();begin+=8192){size_t n=std::min<size_t>(8192,base.rows()-begin);
                assign.search(n,base.row(begin),1,dist.data(),ids.data());for(size_t j=0;j<n;++j)assignment[begin+j]=Id(ids[j]);}
            omp_set_num_threads(1);
            std::ofstream f(cache+".part",std::ios::binary);uint32_t header[]={uint32_t(base.rows()),uint32_t(dim),uint32_t(K)};
            f.write(reinterpret_cast<char*>(header),12);f.write(reinterpret_cast<char*>(centers.data()),centers.size()*4);
            f.write(reinterpret_cast<char*>(assignment.data()),assignment.size()*4);p::require(bool(f),"IVF cache write failed");f.close();std::filesystem::rename(cache+".part",cache);
        }
        for(Id id=0;id<assignment.size();++id){p::require(assignment[id]<uint32_t(K),"invalid IVF assignment");lists[assignment[id]].push_back(id);}
        std::cout<<"[IVF] ready N="<<base.rows()<<" K="<<K<<std::endl;
    }
};
std::vector<Id> choose_lists(const std::vector<float>&cs,int dim,const float*q,int probes){
    std::vector<Item>rank;for(Id i=0;i<cs.size()/dim;++i)rank.emplace_back(vann::squared_l2(q,cs.data()+size_t(i)*dim,dim),i);
    p::require(probes>0&&size_t(probes)<=rank.size(),"invalid IVF probes");std::partial_sort(rank.begin(),rank.begin()+probes,rank.end());
    std::vector<Id>out;for(int j=0;j<probes;++j)out.push_back(rank[j].second);std::sort(out.begin(),out.end());return out;
}
struct Client {Id n,entry,maxlevel;int dim,variant,nlist;Hash node,delta,raw,lists,centers;};
struct Store {
    const vann::HnswIndex&h;const o::Codec&codec;bool graph,quant;
    std::vector<std::vector<std::vector<Id>>>links;std::vector<Id>position;
    std::unique_ptr<p::Tree>nodes,delta,raw,lists;Client client{};
    Store(const vann::HnswIndex&h_,const o::Codec&c,bool graph_,bool quant_,int variant,const IVF*ivf=nullptr):h(h_),codec(c),graph(graph_),quant(quant_){
        client.n=h.size();client.dim=h.dimension();client.entry=h.index->hnsw.entry_point;client.maxlevel=h.index->hnsw.max_level;client.variant=variant;
        position.resize(h.size());std::vector<int>order;
        if(graph){int components=0;order=deterministic_bfs_order(h,components);links.resize(h.size());
            for(Id id=0;id<h.size();++id)for(int level=0;level<h.index->hnsw.levels[id];++level)links[id].push_back(o::neighbors(h,id,level));
        }else{order.resize(h.size());std::iota(order.begin(),order.end(),0);}
        for(Id pos=0;pos<h.size();++pos)position[order[pos]]=pos;
        std::vector<Hash>leaves(h.size()),dl,rl;if(quant){dl.resize(h.size());rl.resize(h.size());}
        for(Id id=0;id<h.size();++id){Id pos=position[id];Bytes code=quant?Bytes(codec.sq(id),codec.sq(id)+client.dim/2):floats(h.vector(id),client.dim);
            leaves[pos]=object_leaf(id,code,graph?links[id]:std::vector<std::vector<Id>>{},graph,quant);
            if(quant){dl[pos]=o::refinement_leaf("record/delta46/v1",id,codec.delta(id),client.dim/4);rl[pos]=o::refinement_leaf("record/raw/v1",id,reinterpret_cast<const uint8_t*>(h.vector(id)),client.dim*4);}}
        nodes=std::make_unique<p::Tree>(std::move(leaves));client.node=nodes->root();
        if(quant){delta=std::make_unique<p::Tree>(std::move(dl));raw=std::make_unique<p::Tree>(std::move(rl));client.delta=delta->root();client.raw=raw->root();}
        if(ivf){std::vector<Hash>ls;for(Id i=0;i<uint32_t(ivf->K);++i)ls.push_back(list_leaf(i,ivf->lists[i]));lists=std::make_unique<p::Tree>(std::move(ls));client.lists=lists->root();client.nlist=ivf->K;client.centers=center_leaf(ivf->centers,client.dim,ivf->K);}
        std::cout<<"[ADS] vertex store ready variant="<<variant<<std::endl;
    }
};
struct Result {std::vector<Id>answer;std::set<Id>touched,delta,raw;std::set<Key>opened;size_t expanded=0;float tau=0;};
template<class Raw,class Code,class Delta>
struct Oracle {
    const o::Codec&codec;const float*q;bool quant;Raw raw;Code code;Delta delta;Result&result;
    Oracle(const o::Codec&c,const float*q_,bool quant_,Raw r,Code s,Delta d,Result&res):codec(c),q(q_),quant(quant_),raw(r),code(s),delta(d),result(res){if(quant)codec.prepare(q);}
    float operator()(Id id,float threshold=INFINITY){
        result.touched.insert(id);
        if(quant){auto s=code(id);if(codec.lower(q,s)>threshold)return INFINITY;result.delta.insert(id);auto d=delta(id);if(codec.lower(q,s,d)>threshold)return INFINITY;}
        if(quant)result.raw.insert(id);return vann::squared_l2(q,raw(id),codec.dim);
    }
};
template<class Distance,class Adj>
void graph_search(Result&s,Id entry,Id maxlevel,int k,int ef,Distance&distance,Adj adjacent){
    auto open=[&](Id id,Id level){s.touched.insert(id);s.opened.insert({id,level});++s.expanded;return adjacent(id,level);};
    Id current=entry;float current_d=distance(current);
    for(Id layer=maxlevel;layer>0;--layer){bool changed=true;while(changed){changed=false;Id previous=current;for(Id id:open(previous,layer)){float d=distance(id,current_d);if(Item{d,id}<Item{current_d,current}){current=id;current_d=d;changed=true;}}}}
    std::priority_queue<Item,std::vector<Item>,std::greater<Item>>candidates;std::priority_queue<Item>best;std::set<Id>seen;
    Item first{distance(current),current};candidates.push(first);best.push(first);seen.insert(current);ef=std::max(ef,k);
    while(!candidates.empty()){auto c=candidates.top();candidates.pop();if(best.size()>=size_t(ef)&&c>best.top())break;
        for(Id id:open(c.second,0)){if(!seen.insert(id).second)continue;float bound=best.size()<size_t(ef)?INFINITY:best.top().first;Item item{distance(id,bound),id};
            if(best.size()<size_t(ef)||item<best.top()){candidates.push(item);best.push(item);if(best.size()>size_t(ef))best.pop();}}}
    std::vector<Item>v;while(!best.empty()){v.push_back(best.top());best.pop();}std::sort(v.begin(),v.end());p::require(v.size()>=size_t(k),"not enough HNSW results");
    for(int j=0;j<k;++j)s.answer.push_back(v[j].second);s.tau=v[k-1].first;
}
template<class Distance>
void scan_lists(Result&s,const std::vector<Id>&candidates,int k,Distance&distance){
    std::priority_queue<Item>best;
    for(Id id:candidates){float threshold=best.size()<size_t(k)?INFINITY:best.top().first;Item item{distance(id,threshold),id};
        if(best.size()<size_t(k)||item<best.top()){best.push(item);if(best.size()>size_t(k))best.pop();}}
    std::vector<Item>v;while(!best.empty()){v.push_back(best.top());best.pop();}std::sort(v.begin(),v.end());p::require(v.size()==size_t(k),"not enough IVF results");
    for(auto item:v)s.answer.push_back(item.second);s.tau=v.back().first;s.expanded=candidates.size();
}
Result search(const vann::HnswIndex&h,const o::Codec&codec,int variant,const IVF*ivf,const float*q,int k,int parameter){
    Result result;
    if(variant>=5){auto t=o::walk(codec,q,parameter,h.index->hnsw.entry_point,h.index->hnsw.max_level,[&](Id id){return codec.sq(id);},[&](Id id,Id l){return o::neighbors(h,id,l);});
        result.touched={t.disclosed.begin(),t.disclosed.end()};result.opened=t.opened;result.expanded=t.expanded.size();
        std::vector<Item>v;for(Id id:t.disclosed)v.emplace_back(vann::squared_l2(q,h.vector(id),h.dimension()),id);std::partial_sort(v.begin(),v.begin()+k,v.end());
        for(int j=0;j<k;++j)result.answer.push_back(v[j].second);result.tau=v[k-1].first;
    }else{
        Oracle oracle(codec,q,is_quant(variant),[&](Id id){return h.vector(id);},[&](Id id){return codec.sq(id);},[&](Id id){return codec.delta(id);},result);
        if(is_ivf(variant)){auto selected=choose_lists(ivf->centers,h.dimension(),q,parameter);std::vector<Id>candidates;for(Id id:selected)candidates.insert(candidates.end(),ivf->lists[id].begin(),ivf->lists[id].end());scan_lists(result,candidates,k,oracle);}
        else graph_search(result,h.index->hnsw.entry_point,h.index->hnsw.max_level,k,parameter,oracle,[&](Id id,Id l){return o::neighbors(h,id,l);});
    }return result;
}
struct Packet {Bytes wire;size_t kind_offset=0,count_offset=0,first_begin=0,first_end=0,payload_offset=0,adj_offset=0,delta_offset=0,records=0,proofs=0;};
void refinements(Result&s,const o::Codec&codec,const float*q){
    codec.prepare(q);
    for(Id id:s.touched)if(codec.lower(q,codec.sq(id))<=s.tau){s.delta.insert(id);if(codec.lower(q,codec.sq(id),codec.delta(id))<=s.tau||std::find(s.answer.begin(),s.answer.end(),id)!=s.answer.end())s.raw.insert(id);}
}
Packet generate(const Store&store,const IVF*ivf,const Result&s,const float*q,int k,int parameter){
    Packet out;auto&b=out.wire;auto&h=store.h;auto&codec=store.codec;int variant=store.client.variant,dim=store.client.dim;
    p::write_header(b,q,dim,k,parameter,s.answer);out.kind_offset=b.size();p::u32(b,variant);
    std::set<Id>list_targets;
    if(ivf){for(float x:ivf->centers)p::f32(b,x);auto selected=choose_lists(ivf->centers,dim,q,parameter);p::u32(b,selected.size());
        for(Id id:selected){list_targets.insert(id);p::u32(b,id);p::u32(b,ivf->lists[id].size());for(Id x:ivf->lists[id])p::u32(b,x);}}
    out.count_offset=b.size();p::u32(b,s.touched.size());std::set<Id>nodes,delta,raw;
    for(Id id:s.touched){size_t begin=b.size();p::u32(b,id);p::u32(b,store.position[id]);nodes.insert(store.position[id]);if(!out.payload_offset)out.payload_offset=b.size();
        if(store.quant){auto p=codec.sq(id);b.insert(b.end(),p,p+dim/2);}else for(int j=0;j<dim;++j)p::f32(b,h.vector(id)[j]);
        if(store.graph){p::u32(b,store.links[id].size());for(auto&layer:store.links[id]){p::u32(b,layer.size());for(Id x:layer){if(!out.adj_offset)out.adj_offset=b.size();p::u32(b,x);}}}
        if(!out.first_end){out.first_begin=begin;out.first_end=b.size();}}
    if(store.quant){p::u32(b,s.delta.size());for(Id id:s.delta){p::u32(b,id);p::u32(b,store.position[id]);delta.insert(store.position[id]);if(!out.delta_offset)out.delta_offset=b.size();auto v=codec.delta(id);b.insert(b.end(),v,v+dim/4);}
        p::u32(b,s.raw.size());for(Id id:s.raw){p::u32(b,id);p::u32(b,store.position[id]);raw.insert(store.position[id]);for(int j=0;j<dim;++j)p::f32(b,h.vector(id)[j]);}}
    out.records=b.size();o::proof_write(b,*store.nodes,nodes);
    if(store.quant){o::proof_write(b,*store.delta,delta);o::proof_write(b,*store.raw,raw);}
    if(ivf)o::proof_write(b,*store.lists,list_targets);out.proofs=b.size()-out.records;return out;
}
bool verify(const Bytes&wire,const Client&a,const o::Codec&codec,const float*q,int k,int parameter,double*auth=nullptr,double*replay=nullptr){try{
    auto started=Clock::now();p::Reader r{wire};auto answer=p::read_header(r,q,a.dim,k,parameter);p::require(r.u32()==uint32_t(a.variant),"variant mismatch");
    bool graph=!is_ivf(a.variant),quant=is_quant(a.variant);
    std::vector<float>centers;std::map<Id,std::vector<Id>>lists;std::map<Id,Hash>list_leaves;
    if(!graph){for(size_t j=0;j<size_t(a.nlist)*a.dim;++j)centers.push_back(r.f32());p::require(center_leaf(centers,a.dim,a.nlist)==a.centers,"centroid authentication failed");
        Id count=r.count(8);p::require(count==uint32_t(parameter),"list count mismatch");for(Id i=0;i<count;++i){Id id=r.u32();p::require(id<uint32_t(a.nlist)&&!lists.count(id),"invalid list ID");auto&v=lists[id];Id n=r.count(4);for(Id j=0;j<n;++j){Id x=r.u32();p::require(x<a.n,"bad member ID");v.push_back(x);}p::require(std::is_sorted(v.begin(),v.end())&&std::adjacent_find(v.begin(),v.end())==v.end(),"noncanonical members");list_leaves[id]=list_leaf(id,v);}}
    std::map<Id,Bytes>codes,deltas;std::map<Id,std::vector<float>>raw;std::map<Id,std::vector<std::vector<Id>>>links;
    std::map<Id,Hash>node_leaves,delta_leaves,raw_leaves;std::map<Id,Id>positions;
    auto id_read=[&](){Id id=r.u32();p::require(id<a.n,"invalid ID");return id;};
    auto blob=[&](size_t n){r.need(n);Bytes b(wire.begin()+r.p,wire.begin()+r.p+n);r.p+=n;return b;};
    Id count=r.count(8);for(Id i=0;i<count;++i){Id id=id_read(),pos=r.u32();p::require(pos<a.n&&positions.emplace(id,pos).second,"duplicate object");Bytes code;
        if(quant){code=blob(a.dim/2);codes[id]=code;}else{auto&v=raw[id];for(int j=0;j<a.dim;++j)v.push_back(r.f32());code=floats(v.data(),a.dim);}
        std::vector<std::vector<Id>>ls;
        if(graph){Id levels=r.count(4);p::require(levels>0&&levels<=a.maxlevel+1,"bad levels");for(Id l=0;l<levels;++l){std::vector<Id>v;Id degree=r.count(4);for(Id j=0;j<degree;++j)v.push_back(id_read());p::require(std::is_sorted(v.begin(),v.end())&&std::adjacent_find(v.begin(),v.end())==v.end(),"invalid adjacency");ls.push_back(v);}links[id]=ls;}
        p::require(node_leaves.emplace(pos,object_leaf(id,code,ls,graph,quant)).second,"duplicate node position");}
    if(quant){count=r.count(8+a.dim/4);for(Id i=0;i<count;++i){Id id=id_read(),pos=r.u32();p::require(positions.at(id)==pos&&!deltas.count(id),"invalid delta ID/position");Bytes b=blob(a.dim/4);deltas[id]=b;p::require(delta_leaves.emplace(pos,o::refinement_leaf("record/delta46/v1",id,b.data(),b.size())).second,"duplicate delta position");}
        count=r.count(8+a.dim*4);for(Id i=0;i<count;++i){Id id=id_read(),pos=r.u32();p::require(positions.at(id)==pos&&deltas.count(id)&&!raw.count(id),"invalid raw ID/position");auto&v=raw[id];for(int j=0;j<a.dim;++j)v.push_back(r.f32());auto b=floats(v.data(),a.dim);p::require(raw_leaves.emplace(pos,o::refinement_leaf("record/raw/v1",id,b.data(),b.size())).second,"duplicate raw position");}}
    p::require(o::check_proof(r,a.node,a.n,node_leaves),"vertex authentication failed");
    if(quant){p::require(o::check_proof(r,a.delta,a.n,delta_leaves),"delta authentication failed");p::require(o::check_proof(r,a.raw,a.n,raw_leaves),"raw authentication failed");}
    if(!graph)p::require(o::check_proof(r,a.lists,a.nlist,list_leaves),"list authentication failed");r.end();
    if(auth)*auth=p::ms(started);started=Clock::now();Result result;
    auto getraw=[&](Id id){return raw.at(id).data();};auto getcode=[&](Id id){return codes.at(id).data();};auto getdelta=[&](Id id){return deltas.at(id).data();};
    if(a.variant==5){auto trace=o::walk(codec,q,parameter,a.entry,a.maxlevel,getcode,[&](Id id,Id l){return links.at(id).at(l);});result.touched={trace.disclosed.begin(),trace.disclosed.end()};
        std::vector<Item>best;for(auto&[id,v]:raw)best.emplace_back(vann::squared_l2(q,v.data(),a.dim),id);std::sort(best.begin(),best.end());p::require(best.size()>=size_t(k),"insufficient exact candidates");
        for(int j=0;j<k;++j)result.answer.push_back(best[j].second);result.tau=best[k-1].first;
        for(Id id:trace.disclosed)if(codec.lower(q,getcode(id))<=result.tau){result.delta.insert(id);auto d=getdelta(id);if(codec.lower(q,getcode(id),d)<=result.tau||std::find(answer.begin(),answer.end(),id)!=answer.end()){result.raw.insert(id);(void)getraw(id);}}
    }else{
        Oracle oracle(codec,q,quant,getraw,getcode,getdelta,result);
        if(graph)graph_search(result,a.entry,a.maxlevel,k,parameter,oracle,[&](Id id,Id l){return links.at(id).at(l);});
        else{auto selected=choose_lists(centers,a.dim,q,parameter);std::vector<Id>got,candidates;std::set<Id>unique;for(auto&[id,v]:lists){got.push_back(id);for(Id x:v){p::require(unique.insert(x).second,"duplicate IVF member");candidates.push_back(x);}}p::require(got==selected,"wrong IVF routing");scan_lists(result,candidates,k,oracle);}}
    p::require(result.answer==answer,"top-k/replay mismatch");p::require(result.touched.size()==positions.size(),"extra or missing nodes");
    if(quant)p::require(result.delta.size()==deltas.size()&&result.raw.size()==raw.size(),"noncanonical refinement set");
    if(replay)*replay=p::ms(started);return true;
}catch(const std::exception&){return false;}}

void attacks(const Packet&packet,const Client&client,const o::Codec&codec,const float*q,int k,int parameter,const std::string&out){
    std::ofstream f(out+"/tamper_tests.csv");f<<"attack,rejected\n";
    auto test=[&](const std::string&name,const Bytes&b,const Client&c,const float*query,int param){bool rejected=!verify(b,c,codec,query,k,param);f<<name<<','<<rejected<<'\n';p::require(rejected,"tamper accepted: "+name);};
    for(auto[name,pos]:std::vector<std::pair<std::string,size_t>>{{"query_binding",4},{"answer",48},{"variant",packet.kind_offset},{"vector_or_SQ4",packet.payload_offset},{"proof",packet.wire.size()-1}}){auto b=packet.wire;b.at(pos)^=1;test(name,b,client,q,parameter);}
    if(packet.adj_offset){auto b=packet.wire;b.at(packet.adj_offset)^=1;test("adjacency",b,client,q,parameter);}
    if(packet.delta_offset){auto b=packet.wire;b.at(packet.delta_offset)^=1;test("delta",b,client,q,parameter);}
    auto b=packet.wire;b.pop_back();test("truncated",b,client,q,parameter);b=packet.wire;b.push_back(0);test("appended",b,client,q,parameter);
    b=packet.wire;p::Reader read{packet.wire};read.p=packet.count_offset;Id count=read.u32();b.erase(b.begin()+packet.first_begin,b.begin()+packet.first_end);for(int j=0;j<4;++j)b[packet.count_offset+j]=uint8_t((count-1)>>(8*j));test("omitted_object",b,client,q,parameter);
    auto c=client;c.node[0]^=1;test("wrong_root",packet.wire,c,q,parameter);test("wrong_parameter",packet.wire,client,q,parameter+1);
    std::vector<float>other(q,q+codec.dim);other[0]+=1;test("wrong_query",packet.wire,client,other.data(),parameter);
    if(is_ivf(client.variant)){c=client;c.lists[0]^=1;test("wrong_membership_root",packet.wire,c,q,parameter);c=client;c.centers[0]^=1;test("wrong_centroid_root",packet.wire,c,q,parameter);}
}
} // namespace ablation

int main(int argc,char**argv)try{
    namespace a=ablation;namespace p=paper_repro;namespace o=optimized;using o::Id;
    std::vector<char*>args{argv[0]};std::string variants="6";bool pilot=false;int nlist=256;
    for(int i=1;i<argc;++i){std::string key=argv[i];p::require(i+1<argc,"missing option");if(key=="--variants")variants=argv[++i];else if(key=="--pilot")pilot=std::stoi(argv[++i])!=0;else if(key=="--nlist")nlist=std::stoi(argv[++i]);else{args.push_back(argv[i]);args.push_back(argv[++i]);}}
    auto c=p::args(args.size(),args.data());auto vs=p::ints(variants);for(int v:vs)p::require(v>=1&&v<=6,"invalid variant");
    p::keccak=false;p::build_threads=c.buildthreads;p::require(c.ks==std::vector<int>{10},"ablation fixes k=10");
    auto base=vann::read_fvecs(c.dataset+"/sift_base.fvecs",c.base),queries=p::read_queries(c);p::require(base.rows()==size_t(c.base),"short base");
    auto data=p::data_hash(base);auto started=p::Clock::now();auto gt=p::ground_truth(base,queries,10);double gt_ms=p::ms(started);
    std::cout<<"[GT] ready N="<<c.base<<" d="<<base.dimension<<std::endl;
    auto cache=c.cache+"/"+p::hex(data).substr(0,16)+"_M"+std::to_string(c.M)+"_efc"+std::to_string(c.efc)+".index";
    started=p::Clock::now();omp_set_num_threads(c.buildthreads);auto h=vann::build_hnsw(base,c.M,c.efc,cache);omp_set_num_threads(1);double index_ms=p::ms(started);
    bool need_ivf=std::any_of(vs.begin(),vs.end(),[](int v){return a::is_ivf(v);});std::unique_ptr<a::IVF>ivf;
    if(need_ivf)ivf=std::make_unique<a::IVF>(base,nlist,c.cache+"/"+p::hex(data).substr(0,16)+"_ivf"+std::to_string(nlist)+"_train65536_iter12_seed42_v1.bin");
    base.values.clear();base.values.shrink_to_fit();o::Codec codec(h);o::Codec client_codec(codec,true);
    std::cout<<"[index/codec] ready"<<std::endl;
    if(pilot){std::ofstream f(c.output+"/pilot.csv");f<<"variant,query,parameter,recall_at_k,visited,expanded,answer\n"<<std::setprecision(12);
        for(int parameter:c.efs)for(int qi=0;qi<c.queries;++qi){std::vector<Id>reference;
            for(int v:vs){auto s=a::search(h,codec,v,ivf.get(),queries.row(qi),10,parameter);int hits=0;for(Id id:s.answer)hits+=std::find(gt[qi].begin(),gt[qi].end(),id)!=gt[qi].end();
                if(reference.empty())reference=s.answer;else p::require(reference==s.answer,"paired variants changed query answers");
                f<<v<<','<<qi+c.queryoffset<<','<<parameter<<','<<double(hits)/10<<','<<s.touched.size()<<','<<s.expanded<<",\"";for(Id id:s.answer)f<<id<<' ';f<<"\"\n";}
            if(qi+1==c.queries){f.flush();std::cout<<"[pilot] parameter="<<parameter<<std::endl;}}
        std::ofstream(c.output+"/PILOT_PASS.txt")<<"PASS paired answers\n";return 0;}
    std::unique_ptr<o::ADS>full;
    for(int v:vs){auto out=c.output+"/variant-"+std::to_string(v);std::filesystem::create_directories(out);auto cfg=c;cfg.output=out;
        std::unique_ptr<a::Store>store;started=p::Clock::now();
        if(v==6){if(!full)full=std::make_unique<o::ADS>(h,codec,data);}else store=std::make_unique<a::Store>(h,codec,!a::is_ivf(v),a::is_quant(v),v,a::is_ivf(v)?ivf.get():nullptr);
        double ads_ms=p::ms(started);std::vector<p::Row>rows;bool tested=false;
        std::ofstream answers(out+"/answers.csv");answers<<"query,repeat,parameter,answer\n";
        std::ofstream roots(out+"/trusted_roots.txt");if(v==6)roots<<p::hex(full->anchor.route)<<'\n'<<p::hex(full->anchor.delta)<<'\n'<<p::hex(full->anchor.raw)<<'\n';else roots<<p::hex(store->client.node)<<'\n'<<p::hex(store->client.delta)<<'\n'<<p::hex(store->client.raw)<<'\n'<<p::hex(store->client.lists)<<'\n'<<p::hex(store->client.centers)<<'\n';
        for(int parameter:c.efs)for(int repeat=0;repeat<c.repeats;++repeat)for(int qi=0;qi<c.queries;++qi){const float*q=queries.row(qi);p::Row row{};row.query=qi+c.queryoffset;row.repeat=repeat;row.k=10;row.ef=parameter;row.probes=a::is_ivf(v)?parameter:1;
            started=p::Clock::now();auto result=a::search(h,codec,v,ivf.get(),q,10,parameter);row.search_ms=p::ms(started);row.visited=result.touched.size();row.expanded=result.expanded;row.buckets=a::is_ivf(v)?parameter:1;
            if(v==6){o::Walk trace;trace.disclosed={result.touched.begin(),result.touched.end()};trace.opened=result.opened; // generate uses disclosed/opened; expanded is only needed for search counters.
                started=p::Clock::now();auto packet=o::generate(h,codec,*full,trace,result.answer,result.tau,q,10,parameter);row.vo_ms=p::ms(started);
                started=p::Clock::now();row.valid=o::verify(packet.wire,full->anchor,client_codec,q,10,parameter,&row.auth_ms,&row.replay_ms);row.verify_ms=p::ms(started);row.vo_bytes=packet.wire.size();row.record_bytes=packet.records;row.local_proof_bytes=packet.proofs;
                if(!tested){p::save(out+"/query0.vo",packet.wire);o::attacks(packet,full->anchor,client_codec,q,10,parameter,out);tested=true;}}
            else{started=p::Clock::now();if(v==5)a::refinements(result,codec,q);auto packet=a::generate(*store,a::is_ivf(v)?ivf.get():nullptr,result,q,10,parameter);row.vo_ms=p::ms(started);
                started=p::Clock::now();row.valid=a::verify(packet.wire,store->client,client_codec,q,10,parameter,&row.auth_ms,&row.replay_ms);row.verify_ms=p::ms(started);row.vo_bytes=packet.wire.size();row.record_bytes=packet.records;row.local_proof_bytes=packet.proofs;
                if(!tested){p::save(out+"/query0.vo",packet.wire);a::attacks(packet,store->client,client_codec,q,10,parameter,out);tested=true;}}
            p::require(row.valid,"honest ablation VO rejected, variant="+std::to_string(v)+" query="+std::to_string(qi));
            answers<<row.query<<','<<repeat<<','<<parameter<<",\"";for(Id id:result.answer)answers<<id<<' ';answers<<"\"\n";
            std::set<Id>truth(gt[qi].begin(),gt[qi].end());int hits=0;for(Id id:result.answer)hits+=truth.count(id);row.recall=double(hits)/10;row.recall1=result.answer[0]==gt[qi][0];rows.push_back(row);
            if(qi+1==c.queries)std::cout<<"[query] variant="<<v<<" parameter="<<parameter<<" repeat="<<repeat<<" complete"<<std::endl;}
        p::output(cfg,rows,index_ms,ads_ms,gt_ms,data,a::names[v],0);std::ofstream(out+"/PASS.txt")<<"PASS honest verification and tamper checks\n";
    }
    std::ofstream(c.output+"/PASS.txt")<<"PASS all variants\n";return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<std::endl;return 1;}

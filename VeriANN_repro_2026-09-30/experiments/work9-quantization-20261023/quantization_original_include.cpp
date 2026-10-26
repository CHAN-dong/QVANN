// Full wire protocol for the final compatible VeriANN optimization path.
#define REFINEMENT_ORDERING_NO_MAIN
#include "/mnt/d/paper_source/work_9/VeriANN_repro_2026-09-30/VeriANN_repro_2026-09-30/src/experiments/refinement_merkle_ordering.cpp"
#include "/mnt/d/paper_source/work_9/VeriANN_repro_2026-09-30/VeriANN_repro_2026-09-30/src/paper_repro_common.hpp"

namespace optimized {
namespace p=paper_repro;
using p::Bytes; using p::Hash; using p::Clock;
using Id=uint32_t;
using Key=std::pair<Id,Id>;

struct CodecScalar {
    int dim; std::vector<float> lo,span; Bytes high,low;
    CodecScalar(const CodecScalar& other,bool):dim(other.dim),lo(other.lo),span(other.span){}
    explicit CodecScalar(const vann::HnswIndex& h):dim(h.dimension()),lo(dim,INFINITY),span(dim),
        high(h.size()*size_t(dim/2)),low(h.size()*size_t(dim/4)) {
        p::require(dim%4==0,"dimension must be divisible by four");
        std::vector<float> hi(dim,-INFINITY);
        for(size_t id=0;id<h.size();++id) for(int j=0;j<dim;++j){lo[j]=std::min(lo[j],h.vector(id)[j]);hi[j]=std::max(hi[j],h.vector(id)[j]);}
        for(int j=0;j<dim;++j)span[j]=hi[j]-lo[j];
        for(size_t id=0;id<h.size();++id)for(int j=0;j<dim;++j){
            double u=span[j]>0?(h.vector(id)[j]-lo[j])/span[j]:0;
            int code=std::clamp(int(std::floor(u*64)),0,63);
            high[id*(dim/2)+j/2]|=uint8_t((code>>2)<<((j%2)*4));
            low[id*(dim/4)+j/4]|=uint8_t((code&3)<<((j%4)*2));
        }
    }
    const uint8_t* sq(Id id)const{return high.data()+size_t(id)*(dim/2);}
    const uint8_t* delta(Id id)const{return low.data()+size_t(id)*(dim/4);}
    float score(const float*q,const uint8_t* s)const{
        float total=0;for(int j=0;j<dim;++j){int code=(s[j/2]>>((j%2)*4))&15;
            float x=lo[j]+span[j]*((code+.5F)/16),z=q[j]-x;total+=z*z;}return total;
    }
    float lower(const float*q,const uint8_t*s,const uint8_t*d=nullptr)const{
        int bins=d?64:16;float total=0;
        for(int j=0;j<dim;++j){int code=(s[j/2]>>((j%2)*4))&15;
            if(d)code=(code<<2)|((d[j/4]>>((j%4)*2))&3);
            float a=std::nextafter(lo[j]+span[j]*(float(code)/bins),-INFINITY);
            float b=std::nextafter(lo[j]+span[j]*(float(code+1)/bins),INFINITY);
            float z=q[j]<a?a-q[j]:(q[j]>b?q[j]-b:0);total+=z*z;
        }return std::nextafter(total,0.F);
    }
};
#include "/mnt/d/paper_source/work_9/VeriANN_repro_2026-09-30/VeriANN_repro_2026-09-30/src/veriann_query_lut.hpp"
struct Walk {std::vector<Id> disclosed;std::set<Id> expanded;std::set<Key> opened;};

// Sparse state removes the old O(N) zeroing/scanning from every query.
template<class Codes,class Neighbors>
Walk walk(const Codec&codec,const float*q,int budget,Id first,Id maxlevel,Codes code,Neighbors adjacent){
    codec.prepare(q);
    Walk t;std::map<Id,float> scores;
    auto discover=[&](Id id){if(!scores.count(id))scores[id]=codec.score(q,code(id));};
    auto open=[&](Id id,Id level){
        Key key{id,level};if(t.opened.count(key))return std::vector<Id>{};
        if(!t.expanded.count(id)){if(int(t.expanded.size())>=budget)return std::vector<Id>{};t.expanded.insert(id);}
        t.opened.insert(key);return adjacent(id,level);
    };
    Id entry=first;discover(entry);bool stopped=false;
    for(Id level=maxlevel;level>0&&!stopped;--level)while(true){
        auto ns=open(entry,level);if(ns.empty()){stopped=int(t.expanded.size())>=budget;break;}
        for(Id id:ns)discover(id);ns.push_back(entry);
        Id next=*std::min_element(ns.begin(),ns.end(),[&](Id a,Id b){return std::make_pair(scores.at(a),a)<std::make_pair(scores.at(b),b);});
        if(next==entry)break;entry=next;
    }
    std::set<std::pair<float,Id>> frontier;
    for(auto[id,s]:scores)frontier.insert({s,id});
    while(int(t.expanded.size())<budget&&!frontier.empty()){
        Id id=frontier.begin()->second;frontier.erase(frontier.begin());
        for(Id nb:open(id,0)){bool fresh=!scores.count(nb);discover(nb);if(fresh)frontier.insert({scores.at(nb),nb});}
    }
    for(auto[id,s]:scores)t.disclosed.push_back(id);return t;
}
Bytes domain_bytes(const char*name){return Bytes(name,name+std::strlen(name));}
Hash refinement_leaf(const char*name,Id id,const uint8_t*data,size_t size){
    auto b=domain_bytes(name);p::u32(b,id);b.insert(b.end(),data,data+size);return p::hash(b);
}
template<class Codes>
Hash expansion_leaf(Hash snapshot,Id id,Id level,const std::vector<Id>&ns,int dim,Codes code){
    auto b=domain_bytes("record/expansion-direct/v1");b.reserve(64+ns.size()*(4+dim/2));
    p::digest(b,snapshot);p::u32(b,id);p::u32(b,level);p::u32(b,ns.size());
    for(Id nb:ns){p::u32(b,nb);auto s=code(nb);b.insert(b.end(),s,s+dim/2);}return p::hash(b);
}
std::vector<Id> neighbors(const vann::HnswIndex&h,Id id,Id level){
    auto ns=h.neighbors(id,level);std::sort(ns.begin(),ns.end());return {ns.begin(),ns.end()};
}
struct Anchor {
    Id n,entry,maxlevel,route_count;int dim;
    Hash snapshot,route,delta,raw;Bytes entry_sq;
};
struct ADS {
    std::vector<Id> route_base,position;std::unique_ptr<p::Tree> route,delta,raw;Anchor anchor{};
    ADS(const vann::HnswIndex&h,const Codec&codec,const Hash&snapshot){
        const size_t n=h.size();route_base.resize(n+1);
        for(size_t id=0;id<n;++id)route_base[id+1]=route_base[id]+h.index->hnsw.levels[id];
        std::vector<Hash> leaves(route_base.back());
        for(Id id=0;id<n;++id)for(int l=0;l<h.index->hnsw.levels[id];++l)
            leaves[route_base[id]+l]=expansion_leaf(snapshot,id,l,neighbors(h,id,l),codec.dim,[&](Id nb){return codec.sq(nb);});
        route=std::make_unique<p::Tree>(std::move(leaves));
        int components=0;auto order=deterministic_bfs_order(h,components);position.resize(n);
        leaves.resize(n);std::vector<Hash> raw_leaves(n);
        for(Id pos=0;pos<n;++pos){Id id=order[pos];position[id]=pos;
            leaves[pos]=refinement_leaf("record/delta46/v1",id,codec.delta(id),codec.dim/4);
            raw_leaves[pos]=refinement_leaf("record/raw/v1",id,reinterpret_cast<const uint8_t*>(h.vector(id)),codec.dim*4);
        }
        delta=std::make_unique<p::Tree>(std::move(leaves));raw=std::make_unique<p::Tree>(std::move(raw_leaves));
        anchor={Id(n),Id(h.index->hnsw.entry_point),Id(h.index->hnsw.max_level),route_base.back(),codec.dim,
                snapshot,route->root(),delta->root(),raw->root(),Bytes(codec.sq(h.index->hnsw.entry_point),codec.sq(h.index->hnsw.entry_point)+codec.dim/2)};
        std::cout<<"[ADS] BFS components="<<components<<" route_leaves="<<route_base.back()<<std::endl;
    }
};
// Canonical multiproof positions are inferred from the disclosed targets.
// Only the 32-byte sibling digests are transported, not redundant coordinates.
void proof_write(Bytes&b,const p::Tree&t,const std::set<Id>&targets){
    auto proof=t.prove(targets);p::u32(b,proof.size());for(auto node:proof)p::digest(b,node.value);
}
p::Proof proof_read(p::Reader&r,Id n,std::set<Id> targets){
    Id width=1;while(width<n)width*=2;p::Proof result;
    Id count=r.count(32);
    for(Id level=0;width>1;++level,width/=2){std::set<Id>next;
        for(Id id:targets){if(!targets.count(id^1)){p::require(result.size()<count,"short multiproof");result.push_back({level,id^1,r.digest()});}next.insert(id/2);}targets=std::move(next);
    }p::require(result.size()==count,"noncanonical proof count");return result;
}
bool check_proof(p::Reader&r,Hash root,Id n,const std::map<Id,Hash>&leaves){
    std::set<Id>targets;for(auto[id,h]:leaves)targets.insert(id);
    auto proof=proof_read(r,n,targets);return leaves.empty()?proof.empty():p::Tree::verify(root,n,leaves,proof);
}
struct Packet {Bytes wire;std::vector<Id>answer;size_t refined=0,raw=0,records=0,proofs=0,route_proof=0,sq_offset=0,delta_offset=0,raw_offset=0,adj_offset=0;};
Packet generate(const vann::HnswIndex&h,const Codec&codec,const ADS&ads,const Walk&t,
                const std::vector<Id>&answer,float tau,const float*q,int k,int E){
    Packet out;out.answer=answer;auto&b=out.wire;p::write_header(b,q,codec.dim,k,E,answer);
    p::u32(b,t.disclosed.size());for(Id id:t.disclosed){p::u32(b,id);if(!out.sq_offset)out.sq_offset=b.size();auto s=codec.sq(id);b.insert(b.end(),s,s+codec.dim/2);}
    p::u32(b,t.opened.size());std::set<Id>route_targets,delta_targets,raw_targets;
    for(auto[id,l]:t.opened){Id pos=ads.route_base[id]+l;route_targets.insert(pos);
        p::u32(b,id);p::u32(b,l);p::u32(b,pos);auto ns=neighbors(h,id,l);p::u32(b,ns.size());for(Id nb:ns){if(!out.adj_offset)out.adj_offset=b.size();p::u32(b,nb);}}
    std::vector<Id>refined,raw;
    for(Id id:t.disclosed)if(codec.lower(q,codec.sq(id))<=tau){refined.push_back(id);
        if(codec.lower(q,codec.sq(id),codec.delta(id))<=tau||std::find(answer.begin(),answer.end(),id)!=answer.end())raw.push_back(id);}
    p::u32(b,refined.size());for(Id id:refined){p::u32(b,id);p::u32(b,ads.position[id]);delta_targets.insert(ads.position[id]);auto s=codec.delta(id);if(!out.delta_offset)out.delta_offset=b.size();b.insert(b.end(),s,s+codec.dim/4);}
    p::u32(b,raw.size());for(Id id:raw){p::u32(b,id);p::u32(b,ads.position[id]);raw_targets.insert(ads.position[id]);if(!out.raw_offset)out.raw_offset=b.size();for(int j=0;j<codec.dim;++j)p::f32(b,h.vector(id)[j]);}
    out.refined=refined.size();out.raw=raw.size();out.records=b.size();auto before=b.size();
    proof_write(b,*ads.route,route_targets);out.route_proof=b.size()-before;
    proof_write(b,*ads.delta,delta_targets);proof_write(b,*ads.raw,raw_targets);out.proofs=b.size()-before;
    return out;
}

// The verifier receives a wire VO, query, and trusted roots/quantization metadata.
// It has no server graph, raw dataset, or server trace reference.
bool verify(const Bytes&wire,const Anchor&a,const Codec&codec,const float*q,int k,int E,
            double*auth_ms=nullptr,double*replay_ms=nullptr){try{
    auto started=Clock::now();p::Reader r{wire};auto answer=p::read_header(r,q,a.dim,k,E);
    std::map<Id,Bytes>codes,deltas;std::map<Id,std::vector<float>>raw;
    std::map<Key,std::vector<Id>>links;std::map<Id,Hash>route_leaves,delta_leaves,raw_leaves;
    auto id_read=[&](){Id id=r.u32();p::require(id<a.n,"invalid object id");return id;};
    auto blob=[&](size_t size){r.need(size);Bytes b(wire.begin()+r.p,wire.begin()+r.p+size);r.p+=size;return b;};
    Id count=r.count(4+a.dim/2);for(Id i=0;i<count;++i){Id id=id_read();p::require(codes.emplace(id,blob(a.dim/2)).second,"duplicate SQ4");}
    p::require(codes.at(a.entry)==a.entry_sq,"entry code changed");
    count=r.count(16);for(Id i=0;i<count;++i){Id id=id_read(),level=r.u32(),pos=r.u32();
        p::require(level<=a.maxlevel&&pos<a.route_count,"invalid route record");std::vector<Id>ns;
        Id degree=r.count(4);for(Id j=0;j<degree;++j)ns.push_back(id_read());
        p::require(std::is_sorted(ns.begin(),ns.end())&&std::adjacent_find(ns.begin(),ns.end())==ns.end(),"noncanonical adjacency");
        p::require(links.emplace(Key{id,level},ns).second,"duplicate opened adjacency");
        auto h=expansion_leaf(a.snapshot,id,level,ns,a.dim,[&](Id nb){return codes.at(nb).data();});
        p::require(route_leaves.emplace(pos,h).second,"duplicate route position");
    }
    count=r.count(8+a.dim/4);for(Id i=0;i<count;++i){Id id=id_read(),pos=r.u32();auto b=blob(a.dim/4);
        p::require(codes.count(id)&&deltas.emplace(id,b).second,"invalid delta record");
        p::require(pos<a.n&&delta_leaves.emplace(pos,refinement_leaf("record/delta46/v1",id,b.data(),b.size())).second,"invalid delta position");}
    count=r.count(8+a.dim*4);for(Id i=0;i<count;++i){Id id=id_read(),pos=r.u32();Bytes b;std::vector<float>v;
        for(int j=0;j<a.dim;++j){float x=r.f32();v.push_back(x);p::f32(b,x);}
        p::require(deltas.count(id)&&raw.emplace(id,v).second,"invalid raw record");
        p::require(pos<a.n&&raw_leaves.emplace(pos,refinement_leaf("record/raw/v1",id,b.data(),b.size())).second,"invalid raw position");}
    p::require(check_proof(r,a.route,a.route_count,route_leaves),"route authentication failed");
    p::require(check_proof(r,a.delta,a.n,delta_leaves),"delta authentication failed");
    p::require(check_proof(r,a.raw,a.n,raw_leaves),"raw authentication failed");r.end();
    if(auth_ms)*auth_ms=p::ms(started);started=Clock::now();
    auto trace=walk(codec,q,E,a.entry,a.maxlevel,[&](Id id){return codes.at(id).data();},[&](Id id,Id level){return links.at({id,level});});
    p::require(trace.disclosed.size()==codes.size()&&trace.opened.size()==links.size(),"disclosure/trace mismatch");
    for(Id id:trace.disclosed)p::require(codes.count(id),"missing discovered node");
    std::set<Id>unique_answer;std::vector<std::pair<float,Id>>best;
    for(Id id:answer){p::require(unique_answer.insert(id).second&&raw.count(id),"invalid result");}
    for(auto&[id,v]:raw)best.emplace_back(vann::squared_l2(q,v.data(),a.dim),id);
    std::sort(best.begin(),best.end());p::require(best.size()>=size_t(k),"too few exact candidates");
    for(int i=0;i<k;++i)p::require(best[i].second==answer[i],"top-k mismatch");float tau=best[k-1].first;
    std::set<Id>used_delta,used_raw;
    for(Id id:trace.disclosed){const auto*s=codes.at(id).data();
        if(codec.lower(q,s)>tau)continue;used_delta.insert(id);auto d=deltas.at(id).data();
        if(codec.lower(q,s,d)>tau&&!unique_answer.count(id))continue;used_raw.insert(id);p::require(raw.count(id),"missing raw refinement");
    }
    p::require(used_delta.size()==deltas.size()&&used_raw.size()==raw.size(),"extra refinement records");
    if(replay_ms)*replay_ms=p::ms(started);return true;
}catch(const std::exception&){return false;}}

void attacks(const Packet&packet,const Anchor&a,const Codec&codec,const float*q,int k,int E,const std::string&path){
    std::ofstream out(path+"/tamper_tests.csv");out<<"attack,rejected\n";
    auto test=[&](const std::string&name,Bytes b,const float*query,const Anchor&anchor,int budget){
        bool rejected=!verify(b,anchor,codec,query,k,budget);out<<name<<','<<rejected<<'\n';p::require(rejected,"tamper accepted: "+name);};
    for(auto[name,pos]:std::vector<std::pair<std::string,size_t>>{{"header",0},{"query_binding",4},{"answer",48},{"SQ4",packet.sq_offset},{"Delta4to6",packet.delta_offset},{"raw_vector",packet.raw_offset},{"adjacency",packet.adj_offset},{"proof",packet.wire.size()-1}}){p::require(pos>0||name=="header","missing tamper target");auto b=packet.wire;b.at(pos)^=1;test(name,b,q,a,E);}
    auto b=packet.wire;b.pop_back();test("truncated",b,q,a,E);b=packet.wire;b.push_back(0);test("appended",b,q,a,E);
    auto changed=a;changed.route[0]^=1;test("wrong_root",packet.wire,q,changed,E);
    std::vector<float>query(q,q+codec.dim);query[0]+=1;test("wrong_query",packet.wire,query.data(),a,E);
    test("wrong_expansion_budget",packet.wire,q,a,E+1);
}
} // namespace optimized


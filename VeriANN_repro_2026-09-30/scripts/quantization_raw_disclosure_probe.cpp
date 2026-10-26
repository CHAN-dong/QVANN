// Raw-disclosure control: unchanged SQ4 top-1 traversal and expansion-centric ADS.
#include "quantization_original_include.cpp"
namespace rawq {
namespace p=paper_repro;namespace o=optimized;
using o::Id;using o::Hash;using o::Bytes;using o::Key;
template<class Vector>
Hash leaf(Hash snapshot,Id id,Id level,const std::vector<Id>&ns,int dim,Vector vector){
    auto b=o::domain_bytes("record/expansion-direct-raw/v1");p::digest(b,snapshot);p::u32(b,id);p::u32(b,level);p::u32(b,ns.size());
    for(Id nb:ns){p::u32(b,nb);auto raw=reinterpret_cast<const uint8_t*>(vector(nb));b.insert(b.end(),raw,raw+dim*4);}return p::hash(b);
}
struct Anchor{Id n,entry,maxlevel,route_count;int dim;Hash snapshot,root;std::vector<float>entry_raw;};
struct ADS {
    Anchor anchor;std::vector<Id>base;std::unique_ptr<p::Tree>tree;
    ADS(const vann::HnswIndex&h,Hash data){
        base.resize(h.size()+1);for(Id id=0;id<h.size();++id)base[id+1]=base[id]+h.index->hnsw.levels[id];
        std::vector<Hash>leaves(base.back());
        for(Id id=0;id<h.size();++id)for(int l=0;l<h.index->hnsw.levels[id];++l)leaves[base[id]+l]=leaf(data,id,l,o::neighbors(h,id,l),h.dimension(),[&](Id nb){return h.vector(nb);});
        tree=std::make_unique<p::Tree>(std::move(leaves));Id entry=h.index->hnsw.entry_point;
        anchor={Id(h.size()),entry,Id(h.index->hnsw.max_level),base.back(),h.dimension(),data,tree->root(),std::vector<float>(h.vector(entry),h.vector(entry)+h.dimension())};
        std::cout<<"RAW_ADS_READY "<<base.back()<<std::endl;
    }
};
struct Packet{Bytes wire;size_t raw_offset=0,adj_offset=0,record_end=0,raw_begin=0,raw_end=0,count_offset=0;};
Packet generate(const vann::HnswIndex&h,const ADS&a,const o::Walk&t,const float*q,int E,const std::vector<Id>&answer){
    Packet out;auto&b=out.wire;p::write_header(b,q,h.dimension(),10,E,answer);out.count_offset=b.size();p::u32(b,t.disclosed.size());
    for(Id id:t.disclosed){size_t begin=b.size();p::u32(b,id);if(!out.raw_offset)out.raw_offset=b.size();for(int j=0;j<h.dimension();++j)p::f32(b,h.vector(id)[j]);if(!out.raw_end){out.raw_begin=begin;out.raw_end=b.size();}}
    p::u32(b,t.opened.size());std::set<Id>targets;
    for(auto[id,l]:t.opened){Id pos=a.base[id]+l;targets.insert(pos);p::u32(b,id);p::u32(b,l);p::u32(b,pos);auto ns=o::neighbors(h,id,l);p::u32(b,ns.size());for(Id nb:ns){if(!out.adj_offset)out.adj_offset=b.size();p::u32(b,nb);}}
    out.record_end=b.size();o::proof_write(b,*a.tree,targets);return out;
}
bool verify(const Bytes&wire,const Anchor&a,const o::Codec&codec,const float*q,int E){try{
    p::Reader r{wire};auto answer=p::read_header(r,q,a.dim,10,E);std::map<Id,std::vector<float>>raw;std::map<Id,Bytes>codes;
    auto read_id=[&](){Id id=r.u32();p::require(id<a.n,"invalid id");return id;};
    Id count=r.count(4+a.dim*4);
    for(Id i=0;i<count;++i){Id id=read_id();std::vector<float>v(a.dim);Bytes code(a.dim/2);
        for(int j=0;j<a.dim;++j){v[j]=r.f32();double u=codec.span[j]>0?(v[j]-codec.lo[j])/codec.span[j]:0;int sq6=std::clamp(int(std::floor(u*64)),0,63);code[j/2]|=uint8_t((sq6>>2)<<((j%2)*4));}
        p::require(raw.emplace(id,std::move(v)).second&&codes.emplace(id,std::move(code)).second,"duplicate raw record");}
    p::require(raw.count(a.entry)&&memcmp(raw.at(a.entry).data(),a.entry_raw.data(),a.dim*4)==0,"entry vector mismatch");
    std::map<Key,std::vector<Id>>links;std::map<Id,Hash>leaves;
    count=r.count(16);for(Id i=0;i<count;++i){Id id=read_id(),level=r.u32(),pos=r.u32();p::require(level<=a.maxlevel&&pos<a.route_count,"invalid route");Id degree=r.count(4);std::vector<Id>ns;for(Id j=0;j<degree;++j)ns.push_back(read_id());
        p::require(std::is_sorted(ns.begin(),ns.end())&&std::adjacent_find(ns.begin(),ns.end())==ns.end(),"noncanonical adjacency");p::require(links.emplace(Key{id,level},ns).second,"duplicate adjacency");
        auto hash=leaf(a.snapshot,id,level,ns,a.dim,[&](Id nb){return raw.at(nb).data();});p::require(leaves.emplace(pos,hash).second,"duplicate route position");}
    p::require(o::check_proof(r,a.root,a.route_count,leaves),"authentication failure");r.end();
    auto trace=o::walk(codec,q,E,a.entry,a.maxlevel,[&](Id id){return codes.at(id).data();},[&](Id id,Id l){return links.at({id,l});});
    p::require(trace.disclosed.size()==raw.size()&&trace.opened.size()==links.size(),"trace mismatch");for(Id id:trace.disclosed)p::require(raw.count(id),"missing record");
    std::vector<std::pair<float,Id>>exact;for(auto&[id,v]:raw)exact.emplace_back(vann::squared_l2(q,v.data(),a.dim),id);std::sort(exact.begin(),exact.end());p::require(exact.size()>=10,"too few candidates");
    for(int j=0;j<10;++j)p::require(exact[j].second==answer[j],"top-k mismatch");return true;
}catch(const std::exception&){return false;}}
Hash trace_hash(const o::Walk&t){Bytes b;for(Id id:t.disclosed)p::u32(b,id);for(auto[id,l]:t.opened){p::u32(b,id);p::u32(b,l);}return p::hash(b);}
}
int main(int argc,char**argv)try{
    namespace p=paper_repro;namespace o=optimized;using namespace rawq;
    auto c=p::args(argc,argv);p::keccak=false;omp_set_num_threads(1);p::require(c.ks==std::vector<int>{10}&&c.efs.size()==1&&c.queries==20&&c.repeats==3&&c.queryoffset==100,"controlled20x3 k10 study");int E=c.efs.front();
    auto base=vann::read_fvecs(c.dataset+"/sift_base.fvecs",c.base),queries=p::read_queries(c);auto data=p::data_hash(base);auto truth=p::ground_truth(base,queries,10);
    auto cache=c.cache+"/"+p::hex(data).substr(0,16)+"_M"+std::to_string(c.M)+"_efc"+std::to_string(c.efc)+".index";p::require(std::filesystem::exists(cache),"original graph required");auto h=vann::build_hnsw(base,c.M,c.efc,cache);base.values.clear();base.values.shrink_to_fit();
    o::Codec codec(h);o::Codec client(codec,true);rawq::ADS ads(h,data);std::filesystem::create_directories(c.output);
    std::ofstream f(c.output+"/samples.csv");f<<"configuration,query,repeat,E,recall,query_ms,verify_ms,vo_bytes,record_bytes,proof_bytes,disclosed,expanded,trace_sha256,answer,valid\n"<<std::setprecision(17);
    std::ofstream attacks(c.output+"/tamper.csv");attacks<<"attack,rejected\n";int negative=0;
    for(int rep=0;rep<3;++rep)for(int qi=0;qi<20;++qi){const float*q=queries.row(qi);codec.last_query.clear();auto start=p::Clock::now();
        auto t=o::walk(codec,q,E,ads.anchor.entry,ads.anchor.maxlevel,[&](Id id){return codec.sq(id);},[&](Id id,Id l){return o::neighbors(h,id,l);});
        std::vector<std::pair<float,Id>>exact;for(Id id:t.disclosed)exact.emplace_back(vann::squared_l2(q,h.vector(id),h.dimension()),id);p::require(exact.size()>=10,"too few candidates");std::partial_sort(exact.begin(),exact.begin()+10,exact.end());std::vector<Id>answer;for(int j=0;j<10;++j)answer.push_back(exact[j].second);
        auto packet=rawq::generate(h,ads,t,q,E,answer);double query_ms=p::ms(start);client.last_query.clear();start=p::Clock::now();bool valid=rawq::verify(packet.wire,ads.anchor,client,q,E);double verify_ms=p::ms(start);p::require(valid,"honest Raw VO rejected");
        int hits=0;for(Id id:answer)hits+=std::find(truth[qi].begin(),truth[qi].end(),id)!=truth[qi].end();f<<"Raw,"<<100+qi<<','<<rep<<','<<E<<','<<hits/10.0<<','<<query_ms<<','<<verify_ms<<','<<packet.wire.size()<<','<<packet.record_end<<','<<packet.wire.size()-packet.record_end<<','<<t.disclosed.size()<<','<<t.expanded.size()<<','<<p::hex(trace_hash(t))<<',';for(Id id:answer)f<<id<<' ';f<<",1\n";f.flush();
        if(qi==0&&rep==0){
            p::save(c.output+"/query0.vo",packet.wire);auto reject=[&](std::string name,const Bytes&b,const Anchor&a,const float*qq,int budget){bool rejected=!rawq::verify(b,a,client,qq,budget);attacks<<name<<','<<rejected<<'\n';p::require(rejected,"tamper accepted: "+name);++negative;};
            for(auto[name,pos]:std::vector<std::pair<std::string,size_t>>{{"header",0},{"query_binding",4},{"answer",48},{"raw",packet.raw_offset},{"adjacency",packet.adj_offset},{"proof",packet.wire.size()-1}}){auto b=packet.wire;b.at(pos)^=1;reject(name,b,ads.anchor,q,E);}
            auto b=packet.wire;b.pop_back();reject("truncated",b,ads.anchor,q,E);b=packet.wire;b.push_back(0);reject("appended",b,ads.anchor,q,E);auto anchor=ads.anchor;anchor.root[0]^=1;reject("wrong_root",packet.wire,anchor,q,E);std::vector<float>qq(q,q+codec.dim);qq[0]+=1;reject("wrong_query",packet.wire,ads.anchor,qq.data(),E);reject("wrong_budget",packet.wire,ads.anchor,q,E+1);
            b=packet.wire;b.erase(b.begin()+packet.raw_begin,b.begin()+packet.raw_end);Id n=t.disclosed.size()-1;for(int j=0;j<4;++j)b[packet.count_offset+j]=uint8_t(n>>(8*j));reject("omitted_raw_record",b,ads.anchor,q,E);
        }
        if(qi==19)std::cout<<"REPEAT_COMPLETE "<<rep<<std::endl;
    }
    std::ofstream(c.output+"/identity.json")<<"{\"N\":"<<h.size()<<",\"D\":"<<h.dimension()<<",\"dataset_sha256\":\""<<p::hex(data)<<"\",\"E\":"<<E<<",\"threads\":1,\"samples\":60,\"tamper_tests\":"<<negative<<"}";
    std::ofstream(c.output+"/PASS.txt")<<"Raw disclosure with fixed SQ4 top-1 traversal;60 authenticated queries passed.\n";return 0;
}catch(const std::exception&e){std::cerr<<"ERROR "<<e.what()<<std::endl;return 1;}

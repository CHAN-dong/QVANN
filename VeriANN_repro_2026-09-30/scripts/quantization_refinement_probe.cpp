// Controlled refinement experiment; the included original traversal is unchanged.
#include "quantization_original_include.cpp"
namespace qr {
namespace o=optimized; namespace p=paper_repro;
using o::Id; using o::Bytes; using o::Hash; using o::Key;
struct Mode {const char*name;std::vector<int>bits;};
const std::vector<Mode> modes={{"SQ4--raw",{}},{"SQ4--SQ6--raw",{6}},{"SQ4--SQ8--raw",{8}},{"SQ4--SQ6--SQ8--raw",{6,8}}};
// Cover rounding in float32 subtraction/division used by the original nested
// quantizer, as well as interval reconstruction (especially cancellation near0).
// A single nextafter of the reconstructed endpoint alone is insufficient.
std::pair<float,float> interval8(float lo,float span,int code){
    double margin=8.0*std::numeric_limits<float>::epsilon()*(std::abs(double(lo))+std::abs(double(span)));
    float a=std::nextafter(float(double(lo)+double(span)*code/256-margin),-INFINITY);
    float b=std::nextafter(float(double(lo)+double(span)*(code+1)/256+margin),INFINITY);
    return {a,b};
}
struct Extension {
    int dim;Bytes d48,d68;const o::Codec&codec;
    mutable std::vector<float> last,table;
    Extension(const vann::HnswIndex&h,const o::Codec&c):dim(c.dim),d48(h.size()*size_t(dim/2)),d68(h.size()*size_t(dim/4)),codec(c){
        for(Id id=0;id<h.size();++id)for(int j=0;j<dim;++j){
            double u=c.span[j]>0?(h.vector(id)[j]-c.lo[j])/c.span[j]:0;
            int code=std::clamp(int(std::floor(u*256)),0,255);
            int sq4=(c.sq(id)[j/2]>>((j%2)*4))&15;
            int sq6=(sq4<<2)|((c.delta(id)[j/4]>>((j%4)*2))&3);
            p::require((code>>2)==sq6,"SQ8 must extend the original SQ6 code");
            auto [a,b]=interval8(c.lo[j],c.span[j],code);
            if(!(a<=h.vector(id)[j]&&h.vector(id)[j]<=b)){
                std::cerr<<std::setprecision(17)<<"SQ8 interval: id="<<id<<" j="<<j<<" raw="<<h.vector(id)[j]<<" a="<<a<<" b="<<b<<" code="<<code<<" lo="<<c.lo[j]<<" span="<<c.span[j]<<std::endl;
                throw std::runtime_error("SQ8 interval does not contain raw coordinate");
            }
            d48[id*size_t(dim/2)+j/2]|=uint8_t((code&15)<<((j%2)*4));
            d68[id*size_t(dim/4)+j/4]|=uint8_t((code&3)<<((j%4)*2));
        }
    }
    // Client instance carries bounds only, never server-sized quantization arrays.
    explicit Extension(const o::Codec&c):dim(c.dim),codec(c){}
    void prepare(const float*q)const{
        if(last.size()==size_t(dim)&&memcmp(last.data(),q,dim*4)==0)return;
        table.resize(size_t(dim)*256);
        for(int j=0;j<dim;++j)for(int code=0;code<256;++code){
            auto [a,b]=interval8(codec.lo[j],codec.span[j],code);
            float z=q[j]<a?a-q[j]:(q[j]>b?q[j]-b:0);table[size_t(j)*256+code]=z*z;
        }last.assign(q,q+dim);
    }
    const uint8_t*delta(Id id,int prev,int bits)const{
        if(bits==6)return codec.delta(id);
        return prev==4?d48.data()+size_t(id)*(dim/2):d68.data()+size_t(id)*(dim/4);
    }
    float lower8(const float*q,const uint8_t*sq,const uint8_t*d6,const uint8_t*d8)const{
        prepare(q);float total=0;
        for(int j=0;j<dim;++j){int code=(sq[j/2]>>((j%2)*4))&15;
            if(d6)code=(code<<4)|(((d6[j/4]>>((j%4)*2))&3)<<2)|((d8[j/4]>>((j%4)*2))&3);
            else code=(code<<4)|((d8[j/2]>>((j%2)*4))&15);
            total+=table[size_t(j)*256+code];
        }return std::nextafter(total,0.F);
    }
};
const char*domain(int prev,int bits){return bits==6?"record/delta46/v1":prev==4?"record/delta48/v1":"record/delta68/v1";}
struct ExtraTrees {
    std::unique_ptr<p::Tree> t48,t68;
    ExtraTrees(const vann::HnswIndex&h,const o::ADS&a,const Extension&e){
        std::vector<Hash>x(h.size()),y(h.size());
        for(Id id=0;id<h.size();++id){auto pos=a.position[id];x[pos]=o::refinement_leaf(domain(4,8),id,e.delta(id,4,8),e.dim/2);y[pos]=o::refinement_leaf(domain(6,8),id,e.delta(id,6,8),e.dim/4);}
        t48=std::make_unique<p::Tree>(std::move(x));t68=std::make_unique<p::Tree>(std::move(y));
    }
};
struct Trusted {
    o::Anchor base;std::vector<Hash>roots;std::vector<int>bits;
};
Trusted trusted(const o::ADS&a,const ExtraTrees&t,const Mode&m){
    Trusted z{a.anchor,{},m.bits};int prev=4;
    for(int bits:m.bits){z.roots.push_back(bits==6?a.delta->root():prev==4?t.t48->root():t.t68->root());prev=bits;}return z;
}
struct Packet {Bytes wire;std::vector<Id>answer;std::vector<size_t>stage_offsets,stage_counts;size_t raw_count=0,sq_offset=0,raw_offset=0,adj_offset=0;};
Packet generate(const vann::HnswIndex&h,const o::Codec&c,const Extension&e,const o::ADS&a,const ExtraTrees&trees,
                const o::Walk&t,const std::vector<Id>&answer,float tau,const float*q,int k,int E,const Mode&m){
    Packet out;out.answer=answer;auto&b=out.wire;p::write_header(b,q,c.dim,k,E,answer);
    p::u32(b,t.disclosed.size());for(Id id:t.disclosed){p::u32(b,id);if(!out.sq_offset)out.sq_offset=b.size();b.insert(b.end(),c.sq(id),c.sq(id)+c.dim/2);}
    p::u32(b,t.opened.size());std::set<Id>rt,raw_targets;
    for(auto[id,l]:t.opened){Id pos=a.route_base[id]+l;rt.insert(pos);p::u32(b,id);p::u32(b,l);p::u32(b,pos);auto ns=o::neighbors(h,id,l);p::u32(b,ns.size());for(Id nb:ns){if(!out.adj_offset)out.adj_offset=b.size();p::u32(b,nb);}}
    std::vector<Id>active;for(Id id:t.disclosed)if(c.lower(q,c.sq(id))<=tau)active.push_back(id);
    std::vector<std::set<Id>>targets;int prev=4;
    for(int bits:m.bits){
        targets.emplace_back();out.stage_counts.push_back(active.size());out.stage_offsets.push_back(0);p::u32(b,active.size());std::vector<Id>next;
        for(Id id:active){p::u32(b,id);p::u32(b,a.position[id]);targets.back().insert(a.position[id]);auto d=e.delta(id,prev,bits);size_t size=c.dim*(bits-prev)/8;if(!out.stage_offsets.back())out.stage_offsets.back()=b.size();b.insert(b.end(),d,d+size);
            float lb=bits==6?c.lower(q,c.sq(id),d):e.lower8(q,c.sq(id),prev==6?c.delta(id):nullptr,d);
            if(lb<=tau||std::find(answer.begin(),answer.end(),id)!=answer.end())next.push_back(id);
        }active.swap(next);prev=bits;
    }
    out.raw_count=active.size();p::u32(b,active.size());for(Id id:active){p::u32(b,id);p::u32(b,a.position[id]);raw_targets.insert(a.position[id]);if(!out.raw_offset)out.raw_offset=b.size();for(int j=0;j<c.dim;++j)p::f32(b,h.vector(id)[j]);}
    o::proof_write(b,*a.route,rt);prev=4;
    for(size_t i=0;i<m.bits.size();++i){int bits=m.bits[i];const p::Tree*tree=bits==6?a.delta.get():prev==4?trees.t48.get():trees.t68.get();o::proof_write(b,*tree,targets[i]);prev=bits;}
    o::proof_write(b,*a.raw,raw_targets);return out;
}
bool verify(const Bytes&wire,const Trusted&a,const o::Codec&c,const Extension&e,const float*q,int k,int E){try{
    p::Reader r{wire};auto answer=p::read_header(r,q,a.base.dim,k,E);const auto&anchor=a.base;
    std::map<Id,Bytes>sq;std::map<Key,std::vector<Id>>links;std::map<Id,Hash>route_leaves,raw_leaves;
    std::vector<std::map<Id,Bytes>>stages(a.bits.size());std::vector<std::map<Id,Hash>>leaves(a.bits.size());
    std::map<Id,std::vector<float>>raw;
    auto id_read=[&](){Id id=r.u32();p::require(id<anchor.n,"invalid object id");return id;};
    auto blob=[&](size_t n){r.need(n);Bytes b(wire.begin()+r.p,wire.begin()+r.p+n);r.p+=n;return b;};
    Id count=r.count(4+c.dim/2);for(Id i=0;i<count;++i){Id id=id_read();p::require(sq.emplace(id,blob(c.dim/2)).second,"duplicate SQ4");}
    p::require(sq.at(anchor.entry)==anchor.entry_sq,"entry mismatch");
    count=r.count(16);for(Id i=0;i<count;++i){Id id=id_read(),level=r.u32(),pos=r.u32();p::require(level<=anchor.maxlevel&&pos<anchor.route_count,"invalid route");Id degree=r.count(4);std::vector<Id>ns;for(Id j=0;j<degree;++j)ns.push_back(id_read());
        p::require(std::is_sorted(ns.begin(),ns.end())&&std::adjacent_find(ns.begin(),ns.end())==ns.end(),"noncanonical adjacency");p::require(links.emplace(Key{id,level},ns).second,"duplicate adjacency");
        auto hash=o::expansion_leaf(anchor.snapshot,id,level,ns,c.dim,[&](Id nb){return sq.at(nb).data();});p::require(route_leaves.emplace(pos,hash).second,"duplicate route position");}
    int prev=4;
    for(size_t s=0;s<a.bits.size();++s){int bits=a.bits[s];size_t size=c.dim*(bits-prev)/8;count=r.count(8+size);
        for(Id i=0;i<count;++i){Id id=id_read(),pos=r.u32();auto b=blob(size);p::require(sq.count(id)&&(!s||stages[s-1].count(id))&&stages[s].emplace(id,b).second,"invalid stage record");p::require(pos<anchor.n&&leaves[s].emplace(pos,o::refinement_leaf(domain(prev,bits),id,b.data(),b.size())).second,"invalid stage position");}prev=bits;}
    count=r.count(8+c.dim*4);for(Id i=0;i<count;++i){Id id=id_read(),pos=r.u32();std::vector<float>v;Bytes b;for(int j=0;j<c.dim;++j){float x=r.f32();p::require(std::isfinite(x),"nonfinite vector");v.push_back(x);p::f32(b,x);}p::require(sq.count(id)&&(stages.empty()||stages.back().count(id))&&raw.emplace(id,v).second,"invalid raw record");p::require(pos<anchor.n&&raw_leaves.emplace(pos,o::refinement_leaf("record/raw/v1",id,b.data(),b.size())).second,"invalid raw position");}
    p::require(o::check_proof(r,anchor.route,anchor.route_count,route_leaves),"route authentication");
    for(size_t s=0;s<stages.size();++s)p::require(o::check_proof(r,a.roots[s],anchor.n,leaves[s]),"stage authentication");
    p::require(o::check_proof(r,anchor.raw,anchor.n,raw_leaves),"raw authentication");r.end();
    auto t=o::walk(c,q,E,anchor.entry,anchor.maxlevel,[&](Id id){return sq.at(id).data();},[&](Id id,Id l){return links.at({id,l});});
    p::require(t.disclosed.size()==sq.size()&&t.opened.size()==links.size(),"trace mismatch");for(Id id:t.disclosed)p::require(sq.count(id),"missing SQ4");
    std::set<Id>unique;for(Id id:answer)p::require(unique.insert(id).second&&raw.count(id),"invalid answer");
    std::vector<std::pair<float,Id>>best;for(auto&[id,v]:raw)best.emplace_back(vann::squared_l2(q,v.data(),c.dim),id);std::sort(best.begin(),best.end());p::require(best.size()>=size_t(k),"too few raw vectors");for(int i=0;i<k;++i)p::require(best[i].second==answer[i],"top-k mismatch");float tau=best[k-1].first;
    std::vector<size_t>used(stages.size());size_t used_raw=0;
    for(Id id:t.disclosed){auto s=sq.at(id).data();if(c.lower(q,s)>tau)continue;bool pass=true;int prevbits=4;
        for(size_t j=0;j<stages.size();++j){++used[j];auto d=stages[j].at(id).data();float lb=a.bits[j]==6?c.lower(q,s,d):e.lower8(q,s,prevbits==6?stages[j-1].at(id).data():nullptr,d);if(lb>tau&&!unique.count(id)){pass=false;break;}prevbits=a.bits[j];}
        if(pass){++used_raw;p::require(raw.count(id),"missing raw vector");}}
    for(size_t s=0;s<used.size();++s)p::require(used[s]==stages[s].size(),"extra stage records");p::require(used_raw==raw.size(),"extra raw records");return true;
}catch(const std::exception&){return false;}}
Hash trace_hash(const o::Walk&t){Bytes b;for(Id id:t.disclosed)p::u32(b,id);for(auto[id,l]:t.opened){p::u32(b,id);p::u32(b,l);}return p::hash(b);}
}

int main(int argc,char**argv)try{
    namespace p=paper_repro;namespace o=optimized;using namespace qr;
    auto cfg=p::args(argc,argv);p::keccak=false;omp_set_num_threads(1);
    p::require(cfg.ks==std::vector<int>{10}&&cfg.efs.size()==1&&cfg.queries==20&&cfg.repeats==3&&cfg.queryoffset==100,"controlled study requires k10,20x3,offset100,one E");int E=cfg.efs[0];
    auto base=vann::read_fvecs(cfg.dataset+"/sift_base.fvecs",cfg.base),queries=p::read_queries(cfg);auto data=p::data_hash(base);auto truth=p::ground_truth(base,queries,10);
    std::string cache=cfg.cache+"/"+p::hex(data).substr(0,16)+"_M"+std::to_string(cfg.M)+"_efc"+std::to_string(cfg.efc)+".index";
    p::require(std::filesystem::exists(cache),"original graph cache required");auto h=vann::build_hnsw(base,cfg.M,cfg.efc,cache);base.values.clear();base.values.shrink_to_fit();
    o::Codec codec(h);o::ADS ads(h,codec,data);Extension ext(h,codec);ExtraTrees trees(h,ads,ext);o::Codec client(codec,true);Extension client_ext(client);
    std::filesystem::create_directories(cfg.output);std::ofstream gt(cfg.output+"/ground_truth.csv");gt<<"query,answer\n";for(int qi=0;qi<20;++qi){gt<<qi+100<<',';for(auto id:truth[qi])gt<<id<<' ';gt<<'\n';}gt.close();
    std::ofstream csv(cfg.output+"/samples.csv");csv<<"configuration,query,repeat,E,recall,search_ms,vo_generation_ms,query_ms,verify_ms,vo_bytes,disclosed,expanded,raw_vectors,stage1_records,stage2_records,trace_sha256,answer,valid\n"<<std::setprecision(17);
    std::ofstream attacks(cfg.output+"/tamper.csv");attacks<<"configuration,attack,rejected\n";
    size_t negative_tests=0,legacy_checks=0;std::map<std::pair<int,int>,Hash>same_trace;std::map<std::pair<int,int>,std::vector<Id>>same_answer;
    // Rotate configuration order across queries to distribute timing/cache effects.
    for(int repeat=0;repeat<3;++repeat)for(int qi=0;qi<20;++qi)for(int slot=0;slot<4;++slot){int mode=(slot+qi+repeat)%4;const auto&m=modes[mode];auto q=queries.row(qi);codec.last_query.clear();ext.last.clear();auto start=p::Clock::now();
        auto t=o::walk(codec,q,E,ads.anchor.entry,ads.anchor.maxlevel,[&](Id id){return codec.sq(id);},[&](Id id,Id l){return o::neighbors(h,id,l);});std::vector<std::pair<float,Id>>exact;for(Id id:t.disclosed)exact.emplace_back(vann::squared_l2(q,h.vector(id),h.dimension()),id);p::require(exact.size()>=10,"insufficient candidates");std::partial_sort(exact.begin(),exact.begin()+10,exact.end());std::vector<Id>answer;for(int j=0;j<10;++j)answer.push_back(exact[j].second);float tau=exact[9].first;double search_ms=p::ms(start);
        start=p::Clock::now();auto packet=qr::generate(h,codec,ext,ads,trees,t,answer,tau,q,10,E,m);double vo_ms=p::ms(start);auto anchor=trusted(ads,trees,m);client.last_query.clear();client_ext.last.clear();start=p::Clock::now();bool valid=qr::verify(packet.wire,anchor,client,client_ext,q,10,E);double verify_ms=p::ms(start);p::require(valid,"honest VO rejected");
        auto th=trace_hash(t);auto key=std::make_pair(qi,repeat);if(!same_trace.count(key)){same_trace[key]=th;same_answer[key]=answer;}else p::require(th==same_trace.at(key)&&answer==same_answer.at(key),"refinement changed traversal/answer");
        std::set<Id>gtset(truth[qi].begin(),truth[qi].end());int hits=0;for(Id id:answer)hits+=gtset.count(id);
        csv<<m.name<<','<<qi+100<<','<<repeat<<','<<E<<','<<hits/10.0<<','<<search_ms<<','<<vo_ms<<','<<search_ms+vo_ms<<','<<verify_ms<<','<<packet.wire.size()<<','<<t.disclosed.size()<<','<<t.expanded.size()<<','<<packet.raw_count<<','<<(packet.stage_counts.empty()?0:packet.stage_counts[0])<<','<<(packet.stage_counts.size()<2?0:packet.stage_counts[1])<<','<<p::hex(th)<<',';for(Id id:answer)csv<<id<<' ';csv<<",1\n";csv.flush();
        if(mode==1){auto native=o::generate(h,codec,ads,t,answer,tau,q,10,E);p::require(native.wire==packet.wire,"SQ4-SQ6-raw wire differs from original protocol");p::require(o::verify(packet.wire,ads.anchor,client,q,10,E),"original verifier rejected SQ6 baseline");++legacy_checks;}
        if(qi==0&&repeat==0){
            std::string tag=std::to_string(mode);p::save(cfg.output+"/configuration"+tag+".vo",packet.wire);
            auto reject=[&](std::string name,const Bytes&b,const float*qq,const Trusted&a,int ee){bool rejected=!qr::verify(b,a,client,client_ext,qq,10,ee);attacks<<m.name<<','<<name<<','<<rejected<<'\n';p::require(rejected,"tamper accepted: "+name);++negative_tests;};
            std::vector<std::pair<std::string,size_t>>positions={{"header",0},{"query_binding",4},{"SQ4",packet.sq_offset},{"adjacency",packet.adj_offset},{"raw",packet.raw_offset},{"proof",packet.wire.size()-1}};
            for(size_t j=0;j<packet.stage_offsets.size();++j)if(packet.stage_offsets[j])positions.push_back({"stage"+std::to_string(j+1),packet.stage_offsets[j]});
            for(auto[name,pos]:positions){auto b=packet.wire;b.at(pos)^=1;reject(name,b,q,anchor,E);}auto b=packet.wire;b.pop_back();reject("truncated",b,q,anchor,E);b=packet.wire;b.push_back(0);reject("appended",b,q,anchor,E);auto wrong=anchor;wrong.base.route[0]^=1;reject("wrong_route_root",packet.wire,q,wrong,E);wrong=anchor;wrong.base.raw[0]^=1;reject("wrong_raw_root",packet.wire,q,wrong,E);
            for(size_t j=0;j<anchor.roots.size();++j){wrong=anchor;wrong.roots[j][0]^=1;reject("wrong_stage_root"+std::to_string(j+1),packet.wire,q,wrong,E);}std::vector<float>qq(q,q+codec.dim);qq[0]+=1;reject("wrong_query",packet.wire,qq.data(),anchor,E);reject("wrong_budget",packet.wire,q,anchor,E+1);
        }
        if(slot==3&&qi==19)std::cout<<"REPEAT_COMPLETE "<<repeat<<std::endl;
    }
    std::ofstream meta(cfg.output+"/identity.json");meta<<"{\"N\":"<<h.size()<<",\"D\":"<<h.dimension()<<",\"dataset_sha256\":\""<<p::hex(data)<<"\",\"E\":"<<E<<",\"M\":"<<cfg.M<<",\"ef_construction\":"<<cfg.efc<<",\"queries\":20,\"repeats\":3,\"threads\":1,\"legacy_wire_checks\":"<<legacy_checks<<",\"tamper_tests\":"<<negative_tests<<"}";
    std::ofstream(cfg.output+"/PASS.txt")<<"240 authenticated searches; fixed SQ4 traversal and answers; SQ6 baseline byte-identical to original; all honest and negative cases pass.\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"ERROR "<<e.what()<<std::endl;return 1;}

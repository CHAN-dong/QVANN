// Routing-precision ablation. Original QVANN protocol is included unchanged.
#include "routing_precision_original_include.cpp"
namespace rp {
namespace p=paper_repro;namespace o=optimized;
using o::Id;using o::Key;using o::Bytes;using o::Hash;
struct Trace {o::Walk walk;std::vector<Key>order;std::map<Id,float>exact_scores;};

// Same control flow and unique-expanded-node budget as optimized::walk.
// Every discovery/upper-layer comparison/frontier decision uses the supplied
// unquantized distance scorer. No SQ4 score is consulted by Exact routing.
template<class Score,class Adjacent>
o::Walk exact_walk(int E,Id first,Id maxlevel,Score distance,Adjacent adjacent,std::map<Id,float>*keep=nullptr){
    o::Walk t;std::map<Id,float>scores;
    auto discover=[&](Id id){if(!scores.count(id))scores[id]=distance(id);};
    auto open=[&](Id id,Id level){Key key{id,level};if(t.opened.count(key))return std::vector<Id>{};
        if(!t.expanded.count(id)){if(int(t.expanded.size())>=E)return std::vector<Id>{};t.expanded.insert(id);}t.opened.insert(key);return adjacent(id,level);};
    Id entry=first;discover(entry);bool stopped=false;
    for(Id level=maxlevel;level>0&&!stopped;--level)while(true){
        auto ns=open(entry,level);if(ns.empty()){stopped=int(t.expanded.size())>=E;break;}
        for(Id id:ns)discover(id);ns.push_back(entry);
        Id next=*std::min_element(ns.begin(),ns.end(),[&](Id a,Id b){return std::make_pair(scores.at(a),a)<std::make_pair(scores.at(b),b);});
        if(next==entry)break;entry=next;
    }
    std::set<std::pair<float,Id>>frontier;for(auto[id,s]:scores)frontier.insert({s,id});
    while(int(t.expanded.size())<E&&!frontier.empty()){
        Id id=frontier.begin()->second;frontier.erase(frontier.begin());
        for(Id nb:open(id,0)){bool fresh=!scores.count(nb);discover(nb);if(fresh)frontier.insert({scores.at(nb),nb});}}
    for(auto[id,s]:scores)t.disclosed.push_back(id);if(keep)*keep=std::move(scores);return t;
}
Trace route(const vann::HnswIndex&h,const o::Codec&c,const float*q,int E,bool exact){
    Trace out;auto adjacent=[&](Id id,Id l){out.order.emplace_back(id,l);return o::neighbors(h,id,l);};
    if(exact)out.walk=exact_walk(E,h.index->hnsw.entry_point,h.index->hnsw.max_level,[&](Id id){return vann::squared_l2(q,h.vector(id),h.dimension());},adjacent,&out.exact_scores);
    else out.walk=o::walk(c,q,E,h.index->hnsw.entry_point,h.index->hnsw.max_level,[&](Id id){return c.sq(id);},adjacent);
    return out;
}
Hash fingerprint(const Trace&t){Bytes b;for(Id id:t.walk.disclosed)p::u32(b,id);for(auto[id,l]:t.order){p::u32(b,id);p::u32(b,l);}return p::hash(b);}
std::vector<Id>answer(const vann::HnswIndex&h,const Trace&t,const float*q,float&tau){
    std::vector<std::pair<float,Id>>best;for(Id id:t.walk.disclosed)best.emplace_back(t.exact_scores.empty()?vann::squared_l2(q,h.vector(id),h.dimension()):t.exact_scores.at(id),id);
    p::require(best.size()>=10,"candidate set smaller than k10");std::partial_sort(best.begin(),best.begin()+10,best.end());std::vector<Id>a;for(int i=0;i<10;++i)a.push_back(best[i].second);tau=best[9].first;return a;
}
struct Packet {o::Packet body;Bytes wire;size_t final_raw=0;};
Packet generate(const vann::HnswIndex&h,const o::Codec&c,const o::ADS&a,const Trace&t,const std::vector<Id>&answer,float tau,const float*q,int E,bool exact){
    c.prepare(q);Packet packet;auto&out=packet.body;out.answer=answer;auto&b=out.wire;p::write_header(b,q,c.dim,10,E,answer);
    p::u32(b,t.walk.disclosed.size());for(Id id:t.walk.disclosed){p::u32(b,id);if(!out.sq_offset)out.sq_offset=b.size();b.insert(b.end(),c.sq(id),c.sq(id)+c.dim/2);}
    p::u32(b,t.walk.opened.size());std::set<Id>rt,dt,raw_targets;
    for(auto[id,l]:t.walk.opened){Id pos=a.route_base[id]+l;rt.insert(pos);p::u32(b,id);p::u32(b,l);p::u32(b,pos);auto ns=o::neighbors(h,id,l);p::u32(b,ns.size());for(Id nb:ns){if(!out.adj_offset)out.adj_offset=b.size();p::u32(b,nb);}}
    std::vector<Id>refined,raw;
    for(Id id:t.walk.disclosed)if(c.lower(q,c.sq(id))<=tau){refined.push_back(id);if(c.lower(q,c.sq(id),c.delta(id))<=tau||std::find(answer.begin(),answer.end(),id)!=answer.end())raw.push_back(id);}
    packet.final_raw=raw.size();
    // Exact decisions require raw distances of ALL scored/discovered candidates.
    // Replace the final-only set by its union with C, rather than sending twice.
    if(exact)raw=t.walk.disclosed;
    p::u32(b,refined.size());for(Id id:refined){p::u32(b,id);p::u32(b,a.position[id]);dt.insert(a.position[id]);if(!out.delta_offset)out.delta_offset=b.size();b.insert(b.end(),c.delta(id),c.delta(id)+c.dim/4);}
    p::u32(b,raw.size());for(Id id:raw){p::u32(b,id);p::u32(b,a.position[id]);raw_targets.insert(a.position[id]);if(!out.raw_offset)out.raw_offset=b.size();for(int j=0;j<c.dim;++j)p::f32(b,h.vector(id)[j]);}
    out.refined=refined.size();out.raw=raw.size();out.records=b.size();size_t before=b.size();o::proof_write(b,*a.route,rt);out.route_proof=b.size()-before;o::proof_write(b,*a.delta,dt);o::proof_write(b,*a.raw,raw_targets);out.proofs=b.size()-before;
    p::u32(packet.wire,exact?0:1);packet.wire.insert(packet.wire.end(),b.begin(),b.end());return packet;
}
struct Verification{bool valid=false,auth_passed=false;double auth_ms=0,replay_ms=0,result_ms=0;Trace trace;};
Verification verify(const Bytes&wire,const o::Anchor&a,const o::Codec&c,const float*q,int E,bool exact){
    Verification out;try{
        auto started=p::Clock::now();p::Reader r{wire};p::require(r.u32()==Id(exact?0:1),"routing-mode mismatch");auto answer=p::read_header(r,q,a.dim,10,E);
        std::map<Id,Bytes>codes,deltas;std::map<Id,std::vector<float>>raw;std::map<Key,std::vector<Id>>links;std::map<Id,Hash>rl,dl,vl;
        auto id_read=[&](){Id id=r.u32();p::require(id<a.n,"invalid object id");return id;};
        auto blob=[&](size_t n){r.need(n);Bytes b(wire.begin()+r.p,wire.begin()+r.p+n);r.p+=n;return b;};
        Id count=r.count(4+a.dim/2);for(Id i=0;i<count;++i){Id id=id_read();p::require(codes.emplace(id,blob(a.dim/2)).second,"duplicate SQ4");}
        p::require(codes.at(a.entry)==a.entry_sq,"changed entry code");
        count=r.count(16);for(Id i=0;i<count;++i){Id id=id_read(),level=r.u32(),pos=r.u32();p::require(level<=a.maxlevel&&pos<a.route_count,"invalid route");Id degree=r.count(4);std::vector<Id>ns;for(Id j=0;j<degree;++j)ns.push_back(id_read());
            p::require(std::is_sorted(ns.begin(),ns.end())&&std::adjacent_find(ns.begin(),ns.end())==ns.end(),"noncanonical adjacency");p::require(links.emplace(Key{id,level},ns).second,"duplicate adjacency");
            auto hash=o::expansion_leaf(a.snapshot,id,level,ns,a.dim,[&](Id nb){return codes.at(nb).data();});p::require(rl.emplace(pos,hash).second,"duplicate route position");}
        count=r.count(8+a.dim/4);for(Id i=0;i<count;++i){Id id=id_read(),pos=r.u32();auto b=blob(a.dim/4);p::require(codes.count(id)&&deltas.emplace(id,b).second,"invalid delta");p::require(pos<a.n&&dl.emplace(pos,o::refinement_leaf("record/delta46/v1",id,b.data(),b.size())).second,"invalid delta position");}
        count=r.count(8+a.dim*4);for(Id i=0;i<count;++i){Id id=id_read(),pos=r.u32();std::vector<float>v;Bytes b;for(int j=0;j<a.dim;++j){float x=r.f32();v.push_back(x);p::f32(b,x);}
            p::require(codes.count(id)&&(exact||deltas.count(id))&&raw.emplace(id,v).second,"invalid raw record");p::require(pos<a.n&&vl.emplace(pos,o::refinement_leaf("record/raw/v1",id,b.data(),b.size())).second,"invalid raw position");}
        p::require(o::check_proof(r,a.route,a.route_count,rl),"route proof");p::require(o::check_proof(r,a.delta,a.n,dl),"delta proof");p::require(o::check_proof(r,a.raw,a.n,vl),"raw proof");r.end();out.auth_passed=true;out.auth_ms=p::ms(started);started=p::Clock::now();
        if(exact){p::require(raw.size()==codes.size(),"Exact needs every scored raw vector");for(auto&[id,code]:codes)p::require(raw.count(id),"missing Exact routing vector");}
        auto adjacent=[&](Id id,Id level){out.trace.order.emplace_back(id,level);return links.at({id,level});};
        if(exact)out.trace.walk=exact_walk(E,a.entry,a.maxlevel,[&](Id id){return vann::squared_l2(q,raw.at(id).data(),a.dim);},adjacent,&out.trace.exact_scores);
        else out.trace.walk=o::walk(c,q,E,a.entry,a.maxlevel,[&](Id id){return codes.at(id).data();},adjacent);
        p::require(out.trace.walk.disclosed.size()==codes.size()&&out.trace.walk.opened.size()==links.size(),"routing transcript mismatch");for(Id id:out.trace.walk.disclosed)p::require(codes.count(id),"missing candidate");out.replay_ms=p::ms(started);started=p::Clock::now();
        // Identical exact top-k and SQ4->SQ6->raw result certificate rules.
        // Exact's additional raw records are justified solely by routing.
        c.prepare(q);std::set<Id>unique;std::vector<std::pair<float,Id>>best;for(Id id:answer)p::require(unique.insert(id).second&&raw.count(id),"invalid answer");
        for(auto&[id,v]:raw)best.emplace_back(exact?out.trace.exact_scores.at(id):vann::squared_l2(q,v.data(),a.dim),id);std::sort(best.begin(),best.end());p::require(best.size()>=10,"insufficient final candidates");for(int j=0;j<10;++j)p::require(best[j].second==answer[j],"final top-k mismatch");float tau=best[9].first;
        std::set<Id>used_delta,used_raw;for(Id id:out.trace.walk.disclosed){auto s=codes.at(id).data();if(c.lower(q,s)>tau)continue;used_delta.insert(id);auto d=deltas.at(id).data();if(c.lower(q,s,d)>tau&&!unique.count(id))continue;used_raw.insert(id);p::require(raw.count(id),"missing final raw refinement");}
        p::require(used_delta.size()==deltas.size()&&(exact?raw.size()==codes.size():used_raw.size()==raw.size()),"unjustified refinement records");out.result_ms=p::ms(started);out.valid=true;
    }catch(const std::exception&){}return out;
}
}

int main(int argc,char**argv)try{
    namespace p=paper_repro;namespace o=optimized;using namespace rp;
    std::string phase="pilot";int maxE=256,matchExact=1,matchSQ4=1;std::vector<char*>args{argv[0]};
    for(int i=1;i<argc;++i){std::string key=argv[i];p::require(i+1<argc,"missing arg");if(key=="--phase")phase=argv[++i];else if(key=="--max-E")maxE=std::stoi(argv[++i]);else if(key=="--matched-exact")matchExact=std::stoi(argv[++i]);else if(key=="--matched-sq4")matchSQ4=std::stoi(argv[++i]);else{args.push_back(argv[i]);args.push_back(argv[++i]);}}
    auto c=p::args(args.size(),args.data());p::keccak=false;omp_set_num_threads(1);p::require(c.ks==std::vector<int>{10}&&c.efs.size()==1&&c.queries==20&&c.repeats==3&&c.queryoffset==100,"require k10,20x3,query100..119,one fixed E");int fixed=c.efs.front();
    auto base=vann::read_fvecs(c.dataset+"/sift_base.fvecs",c.base),queries=p::read_queries(c);auto data=p::data_hash(base);auto truth=p::ground_truth(base,queries,10);
    auto path=c.cache+"/"+p::hex(data).substr(0,16)+"_M"+std::to_string(c.M)+"_efc"+std::to_string(c.efc)+".index";p::require(std::filesystem::exists(path),"original graph required");auto h=vann::build_hnsw(base,c.M,c.efc,path);base.values.clear();base.values.shrink_to_fit();o::Codec codec(h);o::Codec client(codec,true);
    std::ofstream gt(c.output+"/ground_truth.csv");gt<<"query,answer\n";for(int qi=0;qi<20;++qi){gt<<100+qi<<',';for(Id id:truth[qi])gt<<id<<' ';gt<<'\n';}gt.close();
    auto hits=[&](const std::vector<Id>&a,int qi){int n=0;for(Id id:a)n+=std::find(truth[qi].begin(),truth[qi].end(),id)!=truth[qi].end();return n;};
    if(phase=="pilot"){
        std::ofstream f(c.output+"/calibration.csv");f<<"routing,E,query,hits,recall,candidates,expanded,trace_sha256,answer\n"<<std::setprecision(17);
        for(int qi=0;qi<20;++qi){auto q=queries.row(qi);for(int E=1;E<=maxE;++E)for(bool exact:{true,false}){auto t=route(h,codec,q,E,exact);float tau=0;std::vector<Id>a;if(t.walk.disclosed.size()>=10)a=answer(h,t,q,tau);f<<(exact?"Exact":"SQ4")<<','<<E<<','<<100+qi<<','<<hits(a,qi)<<','<<hits(a,qi)/10.0<<','<<t.walk.disclosed.size()<<','<<t.walk.expanded.size()<<','<<p::hex(fingerprint(t))<<',';for(Id id:a)f<<id<<' ';f<<'\n';}f.flush();std::cout<<"PILOT_QUERY_COMPLETE "<<100+qi<<std::endl;}
        std::ofstream(c.output+"/PASS.txt")<<"Dense exact/SQ4 E calibration; search only.\n";return 0;
    }
    p::require(phase=="formal","unknown phase");o::ADS ads(h,codec,data);
    std::ofstream anchor(c.output+"/anchor.json");anchor<<std::setprecision(17)<<"{\"N\":"<<h.size()<<",\"D\":"<<h.dimension()<<",\"entry\":"<<ads.anchor.entry<<",\"maxlevel\":"<<ads.anchor.maxlevel<<",\"route_count\":"<<ads.anchor.route_count<<",\"snapshot\":\""<<p::hex(data)<<"\",\"route\":\""<<p::hex(ads.anchor.route)<<"\",\"delta\":\""<<p::hex(ads.anchor.delta)<<"\",\"raw\":\""<<p::hex(ads.anchor.raw)<<"\",\"lo\":[";for(int j=0;j<codec.dim;++j)anchor<<(j?",":"")<<codec.lo[j];anchor<<"],\"span\":[";for(int j=0;j<codec.dim;++j)anchor<<(j?",":"")<<codec.span[j];anchor<<"]}";anchor.close();
    std::ofstream f(c.output+"/samples.csv"),steps(c.output+"/expansions.csv"),tests(c.output+"/tests.csv");
    f<<"setting,routing,E,query,repeat,hits,recall,candidates,expanded,opened,vo_bytes,verify_ms,auth_ms,replay_ms,result_ms,query_ms,refined_records,raw_records,final_raw_records,routing_only_raw_records,proof_bytes,trace_sha256,client_trace_sha256,answer,valid\n"<<std::setprecision(17);
    steps<<"setting,routing,E,query,repeat,step,node,level,distance\n"<<std::setprecision(17);tests<<"setting,routing,test,rejected,auth_passed\n";
    int legacy=0,negatives=0,semantic=0;std::set<int>witness_settings;
    for(int repeat=0;repeat<3;++repeat)for(int qi=0;qi<20;++qi)for(int setting=0;setting<2;++setting)for(int slot=0;slot<2;++slot){
        bool exact=((slot+qi+repeat+setting)%2)==0;int E=setting?(exact?matchExact:matchSQ4):fixed;std::string label=setting?"Matched R":"Fixed E";auto q=queries.row(qi);codec.last_query.clear();auto start=p::Clock::now();auto t=route(h,codec,q,E,exact);float tau;auto a=answer(h,t,q,tau);auto packet=generate(h,codec,ads,t,a,tau,q,E,exact);double query_ms=p::ms(start);client.last_query.clear();start=p::Clock::now();auto verified=verify(packet.wire,ads.anchor,client,q,E,exact);double verify_ms=p::ms(start);p::require(verified.valid,"honest VO rejected");auto serverHash=fingerprint(t),clientHash=fingerprint(verified.trace);p::require(serverHash==clientHash,"client expansion decisions differ from server");
        p::require(!exact||packet.body.raw==t.walk.disclosed.size(),"Exact raw coverage");
        f<<label<<','<<(exact?"Exact":"SQ4")<<','<<E<<','<<100+qi<<','<<repeat<<','<<hits(a,qi)<<','<<hits(a,qi)/10.0<<','<<t.walk.disclosed.size()<<','<<t.walk.expanded.size()<<','<<t.walk.opened.size()<<','<<packet.wire.size()<<','<<verify_ms<<','<<verified.auth_ms<<','<<verified.replay_ms<<','<<verified.result_ms<<','<<query_ms<<','<<packet.body.refined<<','<<packet.body.raw<<','<<packet.final_raw<<','<<packet.body.raw-packet.final_raw<<','<<packet.body.proofs<<','<<p::hex(serverHash)<<','<<p::hex(clientHash)<<',';for(Id id:a)f<<id<<' ';f<<",1\n";f.flush();
        for(size_t j=0;j<t.order.size();++j){auto[id,l]=t.order[j];float score=exact?vann::squared_l2(q,h.vector(id),h.dimension()):codec.score(q,codec.sq(id));steps<<label<<','<<(exact?"Exact":"SQ4")<<','<<E<<','<<100+qi<<','<<repeat<<','<<j<<','<<id<<','<<l<<','<<score<<'\n';}
        if(!exact){auto old=o::generate(h,codec,ads,t.walk,a,tau,q,10,E);p::require(old.wire==packet.body.wire,"SQ4 VO differs from original QVANN");p::require(o::verify(old.wire,ads.anchor,client,q,10,E),"original SQ4 verifier rejected");++legacy;}
        if(qi==0&&repeat==0){
            auto tag=std::to_string(setting)+"-"+(exact?"Exact":"SQ4");p::save(c.output+"/"+tag+".vo",packet.wire);
            auto reject=[&](std::string name,Bytes b,const o::Anchor&anchor,const float*qq,int budget,bool mode){auto result=verify(b,anchor,client,qq,budget,mode);tests<<label<<','<<(exact?"Exact":"SQ4")<<','<<name<<','<<!result.valid<<','<<result.auth_passed<<'\n';p::require(!result.valid,"tamper accepted: "+name);++negatives;};
            for(auto[name,pos]:std::vector<std::pair<std::string,size_t>>{{"routing_tag",0},{"query_binding",8},{"answer",52},{"SQ4",4+packet.body.sq_offset},{"delta",4+packet.body.delta_offset},{"raw_vector",4+packet.body.raw_offset},{"adjacency",4+packet.body.adj_offset},{"proof",packet.wire.size()-1}}){auto b=packet.wire;b.at(pos)^=1;reject(name,b,ads.anchor,q,E,exact);}
            auto b=packet.wire;b.pop_back();reject("truncated",b,ads.anchor,q,E,exact);b=packet.wire;b.push_back(0);reject("appended",b,ads.anchor,q,E,exact);auto changed=ads.anchor;changed.raw[0]^=1;reject("wrong_raw_root",packet.wire,changed,q,E,exact);changed=ads.anchor;changed.route[0]^=1;reject("wrong_route_root",packet.wire,changed,q,E,exact);std::vector<float>qq(q,q+codec.dim);qq[0]+=1;reject("wrong_query",packet.wire,ads.anchor,qq.data(),E,exact);reject("wrong_E",packet.wire,ads.anchor,q,E+1,exact);
        }
        // An authenticated SQ4 traversal with ALL raw vectors is NOT an Exact
        // routing proof. Require an auth-passing/replay-failing witness per setting.
        if(exact&&repeat==0&&!witness_settings.count(setting)){
            auto sq=route(h,codec,q,E,false);
            if(sq.walk.opened!=t.walk.opened||sq.walk.disclosed!=t.walk.disclosed){float stau;auto sa=answer(h,sq,q,stau);auto bad=generate(h,codec,ads,sq,sa,stau,q,E,true);auto result=verify(bad.wire,ads.anchor,client,q,E,true);
                p::require(result.auth_passed&&!result.valid,"Exact accepted an authenticated SQ4 decision transcript");tests<<label<<",Exact,authenticated_SQ4_trace_is_not_Exact,1,1\n";++semantic;witness_settings.insert(setting);p::save(c.output+"/semantic-witness-"+std::to_string(setting)+".vo",bad.wire);
            }
        }
        if(qi==19&&slot==1&&setting==1)std::cout<<"FORMAL_REPEAT_COMPLETE "<<repeat<<std::endl;
    }
    p::require(legacy==120&&semantic==2,"missing legacy or semantic verification witnesses");
    std::ofstream(c.output+"/identity.json")<<"{\"N\":"<<h.size()<<",\"D\":"<<h.dimension()<<",\"dataset_sha256\":\""<<p::hex(data)<<"\",\"legacy_checks\":"<<legacy<<",\"negative_tests\":"<<negatives<<",\"semantic_witnesses\":"<<semantic<<",\"samples\":240,\"threads\":1}";
    std::ofstream(c.output+"/PASS.txt")<<"Actual routing precision, common ADS, deduplicated raw coverage, exact client replay, legacy equivalence and negative tests passed.\n";return 0;
}catch(const std::exception&e){std::cerr<<"ERROR "<<e.what()<<std::endl;return 1;}

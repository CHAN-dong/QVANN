// Isolated authentication experiment: generated includes retain native SQ4/refinement.
#include "granularity_original_include.cpp"
#include <numeric>
namespace granularity {
namespace p=paper_repro;namespace o=optimized;using namespace optimized;
const char* names[]={"Record-wise","Node-wise","ER"};
Hash node_leaf(Hash snapshot,Id id,Id base,const std::vector<std::vector<Id>>&layers,bool records){
    Bytes b=domain_bytes(records?"granularity/node-meta/v1":"granularity/node-adjacency/v1");
    p::digest(b,snapshot);p::u32(b,id);if(records)p::u32(b,base);p::u32(b,layers.size());
    for(auto&ns:layers){p::u32(b,ns.size());if(!records)for(Id nb:ns)p::u32(b,nb);}return p::hash(b);
}
Hash edge_leaf(Hash snapshot,Id id,Id level,Id slot,Id nb){Bytes b=domain_bytes("granularity/directed-edge/v1");p::digest(b,snapshot);for(Id x:{id,level,slot,nb})p::u32(b,x);return p::hash(b);}
Hash code_leaf(Hash snapshot,Id id,const uint8_t*s,int d){Bytes b=domain_bytes("granularity/sq4/v1");p::digest(b,snapshot);p::u32(b,id);b.insert(b.end(),s,s+d/2);return p::hash(b);}
struct Client:o::Anchor {Id mode=0,edge_count=0;Hash nodes{},edges{},codes{};};
struct Store {
    const vann::HnswIndex&h;const Codec&codec;const ADS&ads;std::vector<Id>edge_base;
    std::unique_ptr<p::Tree>meta,node,edges,codes;
    std::vector<std::vector<Id>> layers(Id id)const {std::vector<std::vector<Id>>v;for(int l=0;l<h.index->hnsw.levels[id];++l)v.push_back(neighbors(h,id,l));return v;}
    Store(const vann::HnswIndex&h_,const Codec&c,const ADS&a):h(h_),codec(c),ads(a){
        edge_base.resize(h.size()+1);for(Id id=0;id<h.size();++id){uint64_t next=edge_base[id];for(auto&ns:layers(id))next+=ns.size();p::require(next<UINT32_MAX,"edge tree exceeds uint32 leaf positions");edge_base[id+1]=next;}
        std::vector<Hash>m(h.size()),n(h.size()),s(h.size());
        for(Id id=0;id<h.size();++id){auto ls=layers(id);m[id]=node_leaf(a.anchor.snapshot,id,edge_base[id],ls,true);n[id]=node_leaf(a.anchor.snapshot,id,0,ls,false);s[id]=code_leaf(a.anchor.snapshot,id,c.sq(id),c.dim);}
        meta=std::make_unique<p::Tree>(std::move(m));node=std::make_unique<p::Tree>(std::move(n));codes=std::make_unique<p::Tree>(std::move(s));
        std::vector<Hash>e(edge_base.back());for(Id id=0;id<h.size();++id){Id pos=edge_base[id];auto ls=layers(id);for(Id l=0;l<ls.size();++l)for(Id j=0;j<ls[l].size();++j)e[pos++]=edge_leaf(a.anchor.snapshot,id,l,j,ls[l][j]);}
        edges=std::make_unique<p::Tree>(std::move(e));std::cout<<"GRANULARITY_TREES_READY edges="<<edge_base.back()<<std::endl;
    }
    Client client(Id mode)const {Client c;static_cast<o::Anchor&>(c)=ads.anchor;c.mode=mode;c.edge_count=edge_base.back();c.nodes=(mode==0?meta:node)->root();c.edges=edges->root();c.codes=codes->root();return c;}
};
struct Packet:o::Packet {size_t routing_start=0,routing_end=0,route_payload=0,targets=0,siblings=0;std::set<Id>nt,et,ct;};
void write_routing(Packet&out,const Store&s,const Walk&t,Id mode,std::set<Id>&route_targets){
    auto&b=out.wire;
    if(mode==2){p::u32(b,t.opened.size());for(auto[id,l]:t.opened){Id pos=s.ads.route_base[id]+l;route_targets.insert(pos);p::u32(b,id);p::u32(b,l);p::u32(b,pos);auto ns=neighbors(s.h,id,l);p::u32(b,ns.size());for(Id nb:ns){if(!out.adj_offset)out.adj_offset=b.size();p::u32(b,nb);}}}
    else {
        p::u32(b,t.opened.size());for(auto[id,l]:t.opened){p::u32(b,id);p::u32(b,l);out.nt.insert(id);}
        p::u32(b,out.nt.size());for(Id id:out.nt){p::u32(b,id);auto ls=s.layers(id);if(mode==0)p::u32(b,s.edge_base[id]);p::u32(b,ls.size());for(auto&ns:ls){p::u32(b,ns.size());if(mode==1)for(Id nb:ns){if(!out.adj_offset)out.adj_offset=b.size();p::u32(b,nb);}}}
        if(mode==0)for(auto[id,l]:t.opened){auto ls=s.layers(id);Id pos=s.edge_base[id];for(Id k=0;k<l;++k)pos+=ls[k].size();for(Id nb:ls[l]){out.et.insert(pos++);if(!out.adj_offset)out.adj_offset=b.size();p::u32(b,nb);}}
        out.ct.insert(t.disclosed.begin(),t.disclosed.end());
    }
    out.routing_end=b.size();out.route_payload=out.routing_end-out.routing_start;
}
void write_route_proofs(Packet&out,const Store&s,Id mode,const std::set<Id>&targets){
    auto write=[&](const p::Tree&tree,const std::set<Id>&ts){auto proof=tree.prove(ts);out.targets+=ts.size();out.siblings+=proof.size();p::u32(out.wire,proof.size());for(auto x:proof)p::digest(out.wire,x.value);};
    if(mode==2)write(*s.ads.route,targets);else{write(mode==0?*s.meta:*s.node,out.nt);if(mode==0)write(*s.edges,out.et);write(*s.codes,out.ct);}
}
struct RoutingRead {std::map<Id,Hash>nodes,edges,codes;};
RoutingRead read_routing(p::Reader&r,const Client&a,const std::map<Id,Bytes>&codes,std::map<Key,std::vector<Id>>&links,std::map<Id,Hash>&er){
    RoutingRead result;auto id=[&](){Id v=r.u32();p::require(v<a.n,"invalid node id");return v;};
    if(a.mode==2){Id count=r.count(16);for(Id i=0;i<count;++i){Id v=id(),l=r.u32(),pos=r.u32();p::require(l<=a.maxlevel&&pos<a.route_count,"invalid ER position/layer");Id degree=r.count(4);std::vector<Id>ns;for(Id j=0;j<degree;++j)ns.push_back(id());p::require(std::is_sorted(ns.begin(),ns.end())&&std::adjacent_find(ns.begin(),ns.end())==ns.end(),"noncanonical ER adjacency");p::require(links.emplace(Key{v,l},ns).second,"duplicate ER");p::require(er.emplace(pos,expansion_leaf(a.snapshot,v,l,ns,a.dim,[&](Id nb){return codes.at(nb).data();})).second,"duplicate ER position");}return result;}
    std::vector<Key>opened;std::set<Key>unique;std::set<Id>needed;Id count=r.count(8);
    for(Id i=0;i<count;++i){Id v=id(),l=r.u32();p::require(l<=a.maxlevel&&unique.emplace(v,l).second,"duplicate/invalid opened key");opened.push_back({v,l});needed.insert(v);}
    p::require(std::is_sorted(opened.begin(),opened.end()),"noncanonical opened keys");
    std::map<Id,std::vector<std::vector<Id>>>all;std::map<Id,Id>bases;count=r.count(8);p::require(count==needed.size(),"node records not exact expansion set");
    for(Id i=0;i<count;++i){Id v=id();p::require(needed.count(v)&&!all.count(v),"extra/duplicate node record");Id base=a.mode==0?r.u32():0;Id layers=r.count(4);p::require(layers>0&&layers<=a.maxlevel+1,"invalid layers");std::vector<std::vector<Id>>ls;
        for(Id l=0;l<layers;++l){Id degree=r.u32();p::require(degree<=a.n,"invalid degree");std::vector<Id>ns;if(a.mode==0)ns.resize(degree);else{r.need(size_t(degree)*4);for(Id j=0;j<degree;++j)ns.push_back(id());p::require(std::is_sorted(ns.begin(),ns.end())&&std::adjacent_find(ns.begin(),ns.end())==ns.end(),"noncanonical node adjacency");}ls.push_back(std::move(ns));}
        result.nodes[v]=node_leaf(a.snapshot,v,base,ls,a.mode==0);all.emplace(v,std::move(ls));bases[v]=base;
    }
    for(auto[v,l]:opened){auto&ls=all.at(v);p::require(l<ls.size(),"opened nonexisting layer");std::vector<Id>ns;
        if(a.mode==0){uint64_t pos=bases.at(v);for(Id j=0;j<l;++j)pos+=ls[j].size();r.need(ls[l].size()*4);for(Id j=0;j<ls[l].size();++j){Id nb=id();p::require(pos<a.edge_count&&result.edges.emplace(Id(pos++),edge_leaf(a.snapshot,v,l,j,nb)).second,"invalid/duplicate edge position");ns.push_back(nb);}p::require(std::is_sorted(ns.begin(),ns.end())&&std::adjacent_find(ns.begin(),ns.end())==ns.end(),"noncanonical edge list");}
        else ns=ls[l];links.emplace(Key{v,l},std::move(ns));
    }
    for(auto&[v,b]:codes)result.codes[v]=code_leaf(a.snapshot,v,b.data(),a.dim);return result;
}
bool check_routing(p::Reader&r,const Client&a,const RoutingRead&rr,const std::map<Id,Hash>&er){
    if(a.mode==2)return check_proof(r,a.route,a.route_count,er);
    if(!check_proof(r,a.nodes,a.n,rr.nodes))return false;
    if(a.mode==0&&!check_proof(r,a.edges,a.edge_count,rr.edges))return false;
    return check_proof(r,a.codes,a.n,rr.codes);
}
#include "granularity_generator.cpp"
#include "granularity_verifier.cpp"
Hash fingerprint(const Walk&t){Bytes b;for(auto[id,l]:t.opened){p::u32(b,id);p::u32(b,l);}for(Id id:t.disclosed)p::u32(b,id);return p::hash(b);}
Hash refinement_fingerprint(const Packet&p){Bytes b(p.wire.begin()+p.routing_end,p.wire.begin()+p.records);b.insert(b.end(),p.wire.begin()+p.records+p.route_proof,p.wire.end());return p::hash(b);}
void attacks(const Packet&packet,const Client&a,const Codec&codec,const float*q,int E,std::ofstream&tests){
    auto test=[&](const char*label,Bytes b,const Client&anchor,const float*query,int budget){bool rejected=!verify_granularity(b,anchor,codec,query,10,budget);tests<<names[a.mode]<<','<<label<<','<<rejected<<'\n';p::require(rejected,std::string("tamper accepted: ")+label);};
    for(auto[label,pos]:std::vector<std::pair<const char*,size_t>>{{"mode",0},{"header",4},{"query_binding",8},{"answer",52},{"SQ4",packet.sq_offset},{"adjacency",packet.adj_offset},{"refinement",packet.delta_offset},{"raw",packet.raw_offset},{"routing_metadata",packet.routing_start+packet.routing_end>0?packet.routing_end-1:0},{"proof",packet.records}}){auto b=packet.wire;p::require(pos<b.size(),"missing tamper position");b[pos]^=1;test(label,b,a,q,E);}
    auto b=packet.wire;b.pop_back();test("truncated",b,a,q,E);b=packet.wire;b.push_back(0);test("appended",b,a,q,E);auto changed=a;if(a.mode==2)changed.route[0]^=1;else changed.nodes[0]^=1;test("wrong_root",packet.wire,changed,q,E);std::vector<float>other(q,q+codec.dim);other[0]+=1;test("wrong_query",packet.wire,a,other.data(),E);test("wrong_budget",packet.wire,a,q,E+1);
}
}
int main(int argc,char**argv)try{
    using namespace granularity;std::string phase="all";std::vector<char*>av{argv[0]};for(int i=1;i<argc;++i){p::require(i+1<argc,"missing option");if(std::string(argv[i])=="--phase")phase=argv[++i];else{av.push_back(argv[i]);av.push_back(argv[++i]);}}
    auto c=p::args(av.size(),av.data());p::require(c.ks==std::vector<int>{10},"requires k10");p::keccak=false;
    auto base=vann::read_fvecs(c.dataset+"/sift_base.fvecs",c.base);auto queries=p::read_queries(c);auto data=p::data_hash(base);auto path=c.cache+"/"+p::hex(data).substr(0,16)+"_M"+std::to_string(c.M)+"_efc"+std::to_string(c.efc)+".index";
    omp_set_num_threads(c.buildthreads);auto h=vann::build_hnsw(base,c.M,c.efc,path);omp_set_num_threads(1);std::cout<<"GRAPH_READY M="<<c.M<<std::endl;
    auto truth=p::ground_truth(base,queries,10);base.values.clear();base.values.shrink_to_fit();Codec codec(h),client(codec,true);
    auto search=[&](int qi,int E){return walk(codec,queries.row(qi),E,h.index->hnsw.entry_point,h.index->hnsw.max_level,[&](Id id){return codec.sq(id);},[&](Id id,Id l){return neighbors(h,id,l);});};
    auto rank=[&](const Walk&t,int qi,float&tau){std::vector<std::pair<float,Id>>v;for(Id id:t.disclosed)v.emplace_back(vann::squared_l2(queries.row(qi),h.vector(id),h.dimension()),id);std::vector<Id>a;if(v.size()<10)return a;std::partial_sort(v.begin(),v.begin()+10,v.end());tau=v[9].first;for(int j=0;j<10;++j)a.push_back(v[j].second);return a;};
    auto hits=[&](const std::vector<Id>&a,int qi){int n=0;for(Id id:a)n+=std::find(truth[qi].begin(),truth[qi].end(),id)!=truth[qi].end();return n;};
    std::ofstream cal(c.output+"/calibration.csv");cal<<"E,recall,candidates,valid\n"<<std::setprecision(17);std::map<int,std::pair<double,double>>points;
    auto evaluate=[&](int E){double r=0,C=0;bool valid=true;for(int qi=0;qi<c.queries;++qi){auto t=search(qi,E);float tau;auto a=rank(t,qi,tau);r+=hits(a,qi)/10.0;C+=t.disclosed.size();valid&=a.size()==10;}r/=c.queries;C/=c.queries;cal<<E<<','<<r<<','<<C<<','<<valid<<'\n';cal.flush();if(valid)points[E]={r,C};return valid?r:-1.;};
    int below=1,above=2048;for(int E:{4,8,16,24,32,48,64,96,128,192,256,384,512,768,1024,1536,2048}){if(evaluate(E)>=.95-1e-12){above=E;break;}below=E;}
    for(int E=below+1;E<above;++E)if(!points.count(E))evaluate(E);
    p::require(!points.empty(),"no valid calibration point");auto chosen=points.begin();for(auto it=points.begin();it!=points.end();++it){auto key=[](auto x){return std::make_tuple(std::round(std::abs(x.second.first-.95)*1e12),x.second.first<.95-1e-12,x.first);};if(key(*it)<key(*chosen))chosen=it;}
    int E=chosen->first;std::ofstream(c.output+"/selection.json")<<std::setprecision(17)<<"{\"E\":"<<E<<",\"recall\":"<<chosen->second.first<<",\"M\":"<<c.M<<",\"efConstruction\":"<<c.efc<<",\"graph\":\""<<path<<"\",\"dataset_sha256\":\""<<p::hex(data)<<"\"}";std::cout<<"BUDGET_SELECTED E="<<E<<" recall="<<chosen->second.first<<std::endl;if(phase=="pilot")return 0;
    ADS ads(h,codec,data);Store store(h,codec,ads);std::ofstream f(c.output+"/samples.csv"),tests(c.output+"/tests.csv"),anchors(c.output+"/anchors.csv");
    tests<<"variant,test,rejected\n";anchors<<"variant,n,dim,entry,maxlevel,route_count,edge_count,snapshot,route,node,edges,codes,delta,raw\n";for(Id mode=0;mode<3;++mode){auto a=store.client(mode);anchors<<names[mode]<<','<<a.n<<','<<a.dim<<','<<a.entry<<','<<a.maxlevel<<','<<a.route_count<<','<<a.edge_count<<','<<p::hex(a.snapshot)<<','<<p::hex(a.route)<<','<<p::hex(a.nodes)<<','<<p::hex(a.edges)<<','<<p::hex(a.codes)<<','<<p::hex(a.delta)<<','<<p::hex(a.raw)<<'\n';}
    f<<"query,repeat,variant,E,recall,candidates,expanded,opened,route_proof_bytes,route_payload_bytes,total_bytes,query_ms,verify_ms,auth_ms,replay_ms,targets,sibling_hashes,refined,raw,trace_sha256,refinement_sha256,answer,valid\n"<<std::setprecision(17);size_t trials=0;
    for(int qi=0;qi<c.queries;++qi){Hash ref{};for(int repeat=-1;repeat<c.repeats;++repeat)for(int slot=0;slot<3;++slot){Id mode=(slot+qi+std::max(repeat,0))%3;auto a=store.client(mode);codec.last_query.clear();auto start=Clock::now();auto t=search(qi,E);float tau=0;auto answer=rank(t,qi,tau);auto packet=generate_granularity(h,codec,ads,t,answer,tau,queries.row(qi),10,E,store,mode);double query_ms=p::ms(start);auto current=refinement_fingerprint(packet);if(repeat==-1&&slot==0)ref=current;else p::require(ref==current,"refinement bytes differ across variants");
        client.last_query.clear();Walk replay;double auth=0,replay_ms=0;start=Clock::now();bool valid=verify_granularity(packet.wire,a,client,queries.row(qi),10,E,&auth,&replay_ms,&replay);double verification=p::ms(start);p::require(valid&&fingerprint(t)==fingerprint(replay),"client rejection or trace mismatch");
        if(mode==2&&repeat==0){auto original=o::generate(h,codec,ads,t,answer,tau,queries.row(qi),10,E);p::require(Bytes(packet.wire.begin()+4,packet.wire.end())==original.wire,"ER changed original protocol");p::require(o::verify(original.wire,ads.anchor,client,queries.row(qi),10,E),"original verifier rejected ER");}
        p::require(packet.route_proof==packet.siblings*32+4*(mode==0?3:mode==1?2:1),"routing proof accounting mismatch");
        if(qi==0&&repeat==0){p::save(c.output+"/"+names[mode]+".vo",packet.wire);attacks(packet,a,client,queries.row(qi),E,tests);}
        if(repeat<0)continue;++trials;f<<qi+c.queryoffset<<','<<repeat<<','<<names[mode]<<','<<E<<','<<hits(answer,qi)/10.0<<','<<t.disclosed.size()<<','<<t.expanded.size()<<','<<t.opened.size()<<','<<packet.route_proof<<','<<packet.route_payload<<','<<packet.wire.size()<<','<<query_ms<<','<<verification<<','<<auth<<','<<replay_ms<<','<<packet.targets<<','<<packet.siblings<<','<<packet.refined<<','<<packet.raw<<','<<p::hex(fingerprint(t))<<','<<p::hex(current)<<',';for(Id id:answer)f<<id<<' ';f<<",1\n";
    }f.flush();std::cout<<"QUERY_COMPLETE "<<qi+c.queryoffset<<std::endl;}
    p::require(trials==size_t(c.queries*c.repeats*3),"missing trials");std::ofstream(c.output+"/PASS.txt")<<"All variants fully verified, exact shared trace/refinement, negative tests rejected.\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}

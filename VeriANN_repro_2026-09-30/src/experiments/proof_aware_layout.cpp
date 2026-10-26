// Isolated routing-leaf permutation experiment; original client verifier unchanged.
#include "layout_original_include.cpp"
#include <random>
#include <numeric>
namespace layout_exp {
namespace p=paper_repro;namespace o=optimized;
using o::Id;using o::Hash;using o::Bytes;
std::vector<Id> read_ids(const std::string&path){std::ifstream f(path);p::require(bool(f),"missing query split");std::vector<Id>v;Id x;while(f>>x)v.push_back(x);return v;}
void binary(const std::string&path,const std::vector<Id>&v){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(v.data()),v.size()*4);}
// Exactly Tree::prove's canonical missing-sibling count, not a proxy distance.
size_t cost(std::vector<Id> v,Id n){std::sort(v.begin(),v.end());Id width=1;while(width<n)width*=2;size_t count=0;
    while(width>1){std::vector<Id>next;next.reserve(v.size());for(size_t i=0;i<v.size();){Id parent=v[i]/2;size_t j=i+1;while(j<v.size()&&v[j]/2==parent)++j;count+=2-(j-i);next.push_back(parent);i=j;}v=std::move(next);width/=2;}return count;}
struct Layout{std::string name;std::vector<Id>order,pos;std::unique_ptr<p::Tree>tree;double ordering_ms=0,tree_ms=0,opt_ms=0;};
void inverse(Layout&l){l.pos.resize(l.order.size());for(Id i=0;i<l.order.size();++i)l.pos[l.order[i]]=i;}
std::vector<Id> positions(const std::vector<Id>&records,const Layout&l){std::vector<Id>v;for(Id id:records)v.push_back(l.pos[id]);return v;}
uint64_t mix(uint64_t x){x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);}
void optimize(Layout&l,const std::vector<std::vector<Id>>&traces,const std::string&out){
    auto start=p::Clock::now();const Id n=l.order.size();std::vector<std::vector<Id>>membership(n);
    for(Id qi=0;qi<traces.size();++qi)for(Id record:traces[qi])membership[record].push_back(qi);
    std::vector<size_t>current;size_t objective=0;for(auto&t:traces){current.push_back(cost(positions(t,l),n));objective+=current.back();}
    std::ofstream history(out+"/optimization.csv");history<<"iteration,block_size,proposals,accepted,before_hashes,after_hashes,elapsed_ms\n"<<std::setprecision(17);
    history<<"0,0,0,0,"<<objective<<','<<objective<<','<<p::ms(start)<<'\n';
    // Four prespecified block sweeps. MinHash of query memberships groups
    // co-expanded records; ONLY an exact positive sibling-count gain is accepted.
    // Inactive records retain relative BFS order. No test query is available here.
    std::vector<Id>blocks{256,2048,16384,2048};
    for(size_t pass=0;pass<blocks.size();++pass){Id B=blocks[pass];size_t before=objective,proposals=0,accepted=0;
        std::vector<uint64_t>signature(n,UINT64_MAX);for(Id id=0;id<n;++id)for(Id qi:membership[id])signature[id]=std::min(signature[id],mix(qi+pass*1000003ULL));
        for(Id begin=0;begin<n;begin+=B){Id end=std::min<Id>(n,begin+B);std::vector<Id>old(l.order.begin()+begin,l.order.begin()+end),proposal=old;
            std::stable_sort(proposal.begin(),proposal.end(),[&](Id a,Id b){if(signature[a]!=signature[b])return signature[a]<signature[b];return membership[a]>membership[b];});
            if(proposal==old)continue;++proposals;std::set<Id>affected;
            for(Id id:old)for(Id qi:membership[id])affected.insert(qi);
            for(Id j=0;j<proposal.size();++j)l.pos[proposal[j]]=begin+j;
            size_t old_cost=0,new_cost=0;std::vector<std::pair<Id,size_t>>updated;
            for(Id qi:affected){size_t c=cost(positions(traces[qi],l),n);updated.push_back({qi,c});new_cost+=c;old_cost+=current[qi];}
            if(new_cost<old_cost){std::copy(proposal.begin(),proposal.end(),l.order.begin()+begin);for(auto[qi,c]:updated)current[qi]=c;objective=objective-old_cost+new_cost;++accepted;}
            else for(Id j=0;j<old.size();++j)l.pos[old[j]]=begin+j;
        }
        history<<pass+1<<','<<B<<','<<proposals<<','<<accepted<<','<<before<<','<<objective<<','<<p::ms(start)<<'\n';history.flush();
        std::cout<<"OPTIMIZATION pass="<<pass+1<<" objective="<<objective<<" accepted="<<accepted<<std::endl;
    }
    l.opt_ms=p::ms(start);
}
// Generated native packet serializer with ONLY routing-position/tree substitution.
using namespace optimized;
#include "layout_packet_generator.cpp"
}
int main(int argc,char**argv)try{
    namespace p=paper_repro;namespace o=optimized;using namespace layout_exp;
    std::string train_file,test_file;std::vector<char*>args{argv[0]};
    for(int i=1;i<argc;++i){std::string key=argv[i];p::require(i+1<argc,"missing argument");if(key=="--train-ids")train_file=argv[++i];else if(key=="--test-ids")test_file=argv[++i];else{args.push_back(argv[i]);args.push_back(argv[++i]);}}
    auto c=p::args(args.size(),args.data());p::keccak=false;omp_set_num_threads(1);p::require(c.ks==std::vector<int>{10}&&c.efs.size()==1&&c.buildthreads==1,"require k10, one E, one thread");int E=c.efs.front();
    auto train=read_ids(train_file),test=read_ids(test_file);std::set<Id>unique(train.begin(),train.end());for(Id qi:test)p::require(unique.insert(qi).second,"query leakage or duplicate");p::require(unique.size()==train.size()+test.size(),"duplicate training query");
    auto base=vann::read_fvecs(c.dataset+"/sift_base.fvecs",c.base),queries=vann::read_fvecs(c.dataset+"/sift_query.fvecs",*unique.rbegin()+1);p::require(base.rows()==size_t(c.base)&&queries.rows()>*unique.rbegin(),"short data");auto snapshot=p::data_hash(base);
    auto graph=c.cache+"/"+p::hex(snapshot).substr(0,16)+"_M"+std::to_string(c.M)+"_efc"+std::to_string(c.efc)+".index";p::require(std::filesystem::exists(graph),"cached original graph missing");
    auto started=p::Clock::now();auto h=vann::build_hnsw(base,c.M,c.efc,graph);double load_ms=p::ms(started);
    started=p::Clock::now();o::Codec codec(h);o::Codec client(codec,true);o::ADS ads(h,codec,snapshot);double common_ms=p::ms(started);Id n=ads.route_base.back();
    auto walk=[&](Id qi){codec.last_query.clear();return o::walk(codec,queries.row(qi),E,ads.anchor.entry,ads.anchor.maxlevel,[&](Id id){return codec.sq(id);},[&](Id id,Id level){return o::neighbors(h,id,level);});};
    std::vector<std::vector<Id>>training;std::ofstream traces(c.output+"/traces.csv");traces<<"split,query,records\n";
    started=p::Clock::now();for(Id qi:train){auto t=walk(qi);std::vector<Id>r;traces<<"train,"<<qi<<',';for(auto[id,level]:t.opened){r.push_back(ads.route_base[id]+level);traces<<r.back()<<' ';}traces<<'\n';training.push_back(std::move(r));}double trace_ms=p::ms(started);
    std::vector<Layout>layouts(4);layouts[0].name="ID";layouts[1].name="Random";layouts[2].name="BFS";layouts[3].name="Proof-Aware";
    for(int li=0;li<3;++li){auto&s=layouts[li];started=p::Clock::now();std::vector<int>order;if(li==2){int components;order=deterministic_bfs_order(h,components);}else {order=node_id_order(h.size());if(li==1){std::mt19937 rng(20261024);std::shuffle(order.begin(),order.end(),rng);}}
        for(Id id:order)for(int level=0;level<h.index->hnsw.levels[id];++level)s.order.push_back(ads.route_base[id]+level);inverse(s);s.ordering_ms=p::ms(started);}
    layouts[3].order=layouts[2].order;inverse(layouts[3]);optimize(layouts[3],training,c.output);
    // Layout freeze occurs BEFORE any test traversal or test ground truth.
    std::ofstream(c.output+"/LAYOUT_FROZEN.txt")<<"Training traces only; no test traversals or ground truth have been computed.\n";
    std::ofstream build(c.output+"/construction.csv"),costs(c.output+"/proof_costs.csv"),anchors(c.output+"/anchors.csv");
    build<<"layout,route_leaves,ordering_ms,optimization_ms,tree_build_ms,route_tree_bytes,mapping_bytes,common_codec_ads_ms,train_trace_ms,graph_load_ms\n"<<std::setprecision(17);costs<<"split,query,layout,targets,sibling_hashes\n";anchors<<"layout,route,delta,raw,snapshot,N,D,entry,maxlevel,route_count\n";
    for(auto&s:layouts){binary(c.output+"/"+s.name+".positions.u32",s.pos);started=p::Clock::now();std::vector<Hash>leaves;leaves.reserve(n);for(Id record:s.order)leaves.push_back(ads.route->levels[0][record]);s.tree=std::make_unique<p::Tree>(std::move(leaves));s.tree_ms=p::ms(started);size_t bytes=0;for(auto&level:s.tree->levels)bytes+=level.size()*sizeof(Hash);
        if(s.name=="ID")p::require(s.tree->root()==ads.anchor.route,"ID changed original root");
        build<<s.name<<','<<n<<','<<s.ordering_ms<<','<<s.opt_ms<<','<<s.tree_ms<<','<<bytes<<','<<(s.name=="ID"?0:s.pos.size()*4)<<','<<common_ms<<','<<trace_ms<<','<<load_ms<<'\n';
        anchors<<s.name<<','<<p::hex(s.tree->root())<<','<<p::hex(ads.anchor.delta)<<','<<p::hex(ads.anchor.raw)<<','<<p::hex(snapshot)<<','<<h.size()<<','<<h.dimension()<<','<<ads.anchor.entry<<','<<ads.anchor.maxlevel<<','<<n<<'\n';
        for(size_t qi=0;qi<training.size();++qi){auto v=positions(training[qi],s);std::set<Id>targets(v.begin(),v.end());auto proof=s.tree->prove(targets);p::require(proof.size()==cost(v,n),"objective is not actual Tree::prove cost");costs<<"train,"<<train[qi]<<','<<s.name<<','<<v.size()<<','<<proof.size()<<'\n';}
        std::cout<<"TREE_READY "<<s.name<<std::endl;
    }build.close();anchors.close();
    vann::Matrix tq;tq.dimension=queries.dimension;for(Id qi:test)tq.values.insert(tq.values.end(),queries.row(qi),queries.row(qi)+queries.dimension);auto truth=p::ground_truth(base,tq,10);base.values.clear();base.values.shrink_to_fit();
    std::ofstream gt(c.output+"/ground_truth.csv");gt<<"query,answer\n";for(size_t i=0;i<test.size();++i){gt<<test[i]<<',';for(Id id:truth[i])gt<<id<<' ';gt<<'\n';}gt.close();
    std::ofstream samples(c.output+"/samples.csv");samples<<"query,repeat,layout,E,hits,recall,candidates,expanded,targets,sibling_hashes,route_proof_bytes,auth_proof_bytes,route_position_bytes,total_bytes,payload_bytes,refined,raw,verify_ms,auth_ms,replay_and_result_ms,valid,trace_sha256,refinement_sha256,answer\n"<<std::setprecision(17);
    size_t checks=0;for(size_t qi=0;qi<test.size();++qi){Id id=test[qi];const float*q=queries.row(id);auto t=walk(id);std::vector<std::pair<float,Id>>exact;for(Id v:t.disclosed)exact.emplace_back(vann::squared_l2(q,h.vector(v),h.dimension()),v);std::partial_sort(exact.begin(),exact.begin()+10,exact.end());std::vector<Id>answer;int hits=0;for(int j=0;j<10;++j){answer.push_back(exact[j].second);hits+=std::find(truth[qi].begin(),truth[qi].end(),answer.back())!=truth[qi].end();}float tau=exact[9].first;
        Bytes trace_bytes;std::vector<Id>records;traces<<"test,"<<id<<',';for(auto[v,l]:t.opened){p::u32(trace_bytes,v);p::u32(trace_bytes,l);records.push_back(ads.route_base[v]+l);traces<<records.back()<<' ';}for(Id v:t.disclosed)p::u32(trace_bytes,v);traces<<'\n';auto trace_hash=p::hex(p::hash(trace_bytes));
        auto native=o::generate(h,codec,ads,t,answer,tau,q,10,E);size_t invariant=native.wire.size()-native.route_proof;Hash refinement{};bool first=true;
        for(int repeat=-1;repeat<c.repeats;++repeat)for(int slot=0;slot<4;++slot){auto&s=layouts[(slot+qi+std::max(0,repeat))%4];auto anchor=ads.anchor;anchor.route=s.tree->root();auto packet=generate_layout(h,codec,ads,t,answer,tau,q,10,E,s.pos,*s.tree);p::require(packet.wire.size()-packet.route_proof==invariant&&packet.records==native.records,"nonrouting byte counts changed");
            auto tail=Bytes(packet.wire.begin()+packet.records+packet.route_proof,packet.wire.end());Hash refhash=p::hash(tail);if(first){refinement=refhash;first=false;}else p::require(refinement==refhash,"refinement proof changed");
            if(s.name=="ID")p::require(packet.wire==native.wire,"ID protocol no longer native byte-identical");
            auto v=positions(records,s);std::set<Id>targets(v.begin(),v.end());size_t sibling=s.tree->prove(targets).size();p::require(packet.route_proof==4+sibling*32,"wrong routing proof accounting");
            client.last_query.clear();double auth_ms=0,replay_ms=0;started=p::Clock::now();bool valid=o::verify(packet.wire,anchor,client,q,10,E,&auth_ms,&replay_ms);double verify_ms=p::ms(started);p::require(valid,"client rejected honest reordered VO");
            if(repeat<0)continue;
            if(repeat==0)costs<<"test,"<<id<<','<<s.name<<','<<targets.size()<<','<<sibling<<'\n';
            samples<<id<<','<<repeat<<','<<s.name<<','<<E<<','<<hits<<','<<hits/10.0<<','<<t.disclosed.size()<<','<<t.expanded.size()<<','<<targets.size()<<','<<sibling<<','<<packet.route_proof<<','<<packet.proofs<<','<<targets.size()*4<<','<<packet.wire.size()<<','<<packet.records<<','<<packet.refined<<','<<packet.raw<<','<<verify_ms<<','<<auth_ms<<','<<replay_ms<<",1,"<<trace_hash<<','<<p::hex(refhash)<<',';for(Id v:answer)samples<<v<<' ';samples<<'\n';++checks;
            if(qi<2&&repeat==0)p::save(c.output+"/sample-"+std::to_string(id)+"-"+s.name+".vo",packet.wire);
            if(qi==0&&repeat==0){auto path=c.output+"/tamper-"+s.name;std::filesystem::create_directories(path);o::attacks(packet,anchor,client,q,10,E,path);auto bad=packet.wire; // Corrupt a routing leaf position, not its authenticated record.
                p::Reader reader{bad};p::read_header(reader,q,codec.dim,10,E);Id count=reader.u32();reader.p+=count*(4+codec.dim/2);reader.u32();bad.at(reader.p+8)^=1;p::require(!o::verify(bad,anchor,client,q,10,E),"wrong leaf position accepted");std::ofstream(path+"/position_tamper.txt")<<"Rejected\n";
            }
        }
        if((qi+1)%20==0){samples.flush();std::cout<<"TEST_QUERIES_COMPLETE "<<qi+1<<'/'<<test.size()<<std::endl;}
    }
    p::require(checks==test.size()*c.repeats*4,"missing trials");std::ofstream(c.output+"/PASS.txt")<<"All "<<checks<<" actual VOs verified; shared traversals, records, answers; native ID byte-equivalence; true multiproof objective; 56 negative tests.\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"ERROR "<<e.what()<<std::endl;return 1;}

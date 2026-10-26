bool verify_granularity(const Bytes&wire,const Client&a,const Codec&codec,const float*q,int k,int E,
            double*auth_ms=nullptr,double*replay_ms=nullptr,Walk*replayed=nullptr){try{
    auto started=Clock::now();p::Reader r{wire};p::require(r.u32()==a.mode,"routing mode mismatch");auto answer=p::read_header(r,q,a.dim,k,E);
    std::map<Id,Bytes>codes,deltas;std::map<Id,std::vector<float>>raw;
    std::map<Key,std::vector<Id>>links;std::map<Id,Hash>route_leaves,delta_leaves,raw_leaves;
    auto id_read=[&](){Id id=r.u32();p::require(id<a.n,"invalid object id");return id;};
    auto blob=[&](size_t size){r.need(size);Bytes b(wire.begin()+r.p,wire.begin()+r.p+size);r.p+=size;return b;};
    Id count=r.count(4+a.dim/2);for(Id i=0;i<count;++i){Id id=id_read();p::require(codes.emplace(id,blob(a.dim/2)).second,"duplicate SQ4");}
    p::require(codes.at(a.entry)==a.entry_sq,"entry code changed");
    auto routing=read_routing(r,a,codes,links,route_leaves);
    count=r.count(8+a.dim/4);for(Id i=0;i<count;++i){Id id=id_read(),pos=r.u32();auto b=blob(a.dim/4);
        p::require(codes.count(id)&&deltas.emplace(id,b).second,"invalid delta record");
        p::require(pos<a.n&&delta_leaves.emplace(pos,refinement_leaf("record/delta46/v1",id,b.data(),b.size())).second,"invalid delta position");}
    count=r.count(8+a.dim*4);for(Id i=0;i<count;++i){Id id=id_read(),pos=r.u32();Bytes b;std::vector<float>v;
        for(int j=0;j<a.dim;++j){float x=r.f32();v.push_back(x);p::f32(b,x);}
        p::require(deltas.count(id)&&raw.emplace(id,v).second,"invalid raw record");
        p::require(pos<a.n&&raw_leaves.emplace(pos,refinement_leaf("record/raw/v1",id,b.data(),b.size())).second,"invalid raw position");}
    p::require(check_routing(r,a,routing,route_leaves),"route authentication failed");
    p::require(check_proof(r,a.delta,a.n,delta_leaves),"delta authentication failed");
    p::require(check_proof(r,a.raw,a.n,raw_leaves),"raw authentication failed");r.end();
    if(auth_ms)*auth_ms=p::ms(started);started=Clock::now();
    auto trace=walk(codec,q,E,a.entry,a.maxlevel,[&](Id id){return codes.at(id).data();},[&](Id id,Id level){return links.at({id,level});});
    if(replayed)*replayed=trace;
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

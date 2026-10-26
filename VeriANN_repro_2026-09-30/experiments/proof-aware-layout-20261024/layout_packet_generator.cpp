Packet generate_layout(const vann::HnswIndex&h,const Codec&codec,const ADS&ads,const Walk&t,
                const std::vector<Id>&answer,float tau,const float*q,int k,int E,const std::vector<Id>&route_position,const p::Tree&routing_tree){
    Packet out;out.answer=answer;auto&b=out.wire;p::write_header(b,q,codec.dim,k,E,answer);
    p::u32(b,t.disclosed.size());for(Id id:t.disclosed){p::u32(b,id);if(!out.sq_offset)out.sq_offset=b.size();auto s=codec.sq(id);b.insert(b.end(),s,s+codec.dim/2);}
    p::u32(b,t.opened.size());std::set<Id>route_targets,delta_targets,raw_targets;
    for(auto[id,l]:t.opened){Id pos=route_position.at(ads.route_base[id]+l);route_targets.insert(pos);
        p::u32(b,id);p::u32(b,l);p::u32(b,pos);auto ns=neighbors(h,id,l);p::u32(b,ns.size());for(Id nb:ns){if(!out.adj_offset)out.adj_offset=b.size();p::u32(b,nb);}}
    std::vector<Id>refined,raw;
    for(Id id:t.disclosed)if(codec.lower(q,codec.sq(id))<=tau){refined.push_back(id);
        if(codec.lower(q,codec.sq(id),codec.delta(id))<=tau||std::find(answer.begin(),answer.end(),id)!=answer.end())raw.push_back(id);}
    p::u32(b,refined.size());for(Id id:refined){p::u32(b,id);p::u32(b,ads.position[id]);delta_targets.insert(ads.position[id]);auto s=codec.delta(id);if(!out.delta_offset)out.delta_offset=b.size();b.insert(b.end(),s,s+codec.dim/4);}
    p::u32(b,raw.size());for(Id id:raw){p::u32(b,id);p::u32(b,ads.position[id]);raw_targets.insert(ads.position[id]);if(!out.raw_offset)out.raw_offset=b.size();for(int j=0;j<codec.dim;++j)p::f32(b,h.vector(id)[j]);}
    out.refined=refined.size();out.raw=raw.size();out.records=b.size();auto before=b.size();
    proof_write(b,routing_tree,route_targets);out.route_proof=b.size()-before;
    proof_write(b,*ads.delta,delta_targets);proof_write(b,*ads.raw,raw_targets);out.proofs=b.size()-before;
    return out;
}


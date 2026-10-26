#define COMPLETE_ROUTING_VO_NO_MAIN
#include "complete_routing_vo.cpp"
#undef COMPLETE_ROUTING_VO_NO_MAIN

// Stateless authenticated three-level residual IVF:
//   fine center = coarse base + middle residual + fine residual.
// The verifier receives every root code, complete middle-code blocks for the
// selected roots, and complete fine-code blocks for the selected middles.

struct TCfg {
    std::string data="../dataset/sift",output="residual_3level_vo.csv";
    std::string coarse_codec="sq4",mid_codec="pq24",fine_codec="pq16";
    int base=100000,train_queries=200,test_queries=100,k=100;
    int fine_nlist=65536,fine_probe=768,mid_nlist=4096,mid_probe=512,coarse_nlist=256,coarse_probe=64;
    int iters=6,repeats=1,pq_train=20000,pq_iters=5;uint64_t seed=20260828;
    bool balanced_groups=false,auto_seeds=false;
};

static TCfg t_args(int argc,char**argv){
    TCfg c;for(int i=1;i<argc;++i){std::string a=argv[i];auto v=[&](){if(++i==argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
        if(a=="--dataset")c.data=v();else if(a=="--output")c.output=v();else if(a=="--coarse-codec")c.coarse_codec=v();
        else if(a=="--mid-codec")c.mid_codec=v();else if(a=="--fine-codec")c.fine_codec=v();else if(a=="--base")c.base=std::stoi(v());
        else if(a=="--train-queries")c.train_queries=std::stoi(v());else if(a=="--test-queries")c.test_queries=std::stoi(v());else if(a=="--k")c.k=std::stoi(v());
        else if(a=="--fine-nlist")c.fine_nlist=std::stoi(v());else if(a=="--fine-probe")c.fine_probe=std::stoi(v());
        else if(a=="--mid-nlist")c.mid_nlist=std::stoi(v());else if(a=="--mid-probe")c.mid_probe=std::stoi(v());
        else if(a=="--coarse-nlist")c.coarse_nlist=std::stoi(v());else if(a=="--coarse-probe")c.coarse_probe=std::stoi(v());
        else if(a=="--iters")c.iters=std::stoi(v());else if(a=="--repeats")c.repeats=std::stoi(v());else if(a=="--pq-train")c.pq_train=std::stoi(v());
        else if(a=="--pq-iters")c.pq_iters=std::stoi(v());else if(a=="--seed")c.seed=std::stoull(v());
        else if(a=="--balanced-groups")c.balanced_groups=true;else if(a=="--auto-seeds")c.auto_seeds=true;
        else throw std::runtime_error("unknown option "+a);}
    if(c.base<c.fine_nlist||c.coarse_nlist>c.mid_nlist||c.mid_nlist>c.fine_nlist||c.coarse_probe<1||c.coarse_probe>c.coarse_nlist||
       c.mid_probe<1||c.mid_probe>c.mid_nlist||c.fine_probe<1||c.fine_probe>c.fine_nlist||c.train_queries<2||c.test_queries<1)
        throw std::runtime_error("invalid configuration");
    return c;
}

struct TState {
    std::vector<VLState>leaf;
    std::vector<std::vector<int>>coarse,mid;
    double mid_candidates=0,fine_candidates=0,recall=0,query_ms=0;
};

static TState t_states(const Matrix&xb,const Matrix&q,const std::vector<std::vector<int>>&lists,
        const std::vector<std::vector<int>>&mid_groups,const std::vector<int>&mid_of_fine,
        const std::vector<std::vector<int>>&coarse_groups,const std::vector<int>&coarse_of_mid,
        const Matrix&coarse_center,const Matrix&mid_residual,const Matrix&fine_residual,
        const CRCodec&coarse_codec,const CRCodec&mid_codec,const CRCodec&fine_codec,
        int coarse_probe,int mid_probe,int fine_probe,int k,const std::vector<std::vector<int>>*gt){
    TState out;out.leaf.resize(q.rows());out.coarse.resize(q.rows());out.mid.resize(q.rows());uint64_t hits=0,mc=0,fc=0;
    auto start=std::chrono::steady_clock::now();
    for(size_t qi=0;qi<q.rows();++qi){
        auto clut=coarse_codec.prepare(q[qi]);std::vector<int>corder(coarse_groups.size());std::iota(corder.begin(),corder.end(),0);
        std::partial_sort(corder.begin(),corder.begin()+coarse_probe,corder.end(),[&](int a,int b){float x=coarse_codec.distance(q[qi],clut,coarse_center,a,false),y=coarse_codec.distance(q[qi],clut,coarse_center,b,false);return x==y?a<b:x<y;});
        out.coarse[qi].assign(corder.begin(),corder.begin()+coarse_probe);
        std::vector<std::vector<float>>decoded_coarse(coarse_groups.size());std::vector<std::pair<float,int>>mcan;
        for(int z=0;z<coarse_probe;++z){int g=corder[z];decoded_coarse[g]=coarse_codec.decode(coarse_center,g);std::vector<float>qr(q.d);
            for(int j=0;j<q.d;++j)qr[j]=q[qi][j]-decoded_coarse[g][j];auto lut=mid_codec.prepare(qr.data());
            for(int m:coarse_groups[g])mcan.push_back({mid_codec.distance(qr.data(),lut,mid_residual,m,false),m});}
        if((int)mcan.size()<mid_probe)throw std::runtime_error("middle shortlist smaller than mid_probe");mc+=mcan.size();
        std::partial_sort(mcan.begin(),mcan.begin()+mid_probe,mcan.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});
        for(int z=0;z<mid_probe;++z)out.mid[qi].push_back(mcan[z].second);
        std::vector<std::pair<float,int>>fcan;for(int m:out.mid[qi]){int g=coarse_of_mid[m];if(decoded_coarse[g].empty())throw std::runtime_error("middle parent was not selected");
            auto dm=mid_codec.decode(mid_residual,m);std::vector<float>qr(q.d);for(int j=0;j<q.d;++j)qr[j]=q[qi][j]-decoded_coarse[g][j]-dm[j];auto lut=fine_codec.prepare(qr.data());
            for(int f:mid_groups[m])fcan.push_back({fine_codec.distance(qr.data(),lut,fine_residual,f,false),f});}
        if((int)fcan.size()<fine_probe)throw std::runtime_error("fine shortlist smaller than fine_probe");fc+=fcan.size();
        std::partial_sort(fcan.begin(),fcan.begin()+fine_probe,fcan.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});
        auto&s=out.leaf[qi];for(int z=0;z<fine_probe;++z){int f=fcan[z].second;if(mid_of_fine[f]<0)throw std::runtime_error("fine leaf has no middle parent");s.lists.push_back(f);}
        std::priority_queue<std::pair<float,int>>top;for(int f:s.lists)for(int id:lists[f]){float d=l2(q[qi],xb[id],xb.d);if((int)top.size()<k)top.push({d,id});else if(d<top.top().first||(d==top.top().first&&id<top.top().second)){top.pop();top.push({d,id});}}
        if((int)top.size()!=k)throw std::runtime_error("fewer than k candidates");s.tau2=top.top().first;while(!top.empty()){s.answer.push_back(top.top().second);top.pop();}
        if(gt){std::unordered_set<int>a(s.answer.begin(),s.answer.end());for(int id:(*gt)[qi])hits+=a.count(id);}
    }
    out.query_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/q.rows();
    out.mid_candidates=mc/(double)q.rows();out.fine_candidates=fc/(double)q.rows();out.recall=gt?hits/(q.rows()*(double)k):0;return out;
}

struct TRoute {
    double parameters=0,coarse_codes=0,mid_codes=0,fine_codes=0;
    double coarse_metadata=0,mid_metadata=0,fine_metadata=0;
    double coarse_proofs=0,mid_proofs=0,fine_proofs=0,total=0;
    double huffman_models=0,huffman_coarse_codes=0,huffman_mid_codes=0,huffman_fine_codes=0,huffman_total=0;
};

struct TLayout {std::vector<int>coarse_position,mid_position,fine_position;};

static std::vector<int> t_best_positions(int n,const std::vector<VLState>&trace,int trials,uint64_t seed){
    std::vector<int>identity(n);std::iota(identity.begin(),identity.end(),0);std::vector<int>best_position=identity;double best=std::numeric_limits<double>::infinity();
    for(int z=-1;z<trials;++z){auto order=z<0?identity:hv_trace_order(n,trace,hv_mix(seed+z));auto pos=hv_positions(order);double cost=0;
        for(const auto&s:trace)if(!s.lists.empty()){std::vector<int>mapped;for(int id:s.lists)mapped.push_back(pos[id]);cost+=hv_dummy_proof_count(n,mapped);}
        if(cost<best){best=cost;best_position=std::move(pos);}}
    return best_position;
}

static TLayout t_layout(const TState&train,const std::vector<std::vector<int>>&mid_groups,const std::vector<int>&mid_of_fine,const std::vector<int>&fine_pos,
        const std::vector<std::vector<int>>&coarse_groups,const std::vector<int>&coarse_of_mid,const std::vector<int>&mid_pos,uint64_t seed){
    TLayout out;std::vector<VLState>coarse_trace(train.leaf.size());for(size_t qi=0;qi<train.leaf.size();++qi)coarse_trace[qi].lists=train.coarse[qi];
    out.coarse_position=t_best_positions(coarse_groups.size(),coarse_trace,16,seed);out.mid_position=mid_pos;out.fine_position=fine_pos;
    for(size_t g=0;g<coarse_groups.size();++g){std::vector<VLState>local(train.leaf.size());for(size_t qi=0;qi<train.leaf.size();++qi)for(int m:train.mid[qi])if(coarse_of_mid[m]==(int)g)local[qi].lists.push_back(mid_pos[m]);
        auto pos=t_best_positions(coarse_groups[g].size(),local,4,seed+17*g);for(int m:coarse_groups[g])out.mid_position[m]=pos[mid_pos[m]];}
    for(size_t m=0;m<mid_groups.size();++m){std::vector<VLState>local(train.leaf.size());for(size_t qi=0;qi<train.leaf.size();++qi)for(int f:train.leaf[qi].lists)if(mid_of_fine[f]==(int)m)local[qi].lists.push_back(fine_pos[f]);
        auto pos=t_best_positions(mid_groups[m].size(),local,3,seed+65537+13*m);for(int f:mid_groups[m])out.fine_position[f]=pos[fine_pos[f]];}
    return out;
}

static TRoute t_route(const TState&s,const std::vector<std::vector<int>>&mid_groups,const std::vector<int>&mid_of_fine,
        const std::vector<std::vector<int>>&coarse_groups,const std::vector<int>&coarse_of_mid,const TLayout&layout,
        const CRCodec&cc,const CRCodec&mc,const CRCodec&fc){
    TRoute r;r.parameters=96.0+cc.parameter_bytes()+mc.parameter_bytes()+fc.parameter_bytes();r.coarse_codes=coarse_groups.size()*(double)cc.code_bytes();
    r.huffman_models=cc.huffman_model_bytes()+mc.huffman_model_bytes()+fc.huffman_model_bytes();
    std::vector<int>all_coarse(coarse_groups.size());std::iota(all_coarse.begin(),all_coarse.end(),0);r.huffman_coarse_codes=cc.huffman_bytes(all_coarse);
    std::vector<std::vector<int>>sel_mid(coarse_groups.size()),sel_fine(mid_groups.size());std::vector<int>mapped;
    for(size_t qi=0;qi<s.leaf.size();++qi){const auto&cg=s.coarse[qi];r.coarse_metadata+=cg.size()*68.0;mapped.clear();for(int g:cg)mapped.push_back(layout.coarse_position[g]);r.coarse_proofs+=hv_dummy_proof_count(coarse_groups.size(),mapped)*32.0;
        for(int g:cg){r.mid_codes+=coarse_groups[g].size()*(double)mc.code_bytes();r.huffman_mid_codes+=mc.huffman_bytes(coarse_groups[g]);}for(auto&v:sel_mid)v.clear();for(int m:s.mid[qi])sel_mid[coarse_of_mid[m]].push_back(layout.mid_position[m]);
        for(int g:cg)if(!sel_mid[g].empty()){r.mid_metadata+=sel_mid[g].size()*68.0;r.mid_proofs+=hv_dummy_proof_count(coarse_groups[g].size(),sel_mid[g])*32.0;}
        for(int m:s.mid[qi]){r.fine_codes+=mid_groups[m].size()*(double)fc.code_bytes();r.huffman_fine_codes+=fc.huffman_bytes(mid_groups[m]);}for(auto&v:sel_fine)v.clear();for(int f:s.leaf[qi].lists)sel_fine[mid_of_fine[f]].push_back(layout.fine_position[f]);
        for(int m:s.mid[qi])if(!sel_fine[m].empty()){r.fine_metadata+=sel_fine[m].size()*4.0;r.fine_proofs+=hv_dummy_proof_count(mid_groups[m].size(),sel_fine[m])*32.0;}}
    double nq=s.leaf.size();r.mid_codes/=nq;r.fine_codes/=nq;r.coarse_metadata/=nq;r.mid_metadata/=nq;r.fine_metadata/=nq;r.coarse_proofs/=nq;r.mid_proofs/=nq;r.fine_proofs/=nq;
    r.huffman_mid_codes/=nq;r.huffman_fine_codes/=nq;double metadata=r.coarse_metadata+r.mid_metadata+r.fine_metadata,proofs=r.coarse_proofs+r.mid_proofs+r.fine_proofs;
    r.total=r.parameters+r.coarse_codes+r.mid_codes+r.fine_codes+metadata+proofs;
    r.huffman_total=r.parameters+r.huffman_models+r.huffman_coarse_codes+r.huffman_mid_codes+r.huffman_fine_codes+16.0+metadata+proofs;return r;
}

int main(int argc,char**argv)try{
    auto c=t_args(argc,argv);std::cerr<<"Loading data...\n";Matrix xb=fvecs(c.data+"/sift_base.fvecs",c.base),trainq=fvecs(c.data+"/sift_learn.fvecs",c.train_queries),testq=fvecs(c.data+"/sift_query.fvecs",c.test_queries);
    std::cerr<<"Fine IVF="<<c.fine_nlist<<"...\n";auto lists=hkmeans(xb,c.fine_nlist,c.iters);Matrix fine_center=cr_matrix(centers(xb,lists));
    std::cerr<<"Middle groups="<<c.mid_nlist<<(c.balanced_groups?" (balanced)":"")<<"...\n";auto mid_groups=c.balanced_groups?hkmeans_balanced(fine_center,c.mid_nlist,c.iters):hkmeans(fine_center,c.mid_nlist,c.iters);Matrix mid_center=cr_matrix(centers(fine_center,mid_groups));
    std::cerr<<"Coarse groups="<<c.coarse_nlist<<(c.balanced_groups?" (balanced)":"")<<"...\n";auto coarse_groups=c.balanced_groups?hkmeans_balanced(mid_center,c.coarse_nlist,c.iters):hkmeans(mid_center,c.coarse_nlist,c.iters);Matrix coarse_center=cr_matrix(centers(mid_center,coarse_groups));
    std::vector<int>mid_of_fine(c.fine_nlist,-1),fine_pos(c.fine_nlist,-1);for(size_t m=0;m<mid_groups.size();++m)for(size_t p=0;p<mid_groups[m].size();++p){mid_of_fine[mid_groups[m][p]]=m;fine_pos[mid_groups[m][p]]=p;}
    std::vector<int>coarse_of_mid(c.mid_nlist,-1),mid_pos(c.mid_nlist,-1);for(size_t g=0;g<coarse_groups.size();++g)for(size_t p=0;p<coarse_groups[g].size();++p){coarse_of_mid[coarse_groups[g][p]]=g;mid_pos[coarse_groups[g][p]]=p;}
    Matrix mr;mr.d=xb.d;mr.x.resize((size_t)c.mid_nlist*xb.d);for(int m=0;m<c.mid_nlist;++m)for(int j=0;j<xb.d;++j)mr.x[(size_t)m*xb.d+j]=mid_center[m][j]-coarse_center[coarse_of_mid[m]][j];
    Matrix fr;fr.d=xb.d;fr.x.resize((size_t)c.fine_nlist*xb.d);for(int f=0;f<c.fine_nlist;++f)for(int j=0;j<xb.d;++j)fr.x[(size_t)f*xb.d+j]=fine_center[f][j]-mid_center[mid_of_fine[f]][j];
    uint64_t cc_seed=c.auto_seeds?c.seed+c.coarse_nlist:c.seed,mc_seed=c.auto_seeds?c.seed+c.mid_nlist:c.seed+1,fc_seed=c.auto_seeds?c.seed+c.fine_nlist+c.fine_nlist/c.mid_nlist:c.seed+2;
    Matrix empty;std::cerr<<"Codecs "<<c.coarse_codec<<'/'<<c.mid_codec<<'/'<<c.fine_codec<<"...\n";CRCodec cc(c.coarse_codec,coarse_center,empty,std::min(c.pq_train,c.coarse_nlist),c.pq_iters,cc_seed);
    CRCodec mc(c.mid_codec,mr,empty,std::min(c.pq_train,c.mid_nlist),c.pq_iters,mc_seed),fc(c.fine_codec,fr,empty,std::min(c.pq_train,c.fine_nlist),c.pq_iters,fc_seed);
    auto gt=exact_gt(xb,testq,c.k);auto train=t_states(xb,trainq,lists,mid_groups,mid_of_fine,coarse_groups,coarse_of_mid,coarse_center,mr,fr,cc,mc,fc,c.coarse_probe,c.mid_probe,c.fine_probe,c.k,nullptr);
    auto test=t_states(xb,testq,lists,mid_groups,mid_of_fine,coarse_groups,coarse_of_mid,coarse_center,mr,fr,cc,mc,fc,c.coarse_probe,c.mid_probe,c.fine_probe,c.k,&gt);
    auto boundaries=verification_tree_bounds(xb,trainq,lists,train.leaf,0,c.train_queries);NestedRule quantizer(xb,std::move(boundaries));if(!quantizer.contains(xb))throw std::runtime_error("quantizer containment failure");auto tail=hv_lossless_tail(xb,quantizer);
    HVLayout identity;identity.name="identity";identity.routing_position.resize(lists.size());std::iota(identity.routing_position.begin(),identity.routing_position.end(),0);int split=c.train_queries/2;
    auto order=hv_downgrade_order(xb,trainq,train.leaf,0,split,lists,quantizer);std::vector<int>counts{0,8,16,24,32,48,64,80,96,112,128};HVProtocol independent;independent.lossless_tail=true;independent.positional_ids=true;independent.public_dummy=true;
    int best_count=0;double best=std::numeric_limits<double>::infinity();for(int count:counts){auto bits=hv_downgrade_bits(xb.d,order,count);auto e=hv_protocol_evaluate(xb,trainq,train.leaf,split,c.train_queries,lists,quantizer,bits,identity,tail,independent,1,false);double leaf=e.total-e.routing;if(leaf<best){best=leaf;best_count=count;}}
    auto all_order=hv_downgrade_order(xb,trainq,train.leaf,0,c.train_queries,lists,quantizer);auto bits=hv_downgrade_bits(xb.d,all_order,best_count);auto ie=hv_protocol_evaluate(xb,trainq,train.leaf,0,c.train_queries,lists,quantizer,bits,identity,tail,independent,1,false);
    HVProtocol fused=independent;fused.fused_commitment=true;auto fe=hv_protocol_evaluate(xb,trainq,train.leaf,0,c.train_queries,lists,quantizer,bits,identity,tail,fused,1,false);const auto&protocol=(fe.total-fe.routing)<(ie.total-ie.routing)?fused:independent;
    auto leaf=hv_protocol_evaluate(xb,testq,test.leaf,0,c.test_queries,lists,quantizer,bits,identity,tail,protocol,c.repeats,true);double leaf_total=leaf.total-leaf.routing;
    auto layout=t_layout(train,mid_groups,mid_of_fine,fine_pos,coarse_groups,coarse_of_mid,mid_pos,c.seed);
    auto route=t_route(test,mid_groups,mid_of_fine,coarse_groups,coarse_of_mid,layout,cc,mc,fc);double lp=cr_leaf_parameter_bytes(quantizer,bits);route.parameters+=lp;route.total+=lp;route.huffman_total+=lp;double total=route.total+leaf_total,huffman_total=route.huffman_total+leaf_total;
    std::ofstream out(c.output);if(!out)throw std::runtime_error("cannot open output");out<<std::setprecision(12);
    out<<"coarse_codec,mid_codec,fine_codec,coarse_nlist,coarse_probe,mid_nlist,mid_probe,fine_nlist,fine_probe,recall,avg_mid_candidates,avg_fine_candidates,avg_visited,avg_refined,avg_exact,routing_parameters,routing_coarse_codes,routing_mid_codes,routing_fine_codes,routing_coarse_metadata,routing_mid_metadata,routing_fine_metadata,routing_coarse_proofs,routing_mid_proofs,routing_fine_proofs,routing_total,leaf_base,leaf_refine,leaf_exact,leaf_merkle,leaf_total,total_vo,huffman_models,huffman_coarse_codes,huffman_mid_codes,huffman_fine_codes,huffman_routing_total,huffman_total_vo,ann_query_ms,vo_generation_ms,violations\n";
    out<<c.coarse_codec<<','<<c.mid_codec<<','<<c.fine_codec<<','<<c.coarse_nlist<<','<<c.coarse_probe<<','<<c.mid_nlist<<','<<c.mid_probe<<','<<c.fine_nlist<<','<<c.fine_probe<<','<<test.recall<<','<<test.mid_candidates<<','<<test.fine_candidates<<','<<leaf.visited<<','<<leaf.after4<<','<<leaf.exact<<','<<route.parameters<<','<<route.coarse_codes<<','<<route.mid_codes<<','<<route.fine_codes<<','<<route.coarse_metadata<<','<<route.mid_metadata<<','<<route.fine_metadata<<','<<route.coarse_proofs<<','<<route.mid_proofs<<','<<route.fine_proofs<<','<<route.total<<','<<leaf.base_payload<<','<<leaf.refine_payload<<','<<leaf.exact_payload<<','<<leaf.local<<','<<leaf_total<<','<<total<<','<<route.huffman_models<<','<<route.huffman_coarse_codes<<','<<route.huffman_mid_codes<<','<<route.huffman_fine_codes<<','<<route.huffman_total<<','<<huffman_total<<','<<test.query_ms<<','<<leaf.ms<<','<<leaf.object_violations<<'\n';
    std::cout<<"recall="<<test.recall<<" mid_candidates="<<test.mid_candidates<<" fine_candidates="<<test.fine_candidates<<" route="<<route.total/1024<<" KiB leaf="<<leaf_total/1024<<" KiB total="<<total/1024<<" KiB huffman_total="<<huffman_total/1024<<" KiB query="<<test.query_ms<<" ms violations="<<leaf.object_violations<<'\n';return leaf.object_violations?2:0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

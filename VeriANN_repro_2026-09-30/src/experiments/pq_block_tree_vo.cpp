#define RESIDUAL_HIER_VO_NO_MAIN
#include "residual_hier_vo.cpp"
#undef RESIDUAL_HIER_VO_NO_MAIN

// Authenticated PQ-set block omission.  A node stores exact symbol sets for J
// PQ subspaces.  For residual ADC LUT L, the verifier computes
//   LB(node)=sum_{stored s} min_{z in set(node,s)} L[s,z]
//          +sum_{other s} min_z L[s,z].
// This is a lower bound for every code below the node.  The group descriptor
// authenticates the root summary; child descriptors are committed by a Merkle
// root.  Only leaf blocks whose LB can enter the global fine top-p are opened.

struct PQBNode {
    int begin=0,end=0;
    int pivot=-1;
    float radius=0;
    std::vector<int> children;
    std::vector<uint64_t> masks;
    std::vector<uint8_t> rank;
    bool leaf()const{return children.empty();}
};

struct PQBTree {
    std::vector<PQBNode> nodes;
    std::vector<int> order;
};

static void pqb_fill_summary(PQBNode&node,const std::vector<int>&ids,const CRCodec&codec){
    const int m=codec.slots();node.masks.assign(m,0);node.rank.resize(m);std::iota(node.rank.begin(),node.rank.end(),0);
    for(int id:ids)for(int s=0;s<m;++s)node.masks[s]|=uint64_t(1)<<codec.symbol(id,s);
    std::stable_sort(node.rank.begin(),node.rank.end(),[&](int a,int b){
        int ca=__builtin_popcountll(node.masks[a]),cb=__builtin_popcountll(node.masks[b]);return ca==cb?a<b:ca<cb;
    });
}

static void pqb_fill_ball(PQBNode&node,const std::vector<int>&ids,const Matrix&decoded,bool root){
    double radius2=0;if(root){node.pivot=-1;for(int id:ids){double z=0;for(int j=0;j<decoded.d;++j)z+=(double)decoded[id][j]*decoded[id][j];radius2=std::max(radius2,z);}}
    else{std::vector<double>mean(decoded.d);for(int id:ids)for(int j=0;j<decoded.d;++j)mean[j]+=decoded[id][j];for(double&x:mean)x/=ids.size();double best=std::numeric_limits<double>::infinity();
        for(int id:ids){double z=0;for(int j=0;j<decoded.d;++j){double x=decoded[id][j]-mean[j];z+=x*x;}if(z<best){best=z;node.pivot=id;}}
        for(int id:ids)radius2=std::max(radius2,(double)l2(decoded[id],decoded[node.pivot],decoded.d));}
    double r=std::sqrt(radius2);node.radius=(float)r;if((double)node.radius<r)node.radius=std::nextafter(node.radius,std::numeric_limits<float>::infinity());
}

static PQBTree pqb_build_tree(const Matrix&decoded,const std::vector<int>&ids,const CRCodec&codec,int max_block,int iters){
    PQBTree out;if(ids.empty())throw std::runtime_error("empty PQ block group");
    std::vector<std::vector<int>>blocks{ids};
    while(true){auto it=std::max_element(blocks.begin(),blocks.end(),[](const auto&a,const auto&b){return a.size()<b.size();});
        if((int)it->size()<=max_block)break;auto split=split2(decoded,*it,std::max(1,iters));
        if(split.first.empty()||split.second.empty())throw std::runtime_error("PQ block split failed");*it=std::move(split.first);blocks.push_back(std::move(split.second));}
    std::stable_sort(blocks.begin(),blocks.end(),[](const auto&a,const auto&b){return *std::min_element(a.begin(),a.end())<*std::min_element(b.begin(),b.end());});
    PQBNode root;pqb_fill_summary(root,ids,codec);pqb_fill_ball(root,ids,decoded,true);root.begin=0;root.end=ids.size();out.nodes.push_back(std::move(root));
    if(blocks.size()==1){out.order=ids;return out;}
    for(auto&block:blocks){PQBNode leaf;leaf.begin=out.order.size();
        std::stable_sort(block.begin(),block.end());out.order.insert(out.order.end(),block.begin(),block.end());leaf.end=out.order.size();
        pqb_fill_summary(leaf,block,codec);pqb_fill_ball(leaf,block,decoded,false);out.nodes[0].children.push_back(out.nodes.size());out.nodes.push_back(std::move(leaf));}
    return out;
}

static double pqb_lower(const PQBNode&node,const std::vector<float>&lut,const CRCodec&codec,int mask_count,double root_norm2){
    double pivot_distance=node.pivot<0?root_norm2:codec.distance(nullptr,lut,Matrix{},node.pivot,false);
    double ball=std::max(0.0,std::sqrt(std::max(0.0,pivot_distance))-(double)node.radius);ball=std::nextafter(ball*ball,-std::numeric_limits<double>::infinity());
    if(mask_count==0)return ball;
    const int m=codec.slots(),a=codec.alphabet();std::vector<float>global(m);double lb=0;
    for(int s=0;s<m;++s){float v=std::numeric_limits<float>::infinity();for(int z=0;z<a;++z)v=std::min(v,lut[(size_t)s*a+z]);global[s]=v;lb+=v;}
    for(int j=0;j<mask_count;++j){int s=node.rank[j];float v=std::numeric_limits<float>::infinity();uint64_t bits=node.masks[s];
        while(bits){int z=__builtin_ctzll(bits);v=std::min(v,lut[(size_t)s*a+z]);bits&=bits-1;}lb+=(double)v-global[s];}
    return std::max(ball,std::nextafter(lb,-std::numeric_limits<double>::infinity()));
}

struct PQBRun {
    std::vector<VLState>state;
    std::vector<std::vector<int>>coarse;
    double recall=0,query_ms=0,opened_codes=0,opened_blocks=0,expanded_groups=0,child_descriptors=0,residual_bytes=0;
    uint64_t selection_mismatches=0,bound_violations=0;
};

struct PQBItem {double lb=0;int group=0,node=0;};
struct PQBGreater {bool operator()(const PQBItem&a,const PQBItem&b)const{return a.lb==b.lb?(a.group==b.group?a.node>b.node:a.group>b.group):a.lb>b.lb;}};

static PQBRun pqb_query(const Matrix&xb,const Matrix&q,const std::vector<std::vector<int>>&lists,
        const std::vector<std::vector<int>>&groups,const Matrix&coarse_centers,const Matrix&residual,
        const CRCodec&coarse_codec,const CRCodec&residual_codec,const std::vector<PQBTree>&trees,
        int coarse_probe,int fine_probe,int k,int mask_count,const RHStates&reference,
        const std::vector<std::vector<int>>&gt,bool safety_check){
    PQBRun out;out.state.resize(q.rows());out.coarse.resize(q.rows());uint64_t hits=0;double timed_ms=0;
    const double child_descriptor=62.0+(mask_count?4.0+8.0*mask_count:0.0);
    for(size_t qi=0;qi<q.rows();++qi){auto begin=std::chrono::steady_clock::now();auto clut=coarse_codec.prepare(q[qi]);std::vector<int>corder(groups.size());std::iota(corder.begin(),corder.end(),0);
        std::partial_sort(corder.begin(),corder.begin()+coarse_probe,corder.end(),[&](int a,int b){float x=coarse_codec.distance(q[qi],clut,coarse_centers,a,false),y=coarse_codec.distance(q[qi],clut,coarse_centers,b,false);return x==y?a<b:x<y;});
        out.coarse[qi].assign(corder.begin(),corder.begin()+coarse_probe);std::vector<std::vector<float>>luts(groups.size());std::vector<double>root_norm2(groups.size());std::priority_queue<PQBItem,std::vector<PQBItem>,PQBGreater>frontier;
        for(int z=0;z<coarse_probe;++z){int g=corder[z];auto dc=coarse_codec.decode(coarse_centers,g);std::vector<float>qr(q.d);for(int j=0;j<q.d;++j)qr[j]=q[qi][j]-dc[j];
            for(float x:qr)root_norm2[g]+=(double)x*x;luts[g]=residual_codec.prepare(qr.data());frontier.push({pqb_lower(trees[g].nodes[0],luts[g],residual_codec,mask_count,root_norm2[g]),g,0});}
        std::priority_queue<std::pair<float,int>>top;uint64_t wire_bits=0;
        while(!frontier.empty()){
            const auto item=frontier.top();if((int)top.size()>=fine_probe&&item.lb>(double)top.top().first+1e-6)break;frontier.pop();const auto&node=trees[item.group].nodes[item.node];
            if(!node.leaf()){++out.expanded_groups;out.child_descriptors+=node.children.size()*child_descriptor;for(int child:node.children)frontier.push({pqb_lower(trees[item.group].nodes[child],luts[item.group],residual_codec,mask_count,root_norm2[item.group]),item.group,child});}
            else{++out.opened_blocks;std::vector<int>opened(trees[item.group].order.begin()+node.begin,trees[item.group].order.begin()+node.end);out.opened_codes+=opened.size();wire_bits+=residual_codec.huffman_bit_count(opened);
                for(int id:opened){float d=residual_codec.distance(nullptr,luts[item.group],residual,id,false);if((int)top.size()<fine_probe)top.push({d,id});
                    else if(d<top.top().first||(d==top.top().first&&id<top.top().second)){top.pop();top.push({d,id});}}}
        }
        out.residual_bytes+=(wire_bits+7)/8.0;auto&s=out.state[qi];while(!top.empty()){s.lists.push_back(top.top().second);top.pop();}
        if((int)s.lists.size()!=fine_probe)throw std::runtime_error("PQ block tree found too few fine leaves");
        auto got=s.lists,want=reference.state[qi].lists;std::sort(got.begin(),got.end());std::sort(want.begin(),want.end());if(got!=want)++out.selection_mismatches;
        std::priority_queue<std::pair<float,int>>objects;for(int c:s.lists)for(int id:lists[c]){float d=l2(q[qi],xb[id],xb.d);if((int)objects.size()<k)objects.push({d,id});
            else if(d<objects.top().first||(d==objects.top().first&&id<objects.top().second)){objects.pop();objects.push({d,id});}}
        if((int)objects.size()!=k)throw std::runtime_error("PQ block tree found too few objects");s.tau2=objects.top().first;while(!objects.empty()){s.answer.push_back(objects.top().second);objects.pop();}
        timed_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();std::unordered_set<int>answer(s.answer.begin(),s.answer.end());for(int id:gt[qi])hits+=answer.count(id);
        if(safety_check)while(!frontier.empty()){auto item=frontier.top();frontier.pop();const auto&node=trees[item.group].nodes[item.node];
            for(int p=node.begin;p<node.end;++p){int id=trees[item.group].order[p];float d=residual_codec.distance(nullptr,luts[item.group],residual,id,false);if(item.lb>(double)d+1e-5)++out.bound_violations;}}
    }
    double nq=q.rows();out.recall=hits/(nq*k);out.query_ms=timed_ms/nq;out.opened_codes/=nq;out.opened_blocks/=nq;out.expanded_groups/=nq;out.child_descriptors/=nq;out.residual_bytes/=nq;return out;
}

struct PQBRoute {double parameters=0,models=0,coarse_codes=0,residual_codes=0,group_metadata=0,fine_metadata=0,child_descriptors=0,coarse_proofs=0,fine_proofs=0,total=0;};

static PQBRoute pqb_route(const PQBRun&query,const std::vector<std::vector<int>>&groups,const std::vector<PQBTree>&trees,
        const std::vector<int>&fine_position,const RHLayout&layout,const CRCodec&coarse_codec,const CRCodec&residual_codec,int mask_count){
    PQBRoute out;out.parameters=96.0+coarse_codec.parameter_bytes()+residual_codec.parameter_bytes();out.models=coarse_codec.huffman_model_bytes()+residual_codec.huffman_model_bytes();
    std::vector<int>all_coarse(groups.size());std::iota(all_coarse.begin(),all_coarse.end(),0);out.coarse_codes=coarse_codec.huffman_bytes(all_coarse);out.residual_codes=query.residual_bytes;out.child_descriptors=query.child_descriptors;
    std::vector<std::vector<int>>selected(groups.size());std::vector<int>mapped;for(size_t qi=0;qi<query.state.size();++qi){const auto&cg=query.coarse[qi];
        out.group_metadata+=cg.size()*(68.0+6.0+(mask_count?4.0+8.0*mask_count:0.0));mapped.clear();for(int g:cg)mapped.push_back(layout.coarse_position[g]);out.coarse_proofs+=hv_dummy_proof_count(groups.size(),mapped)*32.0;
        for(auto&v:selected)v.clear();for(int leaf:query.state[qi].lists){int g=-1; // Groups are small; avoid another persistent client directory.
            // The server supplies the selected fine id (4 B); membership is authenticated by the local directory proof below.
            for(int x:cg)if(fine_position[leaf]>=0&&std::find(groups[x].begin(),groups[x].end(),leaf)!=groups[x].end()){g=x;break;}if(g<0)throw std::runtime_error("selected fine leaf missing from selected group");selected[g].push_back(fine_position[leaf]);}
        for(int g:cg)if(!selected[g].empty()){out.fine_metadata+=selected[g].size()*4.0;out.fine_proofs+=hv_dummy_proof_count(groups[g].size(),selected[g])*32.0;}}
    double nq=query.state.size();out.group_metadata/=nq;out.fine_metadata/=nq;out.coarse_proofs/=nq;out.fine_proofs/=nq;
    out.total=out.parameters+out.models+out.coarse_codes+out.residual_codes+out.group_metadata+out.fine_metadata+out.child_descriptors+out.coarse_proofs+out.fine_proofs+16.0;return out;
}

int main(int argc,char**argv)try{
    auto cfg=cr_args(argc,argv);if(!cfg.coarse_nlist||cfg.coarse_codec.empty()||cfg.residual_codec.empty())throw std::runtime_error("require coarse options plus both codecs");
    std::cerr<<"Loading data...\n";Matrix xb=fvecs(cfg.data+"/sift_base.fvecs",cfg.base),trainq=fvecs(cfg.data+"/sift_learn.fvecs",cfg.train_queries),testq=fvecs(cfg.data+"/sift_query.fvecs",cfg.test_queries);
    std::cerr<<"Training fine/coarse IVF"<<(cfg.balanced_groups?" with balanced coarse groups":"")<<"...\n";auto lists=hkmeans(xb,cfg.nlist,cfg.iters);auto fine_vector=centers(xb,lists);Matrix fine_centers=cr_matrix(fine_vector);auto groups=cfg.balanced_groups?hkmeans_balanced(fine_centers,cfg.coarse_nlist,cfg.iters):hkmeans(fine_centers,cfg.coarse_nlist,cfg.iters);auto coarse_vector=centers(fine_centers,groups);Matrix coarse_centers=cr_matrix(coarse_vector);
    std::vector<int>group_of(cfg.nlist,-1),original_position(cfg.nlist,-1);for(size_t g=0;g<groups.size();++g)for(size_t p=0;p<groups[g].size();++p){group_of[groups[g][p]]=g;original_position[groups[g][p]]=p;}
    Matrix residual;residual.d=xb.d;residual.x.resize((size_t)cfg.nlist*xb.d);for(int leaf=0;leaf<cfg.nlist;++leaf)for(int j=0;j<xb.d;++j)residual.x[(size_t)leaf*xb.d+j]=fine_centers[leaf][j]-coarse_centers[group_of[leaf]][j];
    Matrix empty;CRCodec coarse_codec(cfg.coarse_codec,coarse_centers,empty,std::min(cfg.pq_train,cfg.coarse_nlist),cfg.pq_iters,cfg.seed);CRCodec residual_codec(cfg.residual_codec,residual,empty,cfg.pq_train,cfg.pq_iters,cfg.seed+1);
    if(residual_codec.alphabet()>64)throw std::runtime_error("PQ-set masks currently require alphabet <= 64");auto gt=exact_gt(xb,testq,cfg.k);
    auto train=rh_states(xb,trainq,lists,fine_centers,groups,group_of,coarse_centers,residual,coarse_codec,residual_codec,cfg.coarse_nprobe,cfg.nprobe,cfg.k,nullptr);auto test=rh_states(xb,testq,lists,fine_centers,groups,group_of,coarse_centers,residual,coarse_codec,residual_codec,cfg.coarse_nprobe,cfg.nprobe,cfg.k,&gt);
    std::cerr<<"Training leaf verification protocol...\n";auto boundaries=verification_tree_bounds(xb,trainq,lists,train.state,0,cfg.train_queries);NestedRule quantizer(xb,std::move(boundaries));if(!quantizer.contains(xb))throw std::runtime_error("quantizer containment failure");auto tail=hv_lossless_tail(xb,quantizer);
    HVLayout identity;identity.name="identity";identity.routing_position.resize(lists.size());std::iota(identity.routing_position.begin(),identity.routing_position.end(),0);int split=cfg.train_queries/2;auto downgrade=hv_downgrade_order(xb,trainq,train.state,0,split,lists,quantizer);std::vector<int>counts{0,8,16,24,32,48,64,80,96,112,128};
    HVProtocol independent;independent.lossless_tail=true;independent.positional_ids=true;independent.public_dummy=true;int best_count=0;double best=std::numeric_limits<double>::infinity();for(int count:counts){auto bits=hv_downgrade_bits(xb.d,downgrade,count);auto e=hv_protocol_evaluate(xb,trainq,train.state,split,cfg.train_queries,lists,quantizer,bits,identity,tail,independent,1,false);double leaf=e.total-e.routing;if(leaf<best){best=leaf;best_count=count;}}
    auto all_order=hv_downgrade_order(xb,trainq,train.state,0,cfg.train_queries,lists,quantizer);auto bits=hv_downgrade_bits(xb.d,all_order,best_count);auto ie=hv_protocol_evaluate(xb,trainq,train.state,0,cfg.train_queries,lists,quantizer,bits,identity,tail,independent,1,false);HVProtocol fused=independent;fused.fused_commitment=true;auto fe=hv_protocol_evaluate(xb,trainq,train.state,0,cfg.train_queries,lists,quantizer,bits,identity,tail,fused,1,false);const auto&protocol=(fe.total-fe.routing)<(ie.total-ie.routing)?fused:independent;
    auto leaf=hv_protocol_evaluate(xb,testq,test.state,0,cfg.test_queries,lists,quantizer,bits,identity,tail,protocol,cfg.repeats,true);double leaf_total=leaf.total-leaf.routing,leaf_parameters=cr_leaf_parameter_bytes(quantizer,bits);auto baseline_layout=rh_learn_layout(train,groups,group_of,original_position,cfg.seed);auto baseline_route=rh_route(test,groups,group_of,baseline_layout,coarse_codec,residual_codec);baseline_route.huffman_total+=leaf_parameters;double baseline_total=baseline_route.huffman_total+leaf_total;
    Matrix decoded;decoded.d=residual.d;decoded.x.resize(residual.x.size());for(size_t id=0;id<residual.rows();++id){auto v=residual_codec.decode(residual,id);std::copy(v.begin(),v.end(),decoded.x.begin()+id*(size_t)decoded.d);}
    std::vector<int>blocks=cfg.max_block==1?std::vector<int>{4,8,12,16,24,32,48}:std::vector<int>{cfg.max_block};std::vector<int>masks=cfg.beam==0?std::vector<int>{0,2,4,8,16,32}:std::vector<int>{cfg.beam};
    std::ofstream out(cfg.output);if(!out)throw std::runtime_error("cannot open output");out<<std::setprecision(12);out<<"balanced_groups,max_block,mask_subspaces,recall,selection_mismatches,bound_violations,avg_opened_codes,avg_opened_blocks,avg_expanded_groups,routing_parameters,huffman_models,huffman_coarse_codes,huffman_opened_residual_codes,root_group_metadata,fine_metadata,child_descriptors,coarse_proofs,fine_proofs,routing_total,leaf_total,total_vo,baseline_huffman_total,vo_reduction_vs_baseline,ann_query_ms,baseline_query_ms,violations\n";
    for(int block:blocks){std::cerr<<"Building authenticated PQ blocks max="<<block<<"...\n";std::vector<PQBTree>trees(groups.size());std::vector<int>fine_position(cfg.nlist,-1);for(size_t g=0;g<groups.size();++g){trees[g]=pqb_build_tree(decoded,groups[g],residual_codec,block,std::min(3,cfg.iters));for(size_t p=0;p<trees[g].order.size();++p)fine_position[trees[g].order[p]]=p;}
        for(int mc:masks){if(mc<0||mc>residual_codec.slots())continue;bool safety=blocks.size()==1&&masks.size()==1;std::cerr<<"Querying masks="<<mc<<"...\n";auto run=pqb_query(xb,testq,lists,groups,coarse_centers,residual,coarse_codec,residual_codec,trees,cfg.coarse_nprobe,cfg.nprobe,cfg.k,mc,test,gt,safety);auto route=pqb_route(run,groups,trees,fine_position,baseline_layout,coarse_codec,residual_codec,mc);route.parameters+=leaf_parameters;route.total+=leaf_parameters;double total=route.total+leaf_total;uint64_t violations=leaf.object_violations+run.selection_mismatches+run.bound_violations;
            out<<(cfg.balanced_groups?1:0)<<','<<block<<','<<mc<<','<<run.recall<<','<<run.selection_mismatches<<','<<run.bound_violations<<','<<run.opened_codes<<','<<run.opened_blocks<<','<<run.expanded_groups<<','<<route.parameters<<','<<route.models<<','<<route.coarse_codes<<','<<route.residual_codes<<','<<route.group_metadata<<','<<route.fine_metadata<<','<<route.child_descriptors<<','<<route.coarse_proofs<<','<<route.fine_proofs<<','<<route.total<<','<<leaf_total<<','<<total<<','<<baseline_total<<','<<1.0-total/baseline_total<<','<<run.query_ms<<','<<test.query_ms<<','<<violations<<'\n';
            std::cout<<"block="<<block<<" masks="<<mc<<" recall="<<run.recall<<" opened="<<run.opened_codes<<" route="<<route.total/1024<<" KiB total="<<total/1024<<" KiB reduction="<<(1-total/baseline_total)*100<<"% query="<<run.query_ms<<" ms mismatch="<<run.selection_mismatches<<" violations="<<violations<<'\n';}}
    return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

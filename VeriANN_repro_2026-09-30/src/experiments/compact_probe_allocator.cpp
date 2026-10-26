#define BUDGETED_RESIDUAL_VO_NO_MAIN
#include "budgeted_residual_vo.cpp"
#undef BUDGETED_RESIDUAL_VO_NO_MAIN

#include <set>

// A stagewise alternative to best-first efVO.  The production policy is a tiny
// authenticated tier -> per-level probe table.  An anchored quadratic fit is
// retained as a compact-model ablation.  Both execute ordinary per-level top-p,
// so the verifier never needs to replay a dynamic priority queue.

struct WCfg {
    std::string data="../dataset/sift",output="compact_probe_allocator.csv";
    int base=100000,cal_queries=60,test_queries=500,k=100,iters=4,pq_train=12000,pq_iters=4,repeats=1;
    std::vector<int>counts{256,4096,65536},anchor{102,653,1254};
    std::vector<std::string>modes{"sq4","pq24","pq16"};
    std::vector<double>budget_ratios{0.75,0.9,1.0,1.15,1.3};
    uint64_t seed=20260830;
};

static WCfg w_args(int argc,char**argv){
    WCfg c;for(int i=1;i<argc;++i){std::string a=argv[i];auto v=[&](){if(++i==argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
        if(a=="--dataset")c.data=v();else if(a=="--output")c.output=v();else if(a=="--base")c.base=std::stoi(v());
        else if(a=="--cal-queries")c.cal_queries=std::stoi(v());else if(a=="--test-queries")c.test_queries=std::stoi(v());else if(a=="--k")c.k=std::stoi(v());
        else if(a=="--counts")c.counts=b_ints(v());else if(a=="--codecs")c.modes=b_strings(v());else if(a=="--anchor-probes")c.anchor=b_ints(v());
        else if(a=="--budget-ratios")c.budget_ratios=b_doubles(v());else if(a=="--iters")c.iters=std::stoi(v());else if(a=="--pq-train")c.pq_train=std::stoi(v());
        else if(a=="--pq-iters")c.pq_iters=std::stoi(v());else if(a=="--repeats")c.repeats=std::stoi(v());else if(a=="--seed")c.seed=std::stoull(v());else throw std::runtime_error("unknown option "+a);}
    if(c.counts.size()<2||c.counts.size()>4||c.modes.size()!=c.counts.size()||c.anchor.size()!=c.counts.size()||c.base<c.counts.back()||c.cal_queries<2||c.test_queries<1||c.k<1)throw std::runtime_error("invalid configuration");
    for(size_t l=0;l<c.counts.size();++l)if(c.anchor[l]<1||c.anchor[l]>c.counts[l]||(l&&c.anchor[l]>c.anchor[l-1]*(int)std::ceil(c.counts[l]/(double)c.counts[l-1])))throw std::runtime_error("invalid anchor probes");
    std::sort(c.budget_ratios.begin(),c.budget_ratios.end());return c;
}

struct WStates {
    std::vector<BTrace>trace;double recall=0,query_ms=0,routing_ms=0,visited=0,generated=0;std::vector<double>selected;
};

static std::vector<int> w_clamp_probes(const BIndex&t,const std::vector<int>&p,int k){
    std::vector<int>out=p;int min_leaf=std::min(t.counts.back(),std::max(k,(int)std::ceil(2.5*k*t.counts.back()/(double)t.xb->rows())));
    for(int l=0;l<t.depth;++l){int cap=t.counts[l];if(l){int fan=(int)std::ceil(t.counts[l]/(double)t.counts[l-1]);cap=std::min(cap,out[l-1]*fan);}out[l]=std::max(1,std::min(cap,out[l]));}
    out.back()=std::min(t.counts.back(),std::max(min_leaf,out.back()));
    for(int l=t.depth-1;l>0;--l){int fan=(int)std::ceil(t.counts[l]/(double)t.counts[l-1]);out[l-1]=std::max(out[l-1],(out[l]+fan-1)/fan);}return out;
}

static WStates w_query(const BIndex&t,const Matrix&q,const std::vector<int>&probe,int k,const std::vector<std::vector<int>>*gt){
    WStates out;out.trace.resize(q.rows());out.selected.assign(t.depth,0);uint64_t hits=0;double routing_ns=0;auto all_begin=std::chrono::steady_clock::now();
    for(size_t qi=0;qi<q.rows();++qi){auto route_begin=std::chrono::steady_clock::now();auto&tr=out.trace[qi];tr.selected.resize(t.depth);auto lut0=t.codec[0]->prepare(q[qi]);std::vector<std::pair<float,int>>cand(t.counts[0]);
        for(int id=0;id<t.counts[0];++id)cand[id]={t.codec[0]->distance(q[qi],lut0,t.encoded[0],id,false),id};int take=std::min<int>(probe[0],cand.size());std::partial_sort(cand.begin(),cand.begin()+take,cand.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});
        std::vector<int>previous;previous.reserve(take);std::vector<float>previous_decoded;previous_decoded.reserve((size_t)take*t.d);for(int z=0;z<take;++z){int id=cand[z].second;previous.push_back(id);tr.selected[0].push_back(id);auto d=t.codec[0]->decode(t.encoded[0],id);previous_decoded.insert(previous_decoded.end(),d.begin(),d.end());}out.generated+=t.counts[0];
        // A one-level IVF tree has no residual-routing loop below.  Its selected
        // roots are already the terminal posting lists.
        if(t.depth==1)tr.leaf.lists=previous;
        for(int l=1;l<t.depth;++l){std::vector<std::pair<float,int>>next;for(size_t z=0;z<previous.size();++z){std::vector<float>qr(t.d);const float*parent=previous_decoded.data()+z*t.d;for(int j=0;j<t.d;++j)qr[j]=q[qi][j]-parent[j];auto lut=t.codec[l]->prepare(qr.data());for(int child:t.children[l-1][previous[z]])next.push_back({t.codec[l]->distance(qr.data(),lut,t.encoded[l],child,false),child});}
            out.generated+=next.size();take=std::min<int>(probe[l],next.size());std::partial_sort(next.begin(),next.begin()+take,next.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});std::vector<int>selected;selected.reserve(take);for(int z=0;z<take;++z){selected.push_back(next[z].second);tr.selected[l].push_back(next[z].second);}if(l==t.depth-1){tr.leaf.lists=selected;break;}
            std::vector<int>slot(t.counts[l-1],-1);for(size_t z=0;z<previous.size();++z)slot[previous[z]]=z;std::vector<float>decoded;decoded.resize((size_t)take*t.d);for(int z=0;z<take;++z){int id=selected[z],ps=slot[t.parent[l][id]];auto residual=t.codec[l]->decode(t.encoded[l],id);const float*parent=previous_decoded.data()+(size_t)ps*t.d;for(int j=0;j<t.d;++j)decoded[(size_t)z*t.d+j]=parent[j]+residual[j];}previous.swap(selected);previous_decoded.swap(decoded);}
        routing_ns+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-route_begin).count();std::priority_queue<std::pair<float,int>>top;
        for(int f:tr.leaf.lists)for(int id:t.lists[f]){float distance=l2(q[qi],(*t.xb)[id],t.d);if((int)top.size()<k)top.push({distance,id});else if(distance<top.top().first||(distance==top.top().first&&id<top.top().second)){top.pop();top.push({distance,id});}out.visited++;}
        if((int)top.size()<k)throw std::runtime_error("stagewise probes returned fewer than k objects");tr.leaf.tau2=top.top().first;while(!top.empty()){tr.leaf.answer.push_back(top.top().second);top.pop();}if(gt){std::unordered_set<int>a(tr.leaf.answer.begin(),tr.leaf.answer.end());for(int id:(*gt)[qi])hits+=a.count(id);}for(int l=0;l<t.depth;++l)out.selected[l]+=tr.selected[l].size();}
    double nq=q.rows();out.query_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-all_begin).count()/nq;out.routing_ms=routing_ns/nq/1e6;out.recall=gt?hits/(nq*k):0;out.visited/=nq;out.generated/=nq;for(double&x:out.selected)x/=nq;return out;
}

static BRoute w_route(const BIndex&t,const WStates&s){
    BRoute out;out.parameters=96.0;for(const auto&x:t.codec)out.parameters+=x->parameter_bytes();double fixed=out.parameters+t.counts[0]*(double)t.codec[0]->code_bytes(),huff=out.parameters+16.0;std::vector<int>roots(t.counts[0]);std::iota(roots.begin(),roots.end(),0);for(const auto&x:t.codec)huff+=x->huffman_model_bytes();huff+=t.codec[0]->huffman_bytes(roots);double variable=0,hvariable=0;
    for(const auto&tr:s.trace){double shared=0;for(int l=0;l<t.depth;++l){shared+=tr.selected[l].size()*(l==t.depth-1?4.0:68.0);if(l==0)shared+=hv_dummy_proof_count(t.counts[0],tr.selected[0])*32.0;else{std::vector<std::vector<int>>picked(t.counts[l-1]);for(int id:tr.selected[l])picked[t.parent[l][id]].push_back(t.position[l][id]);for(size_t p=0;p<picked.size();++p)if(!picked[p].empty())shared+=hv_dummy_proof_count(t.children[l-1][p].size(),picked[p])*32.0;}}
        double blocks=0,hblocks=0;for(int l=0;l<t.depth-1;++l)for(int parent:tr.selected[l]){blocks+=t.children[l][parent].size()*(double)t.codec[l+1]->code_bytes();hblocks+=t.codec[l+1]->huffman_bytes(t.children[l][parent]);}variable+=shared+blocks;hvariable+=shared+hblocks;}
    double nq=s.trace.size();out.fixed=fixed+variable/nq;out.huffman=huff+hvariable/nq;return out;
}

static double w_leaf_proxy(const BIndex&t,const WStates&s){return s.visited*t.leaf_unit;}

struct WFull {double route=0,leaf=0,total=0,vo_ms=0;double visited=0,refined=0,exact=0;double base_payload=0,refine_payload=0,exact_payload=0,refinement_merkle=0,exact_merkle=0;long violations=0;};
static WFull w_full(const BIndex&t,const Matrix&trainq,const WStates&train,const Matrix&testq,const WStates&test,int repeats){
    std::vector<VLState>train_leaf,test_leaf;train_leaf.reserve(train.trace.size());test_leaf.reserve(test.trace.size());for(const auto&x:train.trace)train_leaf.push_back(x.leaf);for(const auto&x:test.trace)test_leaf.push_back(x.leaf);
    auto bounds=verification_tree_bounds(*t.xb,trainq,t.lists,train_leaf,0,trainq.rows());NestedRule quantizer(*t.xb,std::move(bounds));if(!quantizer.contains(*t.xb))throw std::runtime_error("quantizer containment failure");auto tail=hv_lossless_tail(*t.xb,quantizer);HVLayout identity;identity.name="identity";identity.routing_position.resize(t.lists.size());std::iota(identity.routing_position.begin(),identity.routing_position.end(),0);int split=trainq.rows()/2;
    auto order=hv_downgrade_order(*t.xb,trainq,train_leaf,0,split,t.lists,quantizer);std::vector<int>choices{0,8,16,24,32,48,64,80,96,112,128};HVProtocol independent;independent.lossless_tail=true;independent.positional_ids=true;independent.public_dummy=true;int best_count=0;double best=std::numeric_limits<double>::infinity();for(int count:choices){auto bits=hv_downgrade_bits(t.d,order,count);auto e=hv_protocol_evaluate(*t.xb,trainq,train_leaf,split,trainq.rows(),t.lists,quantizer,bits,identity,tail,independent,1,false);double leaf=e.total-e.routing;if(leaf<best){best=leaf;best_count=count;}}
    auto full_order=hv_downgrade_order(*t.xb,trainq,train_leaf,0,trainq.rows(),t.lists,quantizer);auto bits=hv_downgrade_bits(t.d,full_order,best_count);auto ie=hv_protocol_evaluate(*t.xb,trainq,train_leaf,0,trainq.rows(),t.lists,quantizer,bits,identity,tail,independent,1,false);HVProtocol fused=independent;fused.fused_commitment=true;auto fe=hv_protocol_evaluate(*t.xb,trainq,train_leaf,0,trainq.rows(),t.lists,quantizer,bits,identity,tail,fused,1,false);const auto&protocol=(fe.total-fe.routing)<(ie.total-ie.routing)?fused:independent;
    auto leaf=hv_protocol_evaluate(*t.xb,testq,test_leaf,0,testq.rows(),t.lists,quantizer,bits,identity,tail,protocol,repeats,true);auto route=w_route(t,test);double leaf_params=cr_leaf_parameter_bytes(quantizer,bits);WFull out;out.route=(std::min(route.fixed,route.huffman)+leaf_params)/1024.0;out.leaf=(leaf.total-leaf.routing)/1024.0;out.total=out.route+out.leaf;out.vo_ms=leaf.ms;out.visited=leaf.visited;out.refined=leaf.after4;out.exact=leaf.exact;out.base_payload=leaf.base_payload/1024.0;out.refine_payload=leaf.refine_payload/1024.0;out.exact_payload=leaf.exact_payload/1024.0;out.refinement_merkle=leaf.refinement_merkle/1024.0;out.exact_merkle=leaf.exact_merkle/1024.0;out.violations=leaf.object_violations;return out;
}

struct WPoint {std::vector<int>p;double recall=0,cost=0,query_ms=0;};

static std::vector<double> w_linear_cost_fit(const std::vector<WPoint>&points,int d){
    int m=d+1;std::vector<std::vector<double>>a(m,std::vector<double>(m+1));for(const auto&x:points){std::vector<double>v(m,1);for(int j=0;j<d;++j)v[j+1]=x.p[j];for(int r=0;r<m;++r){a[r][m]+=v[r]*x.cost;for(int c=0;c<m;++c)a[r][c]+=v[r]*v[c];}}for(int i=0;i<m;++i)a[i][i]+=1e-6;
    for(int i=0;i<m;++i){int pivot=i;for(int r=i+1;r<m;++r)if(std::abs(a[r][i])>std::abs(a[pivot][i]))pivot=r;std::swap(a[i],a[pivot]);double z=a[i][i];if(std::abs(z)<1e-12)continue;for(int c=i;c<=m;++c)a[i][c]/=z;for(int r=0;r<m;++r)if(r!=i){double f=a[r][i];for(int c=i;c<=m;++c)a[r][c]-=f*a[i][c];}}
    std::vector<double>coef(m);for(int i=0;i<m;++i)coef[i]=a[i][m];for(int i=1;i<m;++i)coef[i]=std::max(0.0,coef[i]);double mean=0;for(const auto&x:points){double y=x.cost;for(int i=1;i<m;++i)y-=coef[i]*x.p[i-1];mean+=y;}coef[0]=std::max(0.0,mean/points.size());return coef;
}

static double w_cost(const std::vector<double>&c,const std::vector<int>&p){double z=c[0];for(size_t i=0;i<p.size();++i)z+=c[i+1]*p[i];return z;}

static std::string w_join(const std::vector<int>&v){std::ostringstream s;for(size_t i=0;i<v.size();++i){if(i)s<<'/';s<<v[i];}return s.str();}
static std::string w_join_double(const std::vector<double>&v){std::ostringstream s;s<<std::setprecision(5);for(size_t i=0;i<v.size();++i){if(i)s<<'/';s<<v[i];}return s.str();}

static void w_grid_rec(const BIndex&t,const WCfg&c,double scale,int l,std::vector<int>&p,std::set<std::vector<int>>&all){
    static const double tilt[]{0.78,1.0,1.28};if(l==t.depth){all.insert(w_clamp_probes(t,p,c.k));return;}for(double z:tilt){p[l]=std::max(1,(int)std::lround(c.anchor[l]*scale*z));w_grid_rec(t,c,scale,l+1,p,all);}
}

#ifndef COMPACT_PROBE_ALLOCATOR_NO_MAIN
int main(int argc,char**argv)try{
    WCfg c=w_args(argc,argv);Matrix xb=fvecs(c.data+"/sift_base.fvecs",c.base),calq=fvecs(c.data+"/sift_learn.fvecs",c.cal_queries),testq=fvecs(c.data+"/sift_query.fvecs",c.test_queries);auto calgt=exact_gt(xb,calq,c.k),testgt=exact_gt(xb,testq,c.k);BCfg bc;bc.base=c.base;bc.counts=c.counts;bc.modes=c.modes;bc.iters=c.iters;bc.pq_train=c.pq_train;bc.pq_iters=c.pq_iters;bc.seed=c.seed;auto tree=b_build(xb,bc);c.anchor=w_clamp_probes(tree,c.anchor,c.k);auto policy_begin=std::chrono::steady_clock::now();
    std::set<std::vector<int>>grid;grid.insert(c.anchor);std::vector<int>tmp(tree.depth);for(double s:{0.55,0.70,0.85,1.0,1.20,1.45})w_grid_rec(tree,c,s,0,tmp,grid);std::vector<WPoint>points;points.reserve(grid.size());std::cerr<<"Calibration candidates="<<grid.size()<<"...\n";int done=0;
    for(const auto&p:grid){auto st=w_query(tree,calq,p,c.k,&calgt);auto route=w_route(tree,st);WPoint x;x.p=p;x.recall=st.recall;x.cost=std::min(route.fixed,route.huffman)+w_leaf_proxy(tree,st);x.query_ms=st.query_ms;points.push_back(x);if(++done%25==0)std::cerr<<"  "<<done<<'/'<<grid.size()<<'\n';}
    auto anchor_it=std::find_if(points.begin(),points.end(),[&](const WPoint&x){return x.p==c.anchor;});if(anchor_it==points.end())throw std::runtime_error("anchor missing");double b0=anchor_it->cost;std::vector<const WPoint*>oracle;for(double ratio:c.budget_ratios){double budget=b0*ratio;const WPoint*best=nullptr;for(const auto&x:points)if(x.cost<=budget&&(best==nullptr||x.recall>best->recall||(x.recall==best->recall&&x.cost<best->cost)))best=&x;if(!best)best=&*std::min_element(points.begin(),points.end(),[](const auto&a,const auto&b){return a.cost<b.cost;});oracle.push_back(best);}
    // Fit log(p_l / anchor_l) = beta1_l log(B/B0) + beta2_l log^2(B/B0).
    // Passing exactly through B/B0=1 preserves the validated operating point.
    std::vector<double>beta1(tree.depth),beta2(tree.depth);for(int l=0;l<tree.depth;++l){double s11=1e-6,s12=0,s22=1e-6,t1=0,t2=0;for(size_t z=0;z<oracle.size();++z){double x=std::log(c.budget_ratios[z]),x2=x*x,y=std::log((double)oracle[z]->p[l]/c.anchor[l]);s11+=x*x;s12+=x*x2;s22+=x2*x2;t1+=x*y;t2+=x2*y;}double det=s11*s22-s12*s12;if(std::abs(det)>1e-12){beta1[l]=(t1*s22-t2*s12)/det;beta2[l]=(s11*t2-s12*t1)/det;}}
    auto coef=w_linear_cost_fit(points,tree.depth);double predicted_anchor=w_cost(coef,c.anchor);if(predicted_anchor>0){double scale=b0/predicted_anchor;for(double&x:coef)x*=scale;}double mae=0;for(const auto&x:points)mae+=std::abs(w_cost(coef,x.p)-x.cost)/x.cost;mae/=points.size();size_t compact_model_bytes=24+12*tree.depth,table_bytes=24+4*tree.depth*c.budget_ratios.size();double policy_fit_s=std::chrono::duration<double>(std::chrono::steady_clock::now()-policy_begin).count();
    std::ofstream out(c.output);if(!out)throw std::runtime_error("cannot open output");out<<std::setprecision(12);out<<"kind,budget_ratio,budget_proxy_kib,probes,anchor,fit_beta1,fit_beta2,cost_coef,model_bytes,cal_recall,cal_proxy_kib,test_recall,actual_route_kib,actual_leaf_kib,actual_total_kib,avg_visited,avg_generated_centers,routing_ms,ann_query_ms,vo_generation_ms,index_build_s,policy_fit_s,object_violations,cost_model_mape\n";
    for(size_t z=0;z<c.budget_ratios.size();++z){double ratio=c.budget_ratios[z],budget=b0*ratio,x=std::log(ratio);std::vector<int>raw(tree.depth);for(int l=0;l<tree.depth;++l)raw[l]=std::max(1,(int)std::lround(c.anchor[l]*std::exp(beta1[l]*x+beta2[l]*x*x)));double lo=0.25,hi=2.0;for(int it=0;it<40;++it){double m=(lo+hi)/2;std::vector<int>q(tree.depth);for(int l=0;l<tree.depth;++l)q[l]=std::max(1,(int)std::lround(raw[l]*m));q=w_clamp_probes(tree,q,c.k);if(w_cost(coef,q)<=budget)lo=m;else hi=m;}for(int l=0;l<tree.depth;++l)raw[l]=std::max(1,(int)std::lround(raw[l]*lo));auto compact=std::abs(ratio-1.0)<1e-12?c.anchor:w_clamp_probes(tree,raw,c.k);
        for(int kind=0;kind<2;++kind){const auto&p=kind?oracle[z]->p:compact;auto train=w_query(tree,calq,p,c.k,&calgt),test=w_query(tree,testq,p,c.k,&testgt);auto route=w_route(tree,train);double proxy=std::min(route.fixed,route.huffman)+w_leaf_proxy(tree,train);auto full=w_full(tree,calq,train,testq,test,c.repeats);size_t bytes=kind?table_bytes:compact_model_bytes;out<<(kind?"table":"compact")<<','<<ratio<<','<<budget/1024.0<<','<<w_join(p)<<','<<w_join(c.anchor)<<','<<w_join_double(beta1)<<','<<w_join_double(beta2)<<','<<w_join_double(coef)<<','<<bytes<<','<<train.recall<<','<<proxy/1024.0<<','<<test.recall<<','<<full.route<<','<<full.leaf<<','<<full.total<<','<<test.visited<<','<<test.generated<<','<<test.routing_ms<<','<<test.query_ms<<','<<full.vo_ms<<','<<tree.build_s<<','<<policy_fit_s<<','<<full.violations<<','<<mae<<'\n';std::cout<<(kind?"table":"compact")<<" ratio="<<ratio<<" probes="<<w_join(p)<<" cal="<<train.recall<<" test="<<test.recall<<" VO="<<full.total<<" KiB query="<<test.query_ms<<" ms\n";}}
    std::cout<<"POLICY anchor="<<w_join(c.anchor)<<" beta1="<<w_join_double(beta1)<<" beta2="<<w_join_double(beta2)<<" coef="<<w_join_double(coef)<<" compact_bytes="<<compact_model_bytes<<" table_bytes="<<table_bytes<<" cost_MAPE="<<mae<<" candidates="<<points.size()<<" build="<<tree.build_s<<" s fit="<<policy_fit_s<<" s\n";return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
#endif

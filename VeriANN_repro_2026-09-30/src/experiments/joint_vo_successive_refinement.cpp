#define COMPACT_PROBE_ALLOCATOR_NO_MAIN
#include "compact_probe_allocator.cpp"
#undef COMPACT_PROBE_ALLOCATOR_NO_MAIN

#include <map>

// One core idea for both structure and query parameters:
// VO-guided successive refinement in log space.
//  * structure: insert a resolution near the logarithmic midpoint of an edge;
//  * probes: start feasible, then halve coordinate step sizes and remove or
//    reallocate effort while preserving calibrated Recall.

struct JCfg {
    std::string data="../dataset/sift",output="joint_vo_refinement.csv";
    int base=100000,cal_queries=60,test_queries=500,k=100,iters=4,pq_train=12000,pq_iters=4;
    int max_fine=65536,max_depth=4,beam=5,repeats=1;
    double target=0.99,margin=0.002;
    uint64_t seed=20260830;
};

static JCfg j_args(int argc,char**argv){
    JCfg c;for(int i=1;i<argc;++i){std::string a=argv[i];auto v=[&](){if(++i==argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
        if(a=="--dataset")c.data=v();else if(a=="--output")c.output=v();else if(a=="--base")c.base=std::stoi(v());
        else if(a=="--cal-queries")c.cal_queries=std::stoi(v());else if(a=="--test-queries")c.test_queries=std::stoi(v());
        else if(a=="--k")c.k=std::stoi(v());else if(a=="--max-fine")c.max_fine=std::stoi(v());else if(a=="--max-depth")c.max_depth=std::stoi(v());
        else if(a=="--beam")c.beam=std::stoi(v());else if(a=="--target")c.target=std::stod(v());else if(a=="--margin")c.margin=std::stod(v());
        else if(a=="--iters")c.iters=std::stoi(v());else if(a=="--pq-train")c.pq_train=std::stoi(v());else if(a=="--pq-iters")c.pq_iters=std::stoi(v());
        else if(a=="--repeats")c.repeats=std::stoi(v());else if(a=="--seed")c.seed=std::stoull(v());else throw std::runtime_error("unknown option "+a);}
    if(c.base<1||c.cal_queries<2||c.test_queries<1||c.k<1||c.max_depth<1||c.max_depth>4||c.beam<1||c.target<=0||c.target>1||c.margin<0)throw std::runtime_error("invalid configuration");
    return c;
}

static std::vector<std::string> j_modes(int depth){
    if(depth==1)return {"pq16"};
    std::vector<std::string>m(depth,"pq24");m[0]="sq4";if(depth>=3)m.back()="pq16";return m;
}

static std::vector<int> j_counts(const std::vector<int>&exp){
    std::vector<int>c;for(int e:exp){if(e<0||e>=30)throw std::runtime_error("bad exponent");c.push_back(1<<e);}return c;
}

static std::vector<int> j_alpha_probes(const BIndex&t,double alpha,int k){
    std::vector<int>p(t.depth);p[0]=std::max(1,(int)std::lround(t.counts[0]*alpha));
    for(int l=1;l<t.depth;++l){int fan=t.counts[l]/t.counts[l-1],cap=std::min(t.counts[l],p[l-1]*fan);p[l]=std::max(1,(int)std::lround(cap*alpha));}
    return w_clamp_probes(t,p,k);
}

struct JPOpt {std::vector<int>p;double recall=0,cost=0,query_ms=0,seconds=0;int evaluations=0;};

static JPOpt j_optimize_probes(const BIndex&t,const Matrix&q,const std::vector<std::vector<int>>&gt,int k,double need){
    auto begin=std::chrono::steady_clock::now();std::map<std::vector<int>,WPoint>cache;
    auto eval=[&](std::vector<int>p){p=w_clamp_probes(t,p,k);auto it=cache.find(p);if(it!=cache.end())return it->second;WPoint x;x.p=p;try{auto s=w_query(t,q,p,k,&gt);auto r=w_route(t,s);x.recall=s.recall;x.cost=std::min(r.fixed,r.huffman)+w_leaf_proxy(t,s);x.query_ms=s.query_ms;}catch(const std::runtime_error&){x.recall=0;x.cost=std::numeric_limits<double>::infinity();}cache.emplace(p,x);return x;};
    WPoint best;bool have=false;double a=1.0/64.0;while(a<1.0){auto x=eval(j_alpha_probes(t,a,k));if(x.recall>=need&&(!have||x.cost<best.cost)){best=x;have=true;}a*=std::sqrt(2.0);}auto full=eval(j_alpha_probes(t,1.0,k));if(full.recall>=need&&(!have||full.cost<best.cost)){best=full;have=true;}if(!have)throw std::runtime_error("full probes failed recall");
    // Refine each coordinate by interval halving, deepest first.  This reaches
    // strongly non-uniform layer ratios in O(depth log K) evaluations.  One
    // bounded reallocation pass handles the remaining cross-level trade-off;
    // a final bisection removes any slack it introduced.
    for(int pass=0;pass<2;++pass){
        for(int l=t.depth-1;l>=0;--l){int lo=1,hi=best.p[l];WPoint local=best;while(lo<=hi){int mid=lo+(hi-lo)/2;auto p=best.p;p[l]=mid;auto x=eval(std::move(p));if(x.recall>=need){if(x.cost+0.5<local.cost)local=x;hi=mid-1;}else lo=mid+1;}if(local.cost+0.5<best.cost)best=local;}
        if(pass==0)for(double delta:{0.25,0.125,0.0625}){WPoint next=best;for(int shrink=0;shrink<t.depth;++shrink)for(int grow=0;grow<t.depth;++grow)if(grow!=shrink){auto p=best.p;p[shrink]=std::max(1,(int)std::floor(p[shrink]*(1.0-delta)));p[grow]=std::max(p[grow]+1,(int)std::ceil(p[grow]*(1.0+delta)));auto x=eval(std::move(p));if(x.recall>=need&&x.cost+0.5<next.cost)next=x;}best=next;}
    }
    JPOpt out;out.p=best.p;out.recall=best.recall;out.cost=best.cost;out.query_ms=best.query_ms;out.evaluations=cache.size();out.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();return out;
}

struct JResult {
    std::vector<int>exp,counts,p;double recall=0,proxy=0,query_ms=0,index_build_s=0,probe_search_s=0;int probe_evals=0;
};

struct JFine {std::vector<std::vector<int>>lists;Matrix center;double seconds=0;};

static JResult j_evaluate_structure(const Matrix&xb,const Matrix&calq,const std::vector<std::vector<int>>&gt,const JCfg&c,const std::vector<int>&exp,const JFine&fine){
    BCfg bc;bc.base=c.base;bc.counts=j_counts(exp);bc.modes=j_modes(exp.size());bc.iters=c.iters;bc.pq_train=c.pq_train;bc.pq_iters=c.pq_iters;bc.seed=c.seed;auto tree=b_build(xb,bc,&fine.lists,&fine.center);
    auto po=j_optimize_probes(tree,calq,gt,c.k,std::min(1.0,c.target+c.margin));JResult r;r.exp=exp;r.counts=bc.counts;r.p=po.p;r.recall=po.recall;r.proxy=po.cost;r.query_ms=po.query_ms;r.index_build_s=fine.seconds+tree.build_s;r.probe_search_s=po.seconds;r.probe_evals=po.evaluations;
    std::cerr<<"candidate counts="<<w_join(r.counts)<<" p="<<w_join(r.p)<<" recall="<<r.recall<<" proxy="<<r.proxy/1024.0<<" KiB evals="<<r.probe_evals<<"\n";return r;
}

static std::vector<std::vector<int>> j_refine(const std::vector<JResult>&beam,int depth){
    std::set<std::vector<int>>out;for(const auto&r:beam){const auto&s=r.exp;for(size_t pos=0;pos<s.size();++pos){int a=pos? s[pos-1]:0,b=s[pos];if(b-a<=1)continue;double mid=(a+b)/2.0;int radius=depth==2?2:1,center=(int)std::lround(mid);for(int off=-radius;off<=radius;++off){int e=center+off;if(e<=a||e>=b||e<3)continue;auto n=s;n.insert(n.begin()+pos,e);out.insert(n);}}}return {out.begin(),out.end()};
}

static bool j_better(const JResult&a,const JResult&b){if(a.proxy!=b.proxy)return a.proxy<b.proxy;if(a.recall!=b.recall)return a.recall>b.recall;return a.query_ms<b.query_ms;}

static std::string j_modes_string(int depth){auto m=j_modes(depth);std::ostringstream s;for(size_t i=0;i<m.size();++i){if(i)s<<'/';s<<m[i];}return s.str();}

#ifndef JOINT_VO_SUCCESSIVE_REFINEMENT_NO_MAIN
int main(int argc,char**argv)try{
    auto c=j_args(argc,argv);auto all_begin=std::chrono::steady_clock::now();Matrix xb=fvecs(c.data+"/sift_base.fvecs",c.base),calq=fvecs(c.data+"/sift_learn.fvecs",c.cal_queries),testq=fvecs(c.data+"/sift_query.fvecs",c.test_queries);auto calgt=exact_gt(xb,calq,c.k),testgt=exact_gt(xb,testq,c.k);
    int cap=std::min(c.base,c.max_fine),kf=1;while(kf*2<=cap)kf*=2;std::vector<int>fine;if(kf/2>=2048)fine.push_back(kf/2);fine.push_back(kf);
    std::map<int,JFine>fine_cache;std::vector<JResult>all;for(int K:fine){std::cerr<<"Prepare shared fine leaves="<<K<<"...\n";auto fb=std::chrono::steady_clock::now();JFine prepared;prepared.lists=hkmeans(xb,K,c.iters);prepared.center=cr_matrix(centers(xb,prepared.lists));prepared.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-fb).count();fine_cache.emplace(K,std::move(prepared));const auto&shared=fine_cache.at(K);int J=0;while((1<<J)<K)++J;std::vector<std::vector<int>>level{{J}};double previous_best=std::numeric_limits<double>::infinity();for(int depth=1;depth<=c.max_depth&&!level.empty();++depth){std::vector<JResult>rows;for(const auto&e:level)rows.push_back(j_evaluate_structure(xb,calq,calgt,c,e,shared));std::sort(rows.begin(),rows.end(),j_better);all.insert(all.end(),rows.begin(),rows.end());double gain=std::isfinite(previous_best)?(previous_best-rows.front().proxy)/previous_best:1.0;if(depth>1&&gain<0.01){std::cerr<<"stop depth refinement: gain="<<gain<<" < 0.01\n";break;}previous_best=rows.front().proxy;if(depth==c.max_depth)break;if((int)rows.size()>c.beam)rows.resize(c.beam);level=j_refine(rows,depth+1);}}
    if(all.empty())throw std::runtime_error("no structures");auto best=*std::min_element(all.begin(),all.end(),[](const auto&a,const auto&b){return j_better(a,b);});
    BCfg bc;bc.base=c.base;bc.counts=best.counts;bc.modes=j_modes(best.counts.size());bc.iters=c.iters;bc.pq_train=c.pq_train;bc.pq_iters=c.pq_iters;bc.seed=c.seed;const auto&selected_fine=fine_cache.at(best.counts.back());auto tree=b_build(xb,bc,&selected_fine.lists,&selected_fine.center);auto train=w_query(tree,calq,best.p,c.k,&calgt),test=w_query(tree,testq,best.p,c.k,&testgt);auto full=w_full(tree,calq,train,testq,test,c.repeats);
    double total_search_s=std::chrono::duration<double>(std::chrono::steady_clock::now()-all_begin).count();std::ofstream out(c.output);if(!out)throw std::runtime_error("cannot open output");out<<std::setprecision(12);out<<"selected,depth,counts,fanouts,codecs,probes,cal_recall,proxy_kib,cal_query_ms,index_build_s,probe_search_s,probe_evaluations,test_recall,routing_kib,leaf_kib,total_kib,test_query_ms,vo_generation_ms,object_violations,total_search_s\n";
    for(const auto&r:all){std::vector<int>fan;for(size_t l=1;l<r.counts.size();++l)fan.push_back(r.counts[l]/r.counts[l-1]);out<<(r.counts==best.counts)<<','<<r.counts.size()<<','<<w_join(r.counts)<<','<<w_join(fan)<<','<<j_modes_string(r.counts.size())<<','<<w_join(r.p)<<','<<r.recall<<','<<r.proxy/1024.0<<','<<r.query_ms<<','<<r.index_build_s<<','<<r.probe_search_s<<','<<r.probe_evals;if(r.counts==best.counts)out<<','<<test.recall<<','<<full.route<<','<<full.leaf<<','<<full.total<<','<<test.query_ms<<','<<full.vo_ms<<','<<full.violations<<','<<total_search_s;out<<'\n';}
    std::cout<<"SELECTED counts="<<w_join(best.counts)<<" probes="<<w_join(best.p)<<" cal="<<train.recall<<" test="<<test.recall<<" route="<<full.route<<" leaf="<<full.leaf<<" total="<<full.total<<" KiB query="<<test.query_ms<<" ms VOgen="<<full.vo_ms<<" ms build="<<selected_fine.seconds+tree.build_s<<" s search="<<total_search_s<<" s violations="<<full.violations<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
#endif

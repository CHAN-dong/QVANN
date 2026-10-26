#define JOINT_VO_SUCCESSIVE_REFINEMENT_NO_MAIN
#include "joint_vo_successive_refinement.cpp"
#undef JOINT_VO_SUCCESSIVE_REFINEMENT_NO_MAIN

#include <unordered_map>

// Recall-constrained VO lower-bound pruning.
//
// For each calibration query, the lower bound is allowed to pick arbitrary
// fine lists containing ground-truth neighbours. It pays every object in
// those lists plus an optimistic minimum routing cost, but pays no Merkle
// proofs and no wrong-list visits. This relaxation can only be cheaper than
// a real stagewise query satisfying the same aggregate Recall target.
//
// At a fixed depth, once `beam` candidates have been fully evaluated, a new
// candidate is skipped only when its lower bound cannot enter the current
// top-beam. Thus pruning preserves the baseline beam at that depth whenever
// the lower bound is admissible.

struct HCfg {
    JCfg j;
    bool pruning=true;
    bool empirical_screen=true;
    double screen_safety=0.05;
    double gray_zone=0.02;
};

static HCfg h_args(int argc,char**argv){
    HCfg h;auto&c=h.j;
    for(int i=1;i<argc;++i){std::string a=argv[i];auto v=[&](){if(++i==argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
        if(a=="--dataset")c.data=v();else if(a=="--output")c.output=v();else if(a=="--base")c.base=std::stoi(v());
        else if(a=="--cal-queries")c.cal_queries=std::stoi(v());else if(a=="--test-queries")c.test_queries=std::stoi(v());
        else if(a=="--k")c.k=std::stoi(v());else if(a=="--max-fine")c.max_fine=std::stoi(v());else if(a=="--max-depth")c.max_depth=std::stoi(v());
        else if(a=="--beam")c.beam=std::stoi(v());else if(a=="--target")c.target=std::stod(v());else if(a=="--margin")c.margin=std::stod(v());
        else if(a=="--iters")c.iters=std::stoi(v());else if(a=="--pq-train")c.pq_train=std::stoi(v());else if(a=="--pq-iters")c.pq_iters=std::stoi(v());
        else if(a=="--repeats")c.repeats=std::stoi(v());else if(a=="--seed")c.seed=std::stoull(v());
        else if(a=="--no-pruning")h.pruning=false;else if(a=="--pruning")h.pruning=true;
        else if(a=="--no-empirical-screen")h.empirical_screen=false;else if(a=="--empirical-screen")h.empirical_screen=true;
        else if(a=="--screen-safety")h.screen_safety=std::stod(v());else if(a=="--gray-zone")h.gray_zone=std::stod(v());else throw std::runtime_error("unknown option "+a);
    }
    if(c.base<1||c.cal_queries<2||c.test_queries<1||c.k<1||c.max_depth<1||c.max_depth>4||c.beam<1||c.target<=0||c.target>1||c.margin<0||h.screen_safety<0||h.screen_safety>=1||h.gray_zone<0)throw std::runtime_error("invalid configuration");
    return h;
}

struct HQDP {
    int max_lists=0,k=0;
    std::vector<double> cost;
    double at(int lists,int hits)const{return cost[(size_t)lists*(k+1)+hits];}
};

struct HBoundCache {
    int nq=0,k=0;
    std::vector<int>owner;
    std::vector<HQDP> query;
};

static HBoundCache h_prepare_bound_cache(const Matrix&xb,const std::vector<std::vector<int>>&fine_lists,
                                         const std::vector<std::vector<int>>&gt,int k,double leaf_unit){
    HBoundCache out;out.nq=gt.size();out.k=k;out.query.reserve(gt.size());
    out.owner.assign(xb.rows(),-1);for(size_t f=0;f<fine_lists.size();++f)for(int id:fine_lists[f])out.owner[id]=f;
    for(size_t qi=0;qi<gt.size();++qi){
        std::unordered_map<int,int>hits_by_list;for(int z=0;z<k&&z<(int)gt[qi].size();++z){int id=gt[qi][z];if(id<0||id>=(int)out.owner.size()||out.owner[id]<0)throw std::runtime_error("ground-truth object missing from fine lists");++hits_by_list[out.owner[id]];}
        std::vector<std::pair<int,double>>actions;actions.reserve(hits_by_list.size());for(const auto&x:hits_by_list)actions.push_back({x.second,fine_lists[x.first].size()*leaf_unit});
        HQDP q;q.max_lists=actions.size();q.k=k;q.cost.assign((size_t)(q.max_lists+1)*(k+1),std::numeric_limits<double>::infinity());q.cost[0]=0;
        int used=0;for(const auto&a:actions){for(int m=used;m>=0;--m)for(int h0=k;h0>=0;--h0){double old=q.cost[(size_t)m*(k+1)+h0];if(!std::isfinite(old))continue;int h1=std::min(k,h0+a.first);double&dst=q.cost[(size_t)(m+1)*(k+1)+h1];dst=std::min(dst,old+a.second);}++used;}
        out.query.push_back(std::move(q));
    }
    return out;
}

// Exact proxy evaluator: with exact scanning inside selected posting lists, a
// global top-k object is recalled iff its posting list is selected. Therefore
// calibration Recall and visited-object proxy can be computed without the
// repeated O(visited_objects * d) leaf distance scans used by w_query().
static WStates h_query_proxy(const BIndex&t,const Matrix&q,const std::vector<int>&probe,int k,
                            const std::vector<std::vector<int>>&gt,const std::vector<int>&owner){
    WStates out;out.trace.resize(q.rows());out.selected.assign(t.depth,0);uint64_t hits=0;double routing_ns=0;auto all_begin=std::chrono::steady_clock::now();
    for(size_t qi=0;qi<q.rows();++qi){auto route_begin=std::chrono::steady_clock::now();auto&tr=out.trace[qi];tr.selected.resize(t.depth);auto lut0=t.codec[0]->prepare(q[qi]);std::vector<std::pair<float,int>>cand(t.counts[0]);
        for(int id=0;id<t.counts[0];++id)cand[id]={t.codec[0]->distance(q[qi],lut0,t.encoded[0],id,false),id};int take=std::min<int>(probe[0],cand.size());std::partial_sort(cand.begin(),cand.begin()+take,cand.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});
        std::vector<int>previous;previous.reserve(take);std::vector<float>previous_decoded;previous_decoded.reserve((size_t)take*t.d);for(int z=0;z<take;++z){int id=cand[z].second;previous.push_back(id);tr.selected[0].push_back(id);auto d=t.codec[0]->decode(t.encoded[0],id);previous_decoded.insert(previous_decoded.end(),d.begin(),d.end());}out.generated+=t.counts[0];if(t.depth==1)tr.leaf.lists=previous;
        for(int l=1;l<t.depth;++l){std::vector<std::pair<float,int>>next;for(size_t z=0;z<previous.size();++z){std::vector<float>qr(t.d);const float*parent=previous_decoded.data()+z*t.d;for(int j=0;j<t.d;++j)qr[j]=q[qi][j]-parent[j];auto lut=t.codec[l]->prepare(qr.data());for(int child:t.children[l-1][previous[z]])next.push_back({t.codec[l]->distance(qr.data(),lut,t.encoded[l],child,false),child});}
            out.generated+=next.size();take=std::min<int>(probe[l],next.size());std::partial_sort(next.begin(),next.begin()+take,next.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});std::vector<int>selected;selected.reserve(take);for(int z=0;z<take;++z){selected.push_back(next[z].second);tr.selected[l].push_back(next[z].second);}if(l==t.depth-1){tr.leaf.lists=selected;break;}
            std::vector<int>slot(t.counts[l-1],-1);for(size_t z=0;z<previous.size();++z)slot[previous[z]]=z;std::vector<float>decoded((size_t)take*t.d);for(int z=0;z<take;++z){int id=selected[z],ps=slot[t.parent[l][id]];auto residual=t.codec[l]->decode(t.encoded[l],id);const float*parent=previous_decoded.data()+(size_t)ps*t.d;for(int j=0;j<t.d;++j)decoded[(size_t)z*t.d+j]=parent[j]+residual[j];}previous.swap(selected);previous_decoded.swap(decoded);}
        routing_ns+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-route_begin).count();size_t candidate_objects=0;std::unordered_set<int>selected_lists;for(int f:tr.leaf.lists){candidate_objects+=t.lists[f].size();selected_lists.insert(f);}if(candidate_objects<(size_t)k)throw std::runtime_error("stagewise probes returned fewer than k objects");
        for(int z=0;z<k&&z<(int)gt[qi].size();++z)hits+=selected_lists.count(owner[gt[qi][z]]);out.visited+=candidate_objects;for(int l=0;l<t.depth;++l)out.selected[l]+=tr.selected[l].size();}
    double nq=q.rows();out.query_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-all_begin).count()/nq;out.routing_ms=routing_ns/nq/1e6;out.recall=hits/(nq*k);out.visited/=nq;out.generated/=nq;for(double&x:out.selected)x/=nq;return out;
}

static JPOpt h_optimize_probes(const BIndex&t,const Matrix&q,const std::vector<std::vector<int>>&gt,
                              const std::vector<int>&owner,int k,double need,double cutoff=std::numeric_limits<double>::infinity(),
                              double screen_ratio=0,bool*screened=nullptr,double*coarse_upper=nullptr){
    auto begin=std::chrono::steady_clock::now();std::map<std::vector<int>,WPoint>cache;
    auto eval=[&](std::vector<int>p){p=w_clamp_probes(t,p,k);auto it=cache.find(p);if(it!=cache.end())return it->second;WPoint x;x.p=p;try{auto s=h_query_proxy(t,q,p,k,gt,owner);auto r=w_route(t,s);x.recall=s.recall;x.cost=std::min(r.fixed,r.huffman)+w_leaf_proxy(t,s);x.query_ms=s.query_ms;}catch(const std::runtime_error&){x.recall=0;x.cost=std::numeric_limits<double>::infinity();}cache.emplace(p,x);return x;};
    WPoint best;bool have=false;double a=1.0/64.0;while(a<1.0){auto x=eval(j_alpha_probes(t,a,k));if(x.recall>=need&&(!have||x.cost<best.cost)){best=x;have=true;}a*=std::sqrt(2.0);}auto full=eval(j_alpha_probes(t,1.0,k));if(full.recall>=need&&(!have||full.cost<best.cost)){best=full;have=true;}if(!have)throw std::runtime_error("full probes failed recall");if(screened)*screened=false;
    for(int pass=0;pass<2;++pass){for(int l=t.depth-1;l>=0;--l){int lo=1,hi=best.p[l];WPoint local=best;while(lo<=hi){int mid=lo+(hi-lo)/2;auto p=best.p;p[l]=mid;auto x=eval(std::move(p));if(x.recall>=need){if(x.cost+0.5<local.cost)local=x;hi=mid-1;}else lo=mid+1;}if(local.cost+0.5<best.cost)best=local;}if(pass==0){for(double delta:{0.25,0.125,0.0625}){WPoint next=best;for(int shrink=0;shrink<t.depth;++shrink)for(int grow=0;grow<t.depth;++grow)if(grow!=shrink){auto p=best.p;p[shrink]=std::max(1,(int)std::floor(p[shrink]*(1.0-delta)));p[grow]=std::max(p[grow]+1,(int)std::ceil(p[grow]*(1.0+delta)));auto x=eval(std::move(p));if(x.recall>=need&&x.cost+0.5<next.cost)next=x;}best=next;}if(coarse_upper)*coarse_upper=best.cost;if(screen_ratio>0&&std::isfinite(cutoff)&&screen_ratio*best.cost>=cutoff){if(screened)*screened=true;JPOpt out;out.p=best.p;out.recall=best.recall;out.cost=best.cost;out.query_ms=best.query_ms;out.evaluations=cache.size();out.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();return out;}}}
    JPOpt out;out.p=best.p;out.recall=best.recall;out.cost=best.cost;out.query_ms=best.query_ms;out.evaluations=cache.size();out.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();return out;
}

struct HRouteLowerBound {
    double static_bytes=0;
    std::vector<double>minimum_node_cost;
    std::vector<double>by_terminal_lists;
};

static HRouteLowerBound h_route_lower_bound(const BIndex&t,int max_lists){
    HRouteLowerBound out;
    double params=96.0;for(const auto&x:t.codec)params+=x->parameter_bytes();
    std::vector<int>roots(t.counts[0]);std::iota(roots.begin(),roots.end(),0);
    double fixed=params+t.counts[0]*(double)t.codec[0]->code_bytes();
    double huffman=params+16.0;for(const auto&x:t.codec)huffman+=x->huffman_model_bytes();huffman+=t.codec[0]->huffman_bytes(roots);
    out.static_bytes=std::min(fixed,huffman);

    std::vector<int>max_capacity(t.depth,1);std::vector<std::vector<int>>capacity(t.depth);capacity.back().assign(t.counts.back(),1);
    std::vector<double>minimum_node_cost(std::max(0,t.depth-1),0);
    for(int l=t.depth-2;l>=0;--l){
        capacity[l].assign(t.counts[l],0);double minimum=std::numeric_limits<double>::infinity();
        for(int p=0;p<t.counts[l];++p){
            for(int child:t.children[l][p])capacity[l][p]+=capacity[l+1][child];
            double raw=t.children[l][p].size()*(double)t.codec[l+1]->code_bytes();
            double entropy=t.codec[l+1]->huffman_bytes(t.children[l][p]);
            minimum=std::min(minimum,68.0+std::min(raw,entropy));
        }
        max_capacity[l]=*std::max_element(capacity[l].begin(),capacity[l].end());minimum_node_cost[l]=minimum;
    }
    out.minimum_node_cost=minimum_node_cost;out.by_terminal_lists.resize(max_lists+1);for(int m=1;m<=max_lists;++m){
        double z=4.0*m;for(int l=0;l<t.depth-1;++l){int nodes=(m+max_capacity[l]-1)/max_capacity[l];z+=nodes*minimum_node_cost[l];}out.by_terminal_lists[m]=z;
    }
    return out;
}

static double h_recall_vo_lower_bound(const BIndex&t,const HBoundCache&cache,double need){
    int total=cache.nq*cache.k,required=(int)std::ceil(need*total-1e-12),allowed_misses=total-required;if(allowed_misses<0)allowed_misses=0;
    auto route=h_route_lower_bound(t,cache.k);const double inf=std::numeric_limits<double>::infinity();
    std::vector<double>global(allowed_misses+1,inf);global[0]=0;
    for(const auto&q:cache.query){
        std::vector<double>qcost(allowed_misses+1,inf);
        for(int miss=0;miss<=allowed_misses;++miss){int need_hits=std::max(0,cache.k-miss);double best=inf;
            for(int m=0;m<=q.max_lists&&m<(int)route.by_terminal_lists.size();++m)for(int hits=need_hits;hits<=cache.k;++hits){double leaf=q.at(m,hits);if(std::isfinite(leaf))best=std::min(best,leaf+route.by_terminal_lists[m]);}qcost[miss]=best;}
        std::vector<double>next(allowed_misses+1,inf);for(int used=0;used<=allowed_misses;++used)if(std::isfinite(global[used]))for(int give=0;used+give<=allowed_misses;++give)if(std::isfinite(qcost[give]))next[used+give]=std::min(next[used+give],global[used]+qcost[give]);global.swap(next);
    }
    double variable=*std::min_element(global.begin(),global.end());if(!std::isfinite(variable))throw std::runtime_error("recall lower-bound DP found no feasible relaxation");return route.static_bytes+variable/cache.nq;
}

// A second admissible bound uses necessary local ranks. If a ground-truth
// leaf is recovered, its ancestor at every level must beat all better siblings.
// With at most M aggregate misses, p_l must cover the H-th smallest such rank.
// Global competition can only increase the required probe count, so sibling
// rank is optimistic. The byte bound then charges the cheapest possible nodes
// and the globally smallest fine lists.
static double h_rank_envelope_lower_bound(const BIndex&t,const Matrix&q,const std::vector<std::vector<int>>&gt,
                                          const std::vector<int>&owner,int k,double need,std::vector<int>*probes_out=nullptr){
    std::vector<Matrix>decoded(t.depth);decoded[0].d=t.d;decoded[0].x.resize((size_t)t.counts[0]*t.d);for(int id=0;id<t.counts[0];++id){auto v=t.codec[0]->decode(t.encoded[0],id);std::copy(v.begin(),v.end(),decoded[0].x.begin()+(size_t)id*t.d);}
    for(int l=1;l<t.depth;++l){decoded[l].d=t.d;decoded[l].x.resize((size_t)t.counts[l]*t.d);for(int id=0;id<t.counts[l];++id){auto v=t.codec[l]->decode(t.encoded[l],id);const float*p=decoded[l-1][t.parent[l][id]];for(int j=0;j<t.d;++j)decoded[l].x[(size_t)id*t.d+j]=p[j]+v[j];}}
    std::vector<std::vector<int>>ranks(t.depth);for(auto&x:ranks)x.reserve(q.rows()*k);
    for(size_t qi=0;qi<q.rows();++qi){
        std::unordered_map<int,int>fine_hits;for(int z=0;z<k&&z<(int)gt[qi].size();++z)++fine_hits[owner[gt[qi][z]]];
        std::vector<std::unordered_map<int,int>>node_hits(t.depth);for(const auto&x:fine_hits){int id=x.first;node_hits.back()[id]+=x.second;for(int l=t.depth-1;l>0;--l){id=t.parent[l][id];node_hits[l-1][id]+=x.second;}}
        auto lut0=t.codec[0]->prepare(q[qi]);std::vector<std::pair<float,int>>score0(t.counts[0]);for(int id=0;id<t.counts[0];++id)score0[id]={t.codec[0]->distance(q[qi],lut0,t.encoded[0],id,false),id};std::sort(score0.begin(),score0.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});std::vector<int>pos0(t.counts[0]);for(size_t z=0;z<score0.size();++z)pos0[score0[z].second]=z+1;for(const auto&x:node_hits[0])for(int z=0;z<x.second;++z)ranks[0].push_back(pos0[x.first]);
        for(int l=1;l<t.depth;++l){std::unordered_map<int,std::vector<std::pair<int,int>>>targets;for(const auto&x:node_hits[l])targets[t.parent[l][x.first]].push_back(x);for(const auto&g:targets){int parent=g.first;std::vector<float>qr(t.d);for(int j=0;j<t.d;++j)qr[j]=q[qi][j]-decoded[l-1][parent][j];auto lut=t.codec[l]->prepare(qr.data());std::vector<std::pair<float,int>>scores;for(int child:t.children[l-1][parent])scores.push_back({t.codec[l]->distance(qr.data(),lut,t.encoded[l],child,false),child});std::sort(scores.begin(),scores.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});std::unordered_map<int,int>position;for(size_t z=0;z<scores.size();++z)position[scores[z].second]=z+1;for(const auto&x:g.second)for(int z=0;z<x.second;++z)ranks[l].push_back(position[x.first]);}}
    }
    int total=q.rows()*k,required=(int)std::ceil(need*total-1e-12);std::vector<int>pmin(t.depth);for(int l=0;l<t.depth;++l){if((int)ranks[l].size()!=total)throw std::runtime_error("rank envelope lost ground-truth items");std::nth_element(ranks[l].begin(),ranks[l].begin()+required-1,ranks[l].end());pmin[l]=ranks[l][required-1];}
    auto route=h_route_lower_bound(t,k);double cost=route.static_bytes+4.0*pmin.back();for(int l=0;l<t.depth-1;++l)cost+=pmin[l]*route.minimum_node_cost[l];std::vector<int>sizes;for(const auto&list:t.lists)sizes.push_back(list.size());std::nth_element(sizes.begin(),sizes.begin()+std::min<int>(pmin.back(),sizes.size()),sizes.end());std::sort(sizes.begin(),sizes.begin()+std::min<int>(pmin.back(),sizes.size()));for(int z=0;z<pmin.back()&&z<(int)sizes.size();++z)cost+=sizes[z]*t.leaf_unit;if(probes_out)*probes_out=pmin;return cost;
}

struct HRow {
    std::vector<int>exp,counts;
    bool pruned=false;
    std::string reason="evaluated";
    double lower_bound=0,cutoff=std::numeric_limits<double>::infinity(),coarse_upper=std::numeric_limits<double>::quiet_NaN(),empirical_lower=std::numeric_limits<double>::quiet_NaN(),screen_ratio=0,index_build_s=0,bound_s=0,probe_search_s=0;
    int probe_evals=0;
    JResult result;
};

static double h_beam_cutoff(const std::vector<JResult>&rows,int beam){
    if((int)rows.size()<beam)return std::numeric_limits<double>::infinity();auto copy=rows;std::sort(copy.begin(),copy.end(),j_better);return copy[beam-1].proxy;
}

static std::string h_csv_num(double x){if(!std::isfinite(x))return "";std::ostringstream s;s<<std::setprecision(12)<<x;return s.str();}

int main(int argc,char**argv)try{
    auto hc=h_args(argc,argv);auto&c=hc.j;auto all_begin=std::chrono::steady_clock::now();
    Matrix xb=fvecs(c.data+"/sift_base.fvecs",c.base),calq=fvecs(c.data+"/sift_learn.fvecs",c.cal_queries),testq=fvecs(c.data+"/sift_query.fvecs",c.test_queries);auto calgt=exact_gt(xb,calq,c.k),testgt=exact_gt(xb,testq,c.k);
    int cap=std::min(c.base,c.max_fine),kf=1;while(kf*2<=cap)kf*=2;std::vector<int>fine;if(kf/2>=2048)fine.push_back(kf/2);fine.push_back(kf);
    std::map<int,JFine>fine_cache;std::map<int,HBoundCache>bound_cache;std::vector<JResult>all;std::vector<HRow>log;int built=0,fully_evaluated=0,pruned=0;long probe_evals=0;double bound_seconds=0;
    for(int K:fine){
        std::cerr<<"Prepare shared fine leaves="<<K<<"...\n";auto fb=std::chrono::steady_clock::now();JFine prepared;prepared.lists=hkmeans(xb,K,c.iters);prepared.center=cr_matrix(centers(xb,prepared.lists));prepared.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-fb).count();fine_cache.emplace(K,std::move(prepared));const auto&shared=fine_cache.at(K);
        double leaf_unit=xb.d/2.0+32.0+4.0+8.0;bound_cache.emplace(K,h_prepare_bound_cache(xb,shared.lists,calgt,c.k,leaf_unit));const auto&bcache=bound_cache.at(K);
        int J=0;while((1<<J)<K)++J;std::vector<std::vector<int>>level{{J}};double previous_best=std::numeric_limits<double>::infinity();
        for(int depth=1;depth<=c.max_depth&&!level.empty();++depth){
            std::vector<JResult>rows;std::vector<double>observed_ratios;
            for(const auto&e:level){
                BCfg bc;bc.base=c.base;bc.counts=j_counts(e);bc.modes=j_modes(e.size());bc.iters=c.iters;bc.pq_train=c.pq_train;bc.pq_iters=c.pq_iters;bc.seed=c.seed;
                auto ib=std::chrono::steady_clock::now();auto tree=b_build(xb,bc,&shared.lists,&shared.center);double index_s=std::chrono::duration<double>(std::chrono::steady_clock::now()-ib).count();++built;
                auto bb=std::chrono::steady_clock::now();double need=std::min(1.0,c.target+c.margin);double lower=h_recall_vo_lower_bound(tree,bcache,need);std::vector<int>rank_probes;lower=std::max(lower,h_rank_envelope_lower_bound(tree,calq,calgt,bcache.owner,c.k,need,&rank_probes));double bound_s=std::chrono::duration<double>(std::chrono::steady_clock::now()-bb).count();bound_seconds+=bound_s;
                double cutoff=h_beam_cutoff(rows,c.beam);bool skip=hc.pruning&&std::isfinite(cutoff)&&lower>=cutoff-1e-7;
                HRow lr;lr.exp=e;lr.counts=bc.counts;lr.lower_bound=lower;lr.cutoff=cutoff;lr.index_build_s=shared.seconds+index_s;lr.bound_s=bound_s;lr.pruned=skip;
                if(skip){++pruned;lr.reason="strict_bound";std::cerr<<"prune counts="<<w_join(bc.counts)<<" lower="<<lower/1024.0<<" KiB cutoff="<<cutoff/1024.0<<" KiB\n";log.push_back(std::move(lr));continue;}
                double ratio=0;if(hc.empirical_screen&&(int)observed_ratios.size()>=c.beam)ratio=std::max(0.0,*std::min_element(observed_ratios.begin(),observed_ratios.end())-hc.screen_safety);bool screened=false;double coarse=std::numeric_limits<double>::quiet_NaN();double guarded_cutoff=std::isfinite(cutoff)?cutoff*(1.0+hc.gray_zone):cutoff;auto po=h_optimize_probes(tree,calq,calgt,bcache.owner,c.k,std::min(1.0,c.target+c.margin),guarded_cutoff,ratio,&screened,&coarse);lr.coarse_upper=coarse;lr.screen_ratio=ratio;lr.empirical_lower=ratio*coarse;lr.probe_search_s=po.seconds;lr.probe_evals=po.evaluations;probe_evals+=po.evaluations;
                if(screened){++pruned;lr.pruned=true;lr.reason="empirical_screen";std::cerr<<"screen counts="<<w_join(bc.counts)<<" empirical_lower="<<lr.empirical_lower/1024.0<<" KiB cutoff="<<cutoff/1024.0<<" KiB ratio="<<ratio<<" evals="<<po.evaluations<<"\n";log.push_back(std::move(lr));continue;}
                JResult r;r.exp=e;r.counts=bc.counts;r.p=po.p;r.recall=po.recall;r.proxy=po.cost;r.query_ms=po.query_ms;r.index_build_s=shared.seconds+tree.build_s;r.probe_search_s=po.seconds;r.probe_evals=po.evaluations;
                if(lower>r.proxy+1e-6)throw std::runtime_error("lower-bound violation on "+w_join(r.counts));
                ++fully_evaluated;observed_ratios.push_back(r.proxy/coarse);rows.push_back(r);all.push_back(r);lr.result=r;log.push_back(std::move(lr));
                std::cerr<<"candidate counts="<<w_join(r.counts)<<" p="<<w_join(r.p)<<" recall="<<r.recall<<" lower="<<lower/1024.0<<" proxy="<<r.proxy/1024.0<<" KiB evals="<<r.probe_evals<<"\n";
            }
            if(rows.empty())break;std::sort(rows.begin(),rows.end(),j_better);double gain=std::isfinite(previous_best)?(previous_best-rows.front().proxy)/previous_best:1.0;if(depth>1&&gain<0.01){std::cerr<<"stop depth refinement: gain="<<gain<<" < 0.01\n";break;}previous_best=rows.front().proxy;if(depth==c.max_depth)break;if((int)rows.size()>c.beam)rows.resize(c.beam);level=j_refine(rows,depth+1);
        }
    }
    if(all.empty())throw std::runtime_error("no structures");auto best=*std::min_element(all.begin(),all.end(),[](const auto&a,const auto&b){return j_better(a,b);});
    BCfg bc;bc.base=c.base;bc.counts=best.counts;bc.modes=j_modes(best.counts.size());bc.iters=c.iters;bc.pq_train=c.pq_train;bc.pq_iters=c.pq_iters;bc.seed=c.seed;const auto&selected_fine=fine_cache.at(best.counts.back());auto tree=b_build(xb,bc,&selected_fine.lists,&selected_fine.center);auto train=w_query(tree,calq,best.p,c.k,&calgt),test=w_query(tree,testq,best.p,c.k,&testgt);auto full=w_full(tree,calq,train,testq,test,c.repeats);
    double total_search_s=std::chrono::duration<double>(std::chrono::steady_clock::now()-all_begin).count();std::ofstream out(c.output);if(!out)throw std::runtime_error("cannot open output");out<<std::setprecision(12);out<<"selected,status,depth,counts,fanouts,codecs,probes,cal_recall,strict_lower_kib,coarse_upper_kib,screen_ratio,empirical_lower_kib,beam_cutoff_kib,proxy_kib,bound_gap,index_build_s,bound_s,probe_search_s,probe_evaluations,test_recall,routing_kib,leaf_kib,total_kib,test_query_ms,vo_generation_ms,object_violations,structures_built,structures_evaluated,structures_pruned,total_probe_evaluations,total_bound_s,total_search_s\n";
    for(const auto&r:log){std::vector<int>fan;for(size_t l=1;l<r.counts.size();++l)fan.push_back(r.counts[l]/r.counts[l-1]);bool selected=!r.pruned&&r.result.counts==best.counts;out<<selected<<','<<r.reason<<','<<r.counts.size()<<','<<w_join(r.counts)<<','<<w_join(fan)<<','<<j_modes_string(r.counts.size())<<',';
        if(!r.pruned)out<<w_join(r.result.p)<<','<<r.result.recall;else out<<',';out<<','<<r.lower_bound/1024.0<<','<<h_csv_num(r.coarse_upper/1024.0)<<','<<r.screen_ratio<<','<<h_csv_num(r.empirical_lower/1024.0)<<','<<h_csv_num(r.cutoff/1024.0)<<',';if(!r.pruned)out<<r.result.proxy/1024.0<<','<<(r.result.proxy-r.lower_bound)/r.result.proxy;else out<<',';out<<','<<r.index_build_s<<','<<r.bound_s<<','<<r.probe_search_s<<','<<r.probe_evals;
        if(selected)out<<','<<test.recall<<','<<full.route<<','<<full.leaf<<','<<full.total<<','<<test.query_ms<<','<<full.vo_ms<<','<<full.violations<<','<<built<<','<<fully_evaluated<<','<<pruned<<','<<probe_evals<<','<<bound_seconds<<','<<total_search_s;out<<'\n';}
    std::cout<<"SELECTED counts="<<w_join(best.counts)<<" probes="<<w_join(best.p)<<" cal="<<train.recall<<" test="<<test.recall<<" route="<<full.route<<" leaf="<<full.leaf<<" total="<<full.total<<" KiB query="<<test.query_ms<<" ms VOgen="<<full.vo_ms<<" ms build="<<selected_fine.seconds+tree.build_s<<" s search="<<total_search_s<<" s built="<<built<<" evaluated="<<fully_evaluated<<" pruned="<<pruned<<" probe_evals="<<probe_evals<<" bound_s="<<bound_seconds<<" violations="<<full.violations<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

#define RESIDUAL_HIER_VO_NO_MAIN
#include "residual_hier_vo.cpp"
#undef RESIDUAL_HIER_VO_NO_MAIN
#include <sys/resource.h>

// Simple workload-log-free index synthesizer.  Its VO objective is structural;
// an independent learn set is used only to enforce the statistical Recall target.
// The official query set is evaluated once per selected structure and is never
// used to choose depth, branching, or probes.

struct ACfg {
    std::string data="../dataset/sift",output="auto_vo_tree.csv";
    int base=100000,cal_queries=80,test_queries=200,k=100,max_fine=65536,max_depth=4,iters=4,pq_train=12000,pq_iters=4;
    double target=0.99,margin=0.002,probe_guard=0.08,latency_budget_ms=1e100,build_budget_s=1e100;uint64_t seed=20260830;
};

static ACfg a_args(int argc,char**argv){
    ACfg c;for(int i=1;i<argc;++i){std::string a=argv[i];auto v=[&](){if(++i==argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
        if(a=="--dataset")c.data=v();else if(a=="--output")c.output=v();else if(a=="--base")c.base=std::stoi(v());
        else if(a=="--cal-queries")c.cal_queries=std::stoi(v());else if(a=="--test-queries")c.test_queries=std::stoi(v());else if(a=="--k")c.k=std::stoi(v());
        else if(a=="--max-fine")c.max_fine=std::stoi(v());else if(a=="--iters")c.iters=std::stoi(v());else if(a=="--pq-train")c.pq_train=std::stoi(v());
        else if(a=="--pq-iters")c.pq_iters=std::stoi(v());else if(a=="--target")c.target=std::stod(v());else if(a=="--margin")c.margin=std::stod(v());
        else if(a=="--max-depth")c.max_depth=std::stoi(v());else if(a=="--probe-guard")c.probe_guard=std::stod(v());else if(a=="--latency-budget-ms")c.latency_budget_ms=std::stod(v());
        else if(a=="--build-budget-s")c.build_budget_s=std::stod(v());
        else if(a=="--seed")c.seed=std::stoull(v());else throw std::runtime_error("unknown option "+a);}
    if(c.base<2048||c.cal_queries<1||c.test_queries<1||c.k<1||c.target<=0||c.target>1||c.margin<0||c.probe_guard<0||c.probe_guard>0.5||c.max_depth<1||c.max_depth>4||c.latency_budget_ms<=0||c.build_budget_s<=0)throw std::runtime_error("invalid configuration");return c;
}

static std::vector<int> a_fracs(int n,std::initializer_list<double>f){
    std::vector<int>v;for(double x:f)v.push_back(std::max(1,std::min(n,(int)std::lround(n*x))));std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end()),v.end());return v;
}

static double a_visited(const std::vector<std::vector<int>>&lists,const std::vector<VLState>&s){
    double n=0;for(const auto&q:s)for(int l:q.lists)n+=lists[l].size();return n/s.size();
}

static double a_leaf_proxy(const Matrix&q,double visited){
    // Stateless conservative proxy: SQ4 base record + one SHA sibling +
    // positional id/framing per visited object.  Final selected schemes are
    // rerun with the complete progressive leaf protocol.
    return visited*(q.d/2.0+32.0+4.0+8.0);
}

static double a_flat_route(const CRStates&s,int nlist,const CRCodec&codec){
    std::vector<int>all(nlist);std::iota(all.begin(),all.end(),0);double parameters=96.0+codec.parameter_bytes(),variable=0;
    for(const auto&q:s.state)variable+=q.lists.size()*4.0+hv_dummy_proof_count(nlist,q.lists)*32.0;variable/=s.state.size();
    return std::min(parameters+nlist*(double)codec.code_bytes()+variable,parameters+codec.huffman_model_bytes()+codec.huffman_bytes(all)+16.0+variable);
}

static RHLayout a_identity2(const std::vector<std::vector<int>>&groups,int nlist){
    RHLayout x;x.coarse_position.resize(groups.size());std::iota(x.coarse_position.begin(),x.coarse_position.end(),0);x.fine_position.assign(nlist,-1);
    for(size_t g=0;g<groups.size();++g)for(size_t p=0;p<groups[g].size();++p)x.fine_position[groups[g][p]]=p;return x;
}

struct A3State {
    std::vector<VLState>leaf;std::vector<std::vector<int>>coarse,mid;
    double recall=0,query_ms=0,mid_candidates=0,fine_candidates=0;
};

static A3State a3_states(const Matrix&xb,const Matrix&q,const std::vector<std::vector<int>>&lists,
        const std::vector<std::vector<int>>&mid_groups,const std::vector<int>&mid_of_fine,
        const std::vector<std::vector<int>>&coarse_groups,const std::vector<int>&coarse_of_mid,
        const Matrix&coarse_center,const Matrix&mid_residual,const Matrix&fine_residual,
        const CRCodec&cc,const CRCodec&mc,const CRCodec&fc,int cp,int mp,int fp,int k,
        const std::vector<std::vector<int>>*gt){
    A3State out;out.leaf.resize(q.rows());out.coarse.resize(q.rows());out.mid.resize(q.rows());uint64_t hits=0,midcan=0,finecan=0;auto begin=std::chrono::steady_clock::now();
    for(size_t qi=0;qi<q.rows();++qi){auto clut=cc.prepare(q[qi]);std::vector<int>corder(coarse_groups.size());std::iota(corder.begin(),corder.end(),0);
        std::partial_sort(corder.begin(),corder.begin()+cp,corder.end(),[&](int a,int b){float x=cc.distance(q[qi],clut,coarse_center,a,false),y=cc.distance(q[qi],clut,coarse_center,b,false);return x==y?a<b:x<y;});
        out.coarse[qi].assign(corder.begin(),corder.begin()+cp);std::vector<std::vector<float>>dc(coarse_groups.size());std::vector<std::pair<float,int>>mcan;
        for(int z=0;z<cp;++z){int g=corder[z];dc[g]=cc.decode(coarse_center,g);std::vector<float>qr(q.d);for(int j=0;j<q.d;++j)qr[j]=q[qi][j]-dc[g][j];auto lut=mc.prepare(qr.data());
            for(int m:coarse_groups[g])mcan.push_back({mc.distance(qr.data(),lut,mid_residual,m,false),m});}
        if((int)mcan.size()<mp)continue;midcan+=mcan.size();std::partial_sort(mcan.begin(),mcan.begin()+mp,mcan.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});
        for(int z=0;z<mp;++z)out.mid[qi].push_back(mcan[z].second);std::vector<std::pair<float,int>>fcan;
        for(int m:out.mid[qi]){int g=coarse_of_mid[m];auto dm=mc.decode(mid_residual,m);std::vector<float>qr(q.d);for(int j=0;j<q.d;++j)qr[j]=q[qi][j]-dc[g][j]-dm[j];auto lut=fc.prepare(qr.data());
            for(int f:mid_groups[m])fcan.push_back({fc.distance(qr.data(),lut,fine_residual,f,false),f});}
        if((int)fcan.size()<fp)continue;finecan+=fcan.size();std::partial_sort(fcan.begin(),fcan.begin()+fp,fcan.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});
        auto&s=out.leaf[qi];for(int z=0;z<fp;++z)s.lists.push_back(fcan[z].second);std::priority_queue<std::pair<float,int>>top;
        for(int f:s.lists)for(int id:lists[f]){float d=l2(q[qi],xb[id],xb.d);if((int)top.size()<k)top.push({d,id});else if(d<top.top().first||(d==top.top().first&&id<top.top().second)){top.pop();top.push({d,id});}}
        if((int)top.size()<k)continue;s.tau2=top.top().first;while(!top.empty()){s.answer.push_back(top.top().second);top.pop();}
        if(gt){std::unordered_set<int>a(s.answer.begin(),s.answer.end());for(int id:(*gt)[qi])hits+=a.count(id);}}
    out.recall=gt?hits/(q.rows()*(double)k):0;out.query_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/q.rows();
    out.mid_candidates=midcan/(double)q.rows();out.fine_candidates=finecan/(double)q.rows();return out;
}

static double a3_route(const A3State&s,const std::vector<std::vector<int>>&mid_groups,const std::vector<int>&mid_of_fine,
        const std::vector<int>&fine_pos,const std::vector<std::vector<int>>&coarse_groups,const std::vector<int>&coarse_of_mid,
        const std::vector<int>&mid_pos,const CRCodec&cc,const CRCodec&mc,const CRCodec&fc){
    std::vector<int>all(coarse_groups.size());std::iota(all.begin(),all.end(),0);
    double parameters=96.0+cc.parameter_bytes()+mc.parameter_bytes()+fc.parameter_bytes(),fixed=parameters+coarse_groups.size()*(double)cc.code_bytes();
    double huffman=parameters+cc.huffman_model_bytes()+mc.huffman_model_bytes()+fc.huffman_model_bytes()+cc.huffman_bytes(all)+16.0,shared=0,fixed_blocks=0,huffman_blocks=0;std::vector<std::vector<int>>sm(coarse_groups.size()),sf(mid_groups.size());
    for(size_t qi=0;qi<s.leaf.size();++qi){const auto&cg=s.coarse[qi];shared+=cg.size()*68.0+hv_dummy_proof_count(coarse_groups.size(),cg)*32.0;
        for(int g:cg){fixed_blocks+=coarse_groups[g].size()*(double)mc.code_bytes();huffman_blocks+=mc.huffman_bytes(coarse_groups[g]);}for(auto&x:sm)x.clear();for(int m:s.mid[qi])sm[coarse_of_mid[m]].push_back(mid_pos[m]);
        for(int g:cg)if(!sm[g].empty())shared+=sm[g].size()*68.0+hv_dummy_proof_count(coarse_groups[g].size(),sm[g])*32.0;
        for(int m:s.mid[qi]){fixed_blocks+=mid_groups[m].size()*(double)fc.code_bytes();huffman_blocks+=fc.huffman_bytes(mid_groups[m]);}for(auto&x:sf)x.clear();for(int f:s.leaf[qi].lists)sf[mid_of_fine[f]].push_back(fine_pos[f]);
        for(int m:s.mid[qi])if(!sf[m].empty())shared+=sf[m].size()*4.0+hv_dummy_proof_count(mid_groups[m].size(),sf[m])*32.0;}
    double nq=s.leaf.size();return std::min(fixed+(shared+fixed_blocks)/nq,huffman+(shared+huffman_blocks)/nq);
}

// Four routing tiers are used only as a stopping check: if a fourth residual
// gate cannot beat the best shallower candidate, synthesis stops without it.
struct A4State {
    std::vector<VLState>leaf;std::vector<std::vector<int>>coarse,mid,low;
    double recall=0,query_ms=0,mid_candidates=0,low_candidates=0,fine_candidates=0;
};

static A4State a4_states(const Matrix&xb,const Matrix&q,const std::vector<std::vector<int>>&lists,
        const std::vector<std::vector<int>>&low_groups,const std::vector<int>&low_of_fine,
        const std::vector<std::vector<int>>&mid_groups,const std::vector<int>&mid_of_low,
        const std::vector<std::vector<int>>&coarse_groups,const std::vector<int>&coarse_of_mid,
        const Matrix&coarse_center,const Matrix&mid_residual,const Matrix&low_residual,const Matrix&fine_residual,
        const CRCodec&cc,const CRCodec&mc,const CRCodec&lc,const CRCodec&fc,int cp,int mp,int lp,int fp,int k,
        const std::vector<std::vector<int>>*gt){
    A4State out;out.leaf.resize(q.rows());out.coarse.resize(q.rows());out.mid.resize(q.rows());out.low.resize(q.rows());
    uint64_t hits=0,midcan=0,lowcan=0,finecan=0;auto begin=std::chrono::steady_clock::now();
    for(size_t qi=0;qi<q.rows();++qi){
        auto clut=cc.prepare(q[qi]);std::vector<int>corder(coarse_groups.size());std::iota(corder.begin(),corder.end(),0);
        std::partial_sort(corder.begin(),corder.begin()+cp,corder.end(),[&](int a,int b){float x=cc.distance(q[qi],clut,coarse_center,a,false),y=cc.distance(q[qi],clut,coarse_center,b,false);return x==y?a<b:x<y;});
        out.coarse[qi].assign(corder.begin(),corder.begin()+cp);std::vector<std::vector<float>>dc(coarse_groups.size()),dm(mid_groups.size()),dl(low_groups.size());
        std::vector<std::pair<float,int>>mcan;
        for(int g:out.coarse[qi]){dc[g]=cc.decode(coarse_center,g);std::vector<float>qr(q.d);for(int j=0;j<q.d;++j)qr[j]=q[qi][j]-dc[g][j];auto lut=mc.prepare(qr.data());
            for(int m:coarse_groups[g])mcan.push_back({mc.distance(qr.data(),lut,mid_residual,m,false),m});}
        if((int)mcan.size()<mp)continue;midcan+=mcan.size();std::partial_sort(mcan.begin(),mcan.begin()+mp,mcan.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});
        for(int z=0;z<mp;++z)out.mid[qi].push_back(mcan[z].second);std::vector<std::pair<float,int>>lcan;
        for(int m:out.mid[qi]){int g=coarse_of_mid[m];auto r=mc.decode(mid_residual,m);dm[m].resize(q.d);std::vector<float>qr(q.d);for(int j=0;j<q.d;++j){dm[m][j]=dc[g][j]+r[j];qr[j]=q[qi][j]-dm[m][j];}auto lut=lc.prepare(qr.data());
            for(int l:mid_groups[m])lcan.push_back({lc.distance(qr.data(),lut,low_residual,l,false),l});}
        if((int)lcan.size()<lp)continue;lowcan+=lcan.size();std::partial_sort(lcan.begin(),lcan.begin()+lp,lcan.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});
        for(int z=0;z<lp;++z)out.low[qi].push_back(lcan[z].second);std::vector<std::pair<float,int>>fcan;
        for(int l:out.low[qi]){int m=mid_of_low[l];auto r=lc.decode(low_residual,l);dl[l].resize(q.d);std::vector<float>qr(q.d);for(int j=0;j<q.d;++j){dl[l][j]=dm[m][j]+r[j];qr[j]=q[qi][j]-dl[l][j];}auto lut=fc.prepare(qr.data());
            for(int f:low_groups[l])fcan.push_back({fc.distance(qr.data(),lut,fine_residual,f,false),f});}
        if((int)fcan.size()<fp)continue;finecan+=fcan.size();std::partial_sort(fcan.begin(),fcan.begin()+fp,fcan.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});
        auto&s=out.leaf[qi];for(int z=0;z<fp;++z)s.lists.push_back(fcan[z].second);std::priority_queue<std::pair<float,int>>top;
        for(int f:s.lists)for(int id:lists[f]){float d=l2(q[qi],xb[id],xb.d);if((int)top.size()<k)top.push({d,id});else if(d<top.top().first||(d==top.top().first&&id<top.top().second)){top.pop();top.push({d,id});}}
        if((int)top.size()<k)continue;s.tau2=top.top().first;while(!top.empty()){s.answer.push_back(top.top().second);top.pop();}
        if(gt){std::unordered_set<int>a(s.answer.begin(),s.answer.end());for(int id:(*gt)[qi])hits+=a.count(id);}}
    out.recall=gt?hits/(q.rows()*(double)k):0;out.query_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/q.rows();
    out.mid_candidates=midcan/(double)q.rows();out.low_candidates=lowcan/(double)q.rows();out.fine_candidates=finecan/(double)q.rows();return out;
}

static double a4_route(const A4State&s,const std::vector<std::vector<int>>&low_groups,const std::vector<int>&low_of_fine,const std::vector<int>&fine_pos,
        const std::vector<std::vector<int>>&mid_groups,const std::vector<int>&mid_of_low,const std::vector<int>&low_pos,
        const std::vector<std::vector<int>>&coarse_groups,const std::vector<int>&coarse_of_mid,const std::vector<int>&mid_pos,
        const CRCodec&cc,const CRCodec&mc,const CRCodec&lc,const CRCodec&fc){
    std::vector<int>all(coarse_groups.size());std::iota(all.begin(),all.end(),0);double parameters=96.0+cc.parameter_bytes()+mc.parameter_bytes()+lc.parameter_bytes()+fc.parameter_bytes();
    double fixed=parameters+coarse_groups.size()*(double)cc.code_bytes(),huffman=parameters+cc.huffman_model_bytes()+mc.huffman_model_bytes()+lc.huffman_model_bytes()+fc.huffman_model_bytes()+cc.huffman_bytes(all)+16.0;
    double shared=0,fixed_blocks=0,huffman_blocks=0;std::vector<std::vector<int>>sm(coarse_groups.size()),sl(mid_groups.size()),sf(low_groups.size());
    for(size_t qi=0;qi<s.leaf.size();++qi){const auto&cg=s.coarse[qi];shared+=cg.size()*68.0+hv_dummy_proof_count(coarse_groups.size(),cg)*32.0;
        for(int g:cg){fixed_blocks+=coarse_groups[g].size()*(double)mc.code_bytes();huffman_blocks+=mc.huffman_bytes(coarse_groups[g]);}for(auto&x:sm)x.clear();for(int m:s.mid[qi])sm[coarse_of_mid[m]].push_back(mid_pos[m]);
        for(int g:cg)if(!sm[g].empty())shared+=sm[g].size()*68.0+hv_dummy_proof_count(coarse_groups[g].size(),sm[g])*32.0;
        for(int m:s.mid[qi]){fixed_blocks+=mid_groups[m].size()*(double)lc.code_bytes();huffman_blocks+=lc.huffman_bytes(mid_groups[m]);}for(auto&x:sl)x.clear();for(int l:s.low[qi])sl[mid_of_low[l]].push_back(low_pos[l]);
        for(int m:s.mid[qi])if(!sl[m].empty())shared+=sl[m].size()*68.0+hv_dummy_proof_count(mid_groups[m].size(),sl[m])*32.0;
        for(int l:s.low[qi]){fixed_blocks+=low_groups[l].size()*(double)fc.code_bytes();huffman_blocks+=fc.huffman_bytes(low_groups[l]);}for(auto&x:sf)x.clear();for(int f:s.leaf[qi].lists)sf[low_of_fine[f]].push_back(fine_pos[f]);
        for(int l:s.low[qi])if(!sf[l].empty())shared+=sf[l].size()*4.0+hv_dummy_proof_count(low_groups[l].size(),sf[l])*32.0;}
    double nq=s.leaf.size();return std::min(fixed+(shared+fixed_blocks)/nq,huffman+(shared+huffman_blocks)/nq);
}

struct AResult {
    int depth=0,fine=0,coarse=0,mid=0,low=0,cp=0,mp=0,lp=0,fp=0,b1=0,b2=0,b3=0;
    double cal_recall=0,cal_route=0,cal_leaf=0,cal_total=0,cal_ms=0,test_recall=0,test_route=0,test_leaf=0,test_total=0,test_ms=0,visited=0,build_s=0;
    bool feasible=false;
};

static bool a_better(const AResult&a,const AResult&b){
    if(a.feasible!=b.feasible)return a.feasible;if(a.feasible)return a.cal_total<b.cal_total;
    if(a.cal_recall!=b.cal_recall)return a.cal_recall>b.cal_recall;return a.cal_total<b.cal_total;
}

static void a_write(std::ofstream&o,const AResult&r,bool selected){
    o<<selected<<','<<r.depth<<','<<r.fine<<','<<r.coarse<<','<<r.mid<<','<<r.low<<','<<r.b1<<','<<r.b2<<','<<r.b3<<','<<r.cp<<','<<r.mp<<','<<r.lp<<','<<r.fp<<','<<r.feasible<<','
     <<r.cal_recall<<','<<r.cal_route<<','<<r.cal_leaf<<','<<r.cal_total<<','<<r.cal_ms<<','<<r.test_recall<<','<<r.test_route<<','<<r.test_leaf<<','<<r.test_total<<','<<r.visited<<','<<r.test_ms<<','<<r.build_s<<'\n';
}

#ifndef AUTO_VO_TREE_NO_MAIN
int main(int argc,char**argv)try{
    auto synth_begin=std::chrono::steady_clock::now();auto c=a_args(argc,argv);std::cerr<<"Loading base/learn/test...\n";Matrix xb=fvecs(c.data+"/sift_base.fvecs",c.base),calq=fvecs(c.data+"/sift_learn.fvecs",c.cal_queries),testq=fvecs(c.data+"/sift_query.fvecs",c.test_queries);
    std::cerr<<"Computing calibration/test exact ground truth...\n";auto calgt=exact_gt(xb,calq,c.k),testgt=exact_gt(xb,testq,c.k);double need=std::min(1.0,c.target+c.margin);
    auto allowed=[&](AResult&r){r.feasible=r.cal_recall>=need&&r.cal_ms<=c.latency_budget_ms&&r.build_s<=c.build_budget_s;};
    int cap=std::min(c.max_fine,c.base),kf=1;while(kf*2<=cap)kf*=2;std::vector<int>fine_choices;if(kf/2>=2048)fine_choices.push_back(kf/2);fine_choices.push_back(kf);
    std::vector<AResult>rows;AResult global;
    for(int K:fine_choices){std::cerr<<"Proposal fine leaves="<<K<<"...\n";auto fine_begin=std::chrono::steady_clock::now();auto lists=hkmeans(xb,K,c.iters);Matrix fine=cr_matrix(centers(xb,lists));double fine_build_s=std::chrono::duration<double>(std::chrono::steady_clock::now()-fine_begin).count();Matrix empty;int min_fp=std::min(K,std::max(c.k,(int)std::ceil(2.5*c.k*K/(double)c.base)));
        if(c.max_depth>=1){std::cerr<<"  evaluating depth=1...\n";auto b=std::chrono::steady_clock::now();CRCodec codec("pq16",fine,empty,std::min(c.pq_train,K),c.pq_iters,c.seed+K);double build_s=fine_build_s+std::chrono::duration<double>(std::chrono::steady_clock::now()-b).count();AResult best;
            for(int fp0:a_fracs(K,{0.006,0.009,0.012,0.018,0.027,0.04})){int fp=std::max(min_fp,fp0);auto s=cr_states(xb,calq,lists,fine,{},Matrix{},codec,0,fp,c.k,&calgt);double vis=a_visited(lists,s.state),route=a_flat_route(s,K,codec),leaf=a_leaf_proxy(calq,vis);AResult r;r.depth=1;r.fine=K;r.fp=fp;r.cal_recall=s.recall;r.cal_route=route;r.cal_leaf=leaf;r.cal_total=route+leaf;r.cal_ms=s.query_ms;r.build_s=build_s;allowed(r);if(best.depth==0||a_better(r,best))best=r;}
            auto ts=cr_states(xb,testq,lists,fine,{},Matrix{},codec,0,best.fp,c.k,&testgt);best.test_recall=ts.recall;best.visited=a_visited(lists,ts.state);best.test_route=a_flat_route(ts,K,codec);best.test_leaf=a_leaf_proxy(testq,best.visited);best.test_total=best.test_route+best.test_leaf;best.test_ms=ts.query_ms;rows.push_back(best);if(global.depth==0||a_better(best,global))global=best;}
        if(c.max_depth>=2)for(int B:{32,64,128}){if(K%B)continue;int C=K/B;if(C<16)continue;std::cerr<<"  evaluating depth=2 fanout="<<B<<"...\n";auto b=std::chrono::steady_clock::now();auto groups=hkmeans_balanced(fine,C,c.iters);Matrix coarse=cr_matrix(centers(fine,groups));
            std::vector<int>go(K,-1);for(size_t g=0;g<groups.size();++g)for(int f:groups[g])go[f]=g;Matrix res;res.d=xb.d;res.x.resize((size_t)K*xb.d);for(int f=0;f<K;++f)for(int j=0;j<xb.d;++j)res.x[(size_t)f*xb.d+j]=fine[f][j]-coarse[go[f]][j];
            CRCodec cc("sq4",coarse,empty,std::min(c.pq_train,C),c.pq_iters,c.seed+C),rc("pq24",res,empty,std::min(c.pq_train,K),c.pq_iters,c.seed+K+B);double build_s=fine_build_s+std::chrono::duration<double>(std::chrono::steady_clock::now()-b).count();auto layout=a_identity2(groups,K);AResult best;
            auto eval=[&](int cp,int fp){auto s=rh_states(xb,calq,lists,fine,groups,go,coarse,res,cc,rc,cp,fp,c.k,&calgt);auto rr=rh_route(s,groups,go,layout,cc,rc);double vis=a_visited(lists,s.state),route=std::min(rr.total,rr.huffman_total),leaf=a_leaf_proxy(calq,vis);AResult r;r.depth=2;r.fine=K;r.coarse=C;r.b1=B;r.cp=cp;r.fp=fp;r.cal_recall=s.recall;r.cal_route=route;r.cal_leaf=leaf;r.cal_total=route+leaf;r.cal_ms=s.query_ms;r.build_s=build_s;allowed(r);return r;};
            for(int cp:a_fracs(C,{0.08,0.12,0.16,0.20,0.26,0.34}))for(int fp0:a_fracs(K,{0.007,0.010,0.012,0.016,0.022})){int fp=std::max(min_fp,fp0);if(fp>cp*B)continue;auto r=eval(cp,fp);if(best.depth==0||a_better(r,best))best=r;}
            int step=std::max(1,C/96),radius=std::max(4,C/24),center=best.cp;for(int cp=std::max(1,center-radius);cp<=std::min(C,center+radius);cp+=step){int lo=min_fp,hi=std::min(K,cp*B);if(lo>hi)continue;auto high=eval(cp,hi);if(!high.feasible)continue;while(lo<hi){int m=lo+(hi-lo)/2;auto r=eval(cp,m);if(r.feasible)hi=m;else lo=m+1;}int guarded_cp=std::min(C,(int)std::ceil(cp*(1.0+c.probe_guard))),guarded_fp=std::min(K,(int)std::ceil(lo*(1.0+c.probe_guard)));guarded_fp=std::min(guarded_fp,guarded_cp*B);auto r=eval(guarded_cp,guarded_fp);if(a_better(r,best))best=r;}
            auto ts=rh_states(xb,testq,lists,fine,groups,go,coarse,res,cc,rc,best.cp,best.fp,c.k,&testgt);auto tr=rh_route(ts,groups,go,layout,cc,rc);best.test_recall=ts.recall;best.visited=a_visited(lists,ts.state);best.test_route=std::min(tr.total,tr.huffman_total);best.test_leaf=a_leaf_proxy(testq,best.visited);best.test_total=best.test_route+best.test_leaf;best.test_ms=ts.query_ms;rows.push_back(best);if(global.depth==0||a_better(best,global))global=best;}
        if(c.max_depth>=3)for(int B:{8,16,32}){if(K%(B*B))continue;int M=K/B,C=M/B;if(C<8)continue;std::cerr<<"  evaluating depth=3 fanout="<<B<<"/"<<B<<"...\n";auto b=std::chrono::steady_clock::now();auto mg=hkmeans_balanced(fine,M,c.iters);Matrix mid=cr_matrix(centers(fine,mg));auto cg=hkmeans_balanced(mid,C,c.iters);Matrix coarse=cr_matrix(centers(mid,cg));
            std::vector<int>mof(K,-1),fpos(K,-1);for(size_t m=0;m<mg.size();++m)for(size_t p=0;p<mg[m].size();++p){mof[mg[m][p]]=m;fpos[mg[m][p]]=p;}std::vector<int>com(M,-1),mpos(M,-1);for(size_t g=0;g<cg.size();++g)for(size_t p=0;p<cg[g].size();++p){com[cg[g][p]]=g;mpos[cg[g][p]]=p;}
            Matrix mr,fr;mr.d=fr.d=xb.d;mr.x.resize((size_t)M*xb.d);fr.x.resize((size_t)K*xb.d);for(int m=0;m<M;++m)for(int j=0;j<xb.d;++j)mr.x[(size_t)m*xb.d+j]=mid[m][j]-coarse[com[m]][j];for(int f=0;f<K;++f)for(int j=0;j<xb.d;++j)fr.x[(size_t)f*xb.d+j]=fine[f][j]-mid[mof[f]][j];
            CRCodec cc("sq4",coarse,empty,std::min(c.pq_train,C),c.pq_iters,c.seed+C),mc("pq24",mr,empty,std::min(c.pq_train,M),c.pq_iters,c.seed+M),fc("pq16",fr,empty,std::min(c.pq_train,K),c.pq_iters,c.seed+K+B);double build_s=fine_build_s+std::chrono::duration<double>(std::chrono::steady_clock::now()-b).count();AResult best;
            for(int cp:a_fracs(C,{0.08,0.12,0.16,0.22,0.30,0.40}))for(double rm:{0.40,0.55,0.70,0.85}){int mp=std::max(1,std::min(M,(int)std::lround(cp*B*rm)));for(double rf:{0.08,0.12,0.18}){int fp=std::max(min_fp,std::min(K,(int)std::lround(mp*B*rf)));if(fp>mp*B)continue;auto s=a3_states(xb,calq,lists,mg,mof,cg,com,coarse,mr,fr,cc,mc,fc,cp,mp,fp,c.k,&calgt);double vis=a_visited(lists,s.leaf),route=a3_route(s,mg,mof,fpos,cg,com,mpos,cc,mc,fc),leaf=a_leaf_proxy(calq,vis);AResult r;r.depth=3;r.fine=K;r.mid=M;r.coarse=C;r.b1=r.b2=B;r.cp=cp;r.mp=mp;r.fp=fp;r.cal_recall=s.recall;r.cal_route=route;r.cal_leaf=leaf;r.cal_total=route+leaf;r.cal_ms=s.query_ms;r.build_s=build_s;allowed(r);if(best.depth==0||a_better(r,best))best=r;}}
            auto ts=a3_states(xb,testq,lists,mg,mof,cg,com,coarse,mr,fr,cc,mc,fc,best.cp,best.mp,best.fp,c.k,&testgt);best.test_recall=ts.recall;best.visited=a_visited(lists,ts.leaf);best.test_route=a3_route(ts,mg,mof,fpos,cg,com,mpos,cc,mc,fc);best.test_leaf=a_leaf_proxy(testq,best.visited);best.test_total=best.test_route+best.test_leaf;best.test_ms=ts.query_ms;rows.push_back(best);if(global.depth==0||a_better(best,global))global=best;}
        if(c.max_depth>=4)for(int B:{4,8}){if(K%(B*B*B))continue;int L=K/B,M=L/B,C=M/B;if(C<8)continue;std::cerr<<"  evaluating depth=4 fanout="<<B<<"/"<<B<<"/"<<B<<"...\n";auto b=std::chrono::steady_clock::now();auto lg=hkmeans_balanced(fine,L,c.iters);Matrix low=cr_matrix(centers(fine,lg));auto mg=hkmeans_balanced(low,M,c.iters);Matrix mid=cr_matrix(centers(low,mg));auto cg=hkmeans_balanced(mid,C,c.iters);Matrix coarse=cr_matrix(centers(mid,cg));
            std::vector<int>lof(K,-1),fpos(K,-1);for(size_t l=0;l<lg.size();++l)for(size_t p=0;p<lg[l].size();++p){lof[lg[l][p]]=l;fpos[lg[l][p]]=p;}std::vector<int>mol(L,-1),lpos(L,-1);for(size_t m=0;m<mg.size();++m)for(size_t p=0;p<mg[m].size();++p){mol[mg[m][p]]=m;lpos[mg[m][p]]=p;}std::vector<int>com(M,-1),mpos(M,-1);for(size_t g=0;g<cg.size();++g)for(size_t p=0;p<cg[g].size();++p){com[cg[g][p]]=g;mpos[cg[g][p]]=p;}
            Matrix mr,lr,fr;mr.d=lr.d=fr.d=xb.d;mr.x.resize((size_t)M*xb.d);lr.x.resize((size_t)L*xb.d);fr.x.resize((size_t)K*xb.d);for(int m=0;m<M;++m)for(int j=0;j<xb.d;++j)mr.x[(size_t)m*xb.d+j]=mid[m][j]-coarse[com[m]][j];for(int l=0;l<L;++l)for(int j=0;j<xb.d;++j)lr.x[(size_t)l*xb.d+j]=low[l][j]-mid[mol[l]][j];for(int f=0;f<K;++f)for(int j=0;j<xb.d;++j)fr.x[(size_t)f*xb.d+j]=fine[f][j]-low[lof[f]][j];
            CRCodec cc("sq4",coarse,empty,std::min(c.pq_train,C),c.pq_iters,c.seed+C),mc("pq24",mr,empty,std::min(c.pq_train,M),c.pq_iters,c.seed+M),lc("pq24",lr,empty,std::min(c.pq_train,L),c.pq_iters,c.seed+L),fc("pq16",fr,empty,std::min(c.pq_train,K),c.pq_iters,c.seed+K+B);double build_s=fine_build_s+std::chrono::duration<double>(std::chrono::steady_clock::now()-b).count();AResult best;
            for(int cp:a_fracs(C,{0.08,0.14,0.22,0.34}))for(double r1:{0.45,0.65,0.85}){int mp=std::max(1,std::min(M,(int)std::lround(cp*B*r1)));for(double r2:{0.35,0.55,0.75,0.90}){int lp=std::max(1,std::min(L,(int)std::lround(mp*B*r2)));for(double r3:{0.12,0.20,0.30,0.45}){int fp=std::max(min_fp,std::min(K,(int)std::lround(lp*B*r3)));if(fp>lp*B)continue;auto s=a4_states(xb,calq,lists,lg,lof,mg,mol,cg,com,coarse,mr,lr,fr,cc,mc,lc,fc,cp,mp,lp,fp,c.k,&calgt);double vis=a_visited(lists,s.leaf),route=a4_route(s,lg,lof,fpos,mg,mol,lpos,cg,com,mpos,cc,mc,lc,fc),leaf=a_leaf_proxy(calq,vis);AResult r;r.depth=4;r.fine=K;r.low=L;r.mid=M;r.coarse=C;r.b1=r.b2=r.b3=B;r.cp=cp;r.mp=mp;r.lp=lp;r.fp=fp;r.cal_recall=s.recall;r.cal_route=route;r.cal_leaf=leaf;r.cal_total=route+leaf;r.cal_ms=s.query_ms;r.build_s=build_s;allowed(r);if(best.depth==0||a_better(r,best))best=r;}}}
            auto ts=a4_states(xb,testq,lists,lg,lof,mg,mol,cg,com,coarse,mr,lr,fr,cc,mc,lc,fc,best.cp,best.mp,best.lp,best.fp,c.k,&testgt);best.test_recall=ts.recall;best.visited=a_visited(lists,ts.leaf);best.test_route=a4_route(ts,lg,lof,fpos,mg,mol,lpos,cg,com,mpos,cc,mc,lc,fc);best.test_leaf=a_leaf_proxy(testq,best.visited);best.test_total=best.test_route+best.test_leaf;best.test_ms=ts.query_ms;rows.push_back(best);if(global.depth==0||a_better(best,global))global=best;}
    }
    double synthesis_s=std::chrono::duration<double>(std::chrono::steady_clock::now()-synth_begin).count();struct rusage ru{};getrusage(RUSAGE_SELF,&ru);double peak_mb=ru.ru_maxrss/1024.0;
    std::ofstream out(c.output);if(!out)throw std::runtime_error("cannot open output");out<<std::setprecision(12);out<<"selected,depth,fine_nlist,coarse_nlist,mid_nlist,low_nlist,branch1,branch2,branch3,coarse_probe,mid_probe,low_probe,fine_probe,cal_feasible,cal_recall,cal_route_proxy,cal_leaf_proxy,cal_total_proxy,cal_query_ms,test_recall,test_route_proxy,test_leaf_proxy,test_total_proxy,test_visited,test_query_ms,index_build_s\n";
    for(const auto&r:rows)a_write(out,r,r.depth==global.depth&&r.fine==global.fine&&r.coarse==global.coarse&&r.mid==global.mid&&r.low==global.low&&r.cp==global.cp&&r.mp==global.mp&&r.lp==global.lp&&r.fp==global.fp);
    std::cout<<"AUTO_SELECTED depth="<<global.depth<<" fine="<<global.fine<<" coarse="<<global.coarse<<" mid="<<global.mid<<" low="<<global.low<<" branch="<<global.b1<<'/'<<global.b2<<'/'<<global.b3<<" probes="<<global.cp<<'/'<<global.mp<<'/'<<global.lp<<'/'<<global.fp<<" cal_recall="<<global.cal_recall<<" test_recall="<<global.test_recall<<" test_proxy="<<global.test_total/1024<<" KiB visited="<<global.visited<<" query="<<global.test_ms<<" ms build="<<global.build_s<<" s synthesis="<<synthesis_s<<" s peak="<<peak_mb<<" MiB\n";return global.feasible?0:2;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
#endif

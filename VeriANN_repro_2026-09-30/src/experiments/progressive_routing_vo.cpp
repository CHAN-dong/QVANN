#define JOINT_VO_SUCCESSIVE_REFINEMENT_NO_MAIN
#include "joint_vo_successive_refinement.cpp"
#undef JOINT_VO_SUCCESSIVE_REFINEMENT_NO_MAIN

// Fixed-structure experiment: authenticate a PQ routing code as several
// subspace chunks.  The first chunk is disclosed for every reachable child;
// later chunks are disclosed only while the accumulated ADC lower bound can
// still beat the final top-p threshold.  This reproduces the exact same PQ
// top-p set, so it cannot change ANN recall.

struct PRCfg {
    std::string data="../dataset/sift", output="progressive_routing_sift100k.csv";
    int base=100000, cal_queries=60, test_queries=500, k=100;
    int coarse=256, fine=65536, coarse_probe=54, fine_probe=863;
    int iters=4, pq_train=12000, pq_iters=4, repeats=1;
    bool best_only=false;
    uint64_t seed=20260830;
};

struct PRResult {
    int group_slots=0, block_size=1; bool adaptive_order=false;
    double candidates=0, staged_payload=0, staged_proofs=0, extra_roots=0;
    std::vector<double> active;
    uint64_t selection_mismatches=0, bound_violations=0;
};

static PRResult pr_evaluate(const BIndex&t,const Matrix&q,const WStates&reference,
                            int group_slots,bool adaptive_order,int block_size=1){
    if(t.depth!=2||t.codec[1]->alphabet()!=64||t.codec[1]->slots()%group_slots)
        throw std::runtime_error("progressive routing requires two-level PQ64 and divisible groups");
    const int slots=t.codec[1]->slots(),alphabet=t.codec[1]->alphabet(),ng=slots/group_slots;
    PRResult out;out.group_slots=group_slots;out.block_size=block_size;out.adaptive_order=adaptive_order;out.active.assign(ng,0);
    for(size_t qi=0;qi<q.rows();++qi){
        struct Parent {int id=0;std::vector<float>lut;std::vector<int>order;};
        struct Candidate {int id=0,parent=0,pos=0;float exact=0,lb=0;uint32_t revealed=0;bool active=true;};
        std::vector<Parent>parents;std::vector<Candidate>cand;
        for(int parent_id:reference.trace[qi].selected[0]){
            Parent p;p.id=parent_id;auto decoded=t.codec[0]->decode(t.encoded[0],parent_id);std::vector<float>qr(t.d);
            for(int j=0;j<t.d;++j)qr[j]=q[qi][j]-decoded[j];p.lut=t.codec[1]->prepare(qr.data());p.order.resize(ng);std::iota(p.order.begin(),p.order.end(),0);
            if(adaptive_order)std::stable_sort(p.order.begin(),p.order.end(),[&](int a,int b){
                auto score=[&](int g){double z=0;for(int s=g*group_slots;s<(g+1)*group_slots;++s){float lo=std::numeric_limits<float>::infinity();double mean=0;for(int x=0;x<alphabet;++x){float v=p.lut[(size_t)s*alphabet+x];lo=std::min(lo,v);mean+=v;}z+=mean/alphabet-lo;}return z;};
                double x=score(a),y=score(b);return x==y?a<b:x>y;});
            int pi=parents.size();parents.push_back(std::move(p));const auto&lut=parents.back().lut;const auto&children=t.children[0][parent_id];
            float initial=0;for(int s=0;s<slots;++s){float lo=std::numeric_limits<float>::infinity();for(int x=0;x<alphabet;++x)lo=std::min(lo,lut[(size_t)s*alphabet+x]);initial+=lo;}
            for(size_t pos=0;pos<children.size();++pos){int id=children[pos];float exact=t.codec[1]->distance(nullptr,lut,t.encoded[1],id,false);cand.push_back({id,pi,(int)pos,exact,initial,true});}
        }
        out.candidates+=cand.size();std::vector<std::pair<float,int>>rank;rank.reserve(cand.size());for(const auto&x:cand)rank.push_back({x.exact,x.id});
        std::sort(rank.begin(),rank.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});
        if((int)rank.size()<reference.trace[qi].selected[1].size())throw std::runtime_error("too few routing candidates");
        const int take=reference.trace[qi].selected[1].size();const float tau=rank[take-1].first;
        for(int stage=0;stage<ng;++stage){
            std::vector<std::vector<int>>positions(parents.size());int sent=0;
            for(const auto&x:cand)if(x.active){positions[x.parent].push_back(x.pos);++sent;}
            out.active[stage]+=sent;
            if(stage==0||block_size==1){
                out.staged_payload+=sent*(group_slots*6.0/8.0);
                if(stage>0)for(size_t p=0;p<parents.size();++p)if(!positions[p].empty())out.staged_proofs+=hv_dummy_proof_count(t.children[0][parents[p].id].size(),positions[p])*32.0;
            }else for(size_t p=0;p<parents.size();++p)if(!positions[p].empty()){
                const int child_count=t.children[0][parents[p].id].size(),block_count=(child_count+block_size-1)/block_size;
                std::vector<int>blocks;blocks.reserve(positions[p].size());for(int pos:positions[p])blocks.push_back(pos/block_size);
                std::sort(blocks.begin(),blocks.end());blocks.erase(std::unique(blocks.begin(),blocks.end()),blocks.end());
                int disclosed=0;for(int b:blocks)disclosed+=std::min(block_size,child_count-b*block_size);
                out.staged_payload+=disclosed*(group_slots*6.0/8.0);
                out.staged_proofs+=hv_dummy_proof_count(block_count,blocks)*32.0;
            }
            for(auto&x:cand)if(x.active){int g=parents[x.parent].order[stage];const auto&lut=parents[x.parent].lut;
                x.revealed|=(uint32_t(1)<<g);float sum=0;
                for(int s=0;s<slots;++s){int sg=s/group_slots;if(x.revealed&(uint32_t(1)<<sg))sum+=lut[(size_t)s*alphabet+t.codec[1]->symbol(x.id,s)];else{float lo=std::numeric_limits<float>::infinity();for(int z=0;z<alphabet;++z)lo=std::min(lo,lut[(size_t)s*alphabet+z]);sum+=lo;}}
                x.lb=std::nextafter(sum,-std::numeric_limits<float>::infinity());if(x.lb>x.exact)++out.bound_violations;
            }
            if(stage+1<ng)for(auto&x:cand)if(x.active&&x.lb>(double)tau+1e-6)x.active=false;
        }
        std::vector<std::pair<float,int>>got;for(const auto&x:cand)if(x.active)got.push_back({x.exact,x.id});std::sort(got.begin(),got.end(),[](const auto&a,const auto&b){return a.first==b.first?a.second<b.second:a.first<b.first;});
        std::vector<int>a,b;for(int z=0;z<take;++z)a.push_back(got[z].second);b=reference.trace[qi].selected[1];std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());if(a!=b)++out.selection_mismatches;
        out.extra_roots+=parents.size()*(ng-1)*32.0;
    }
    double nq=q.rows();out.candidates/=nq;out.staged_payload/=nq;out.staged_proofs/=nq;out.extra_roots/=nq;for(double&x:out.active)x/=nq;return out;
}

int main(int argc,char**argv)try{
    PRCfg c;for(int i=1;i<argc;++i){std::string a=argv[i];auto v=[&](){if(++i==argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
        if(a=="--dataset")c.data=v();else if(a=="--output")c.output=v();else if(a=="--base")c.base=std::stoi(v());else if(a=="--cal-queries")c.cal_queries=std::stoi(v());else if(a=="--test-queries")c.test_queries=std::stoi(v());else if(a=="--coarse")c.coarse=std::stoi(v());else if(a=="--coarse-probe")c.coarse_probe=std::stoi(v());else if(a=="--fine-probe")c.fine_probe=std::stoi(v());else if(a=="--best-only")c.best_only=true;else throw std::runtime_error("unknown option "+a);}
    Matrix xb=fvecs(c.data+"/sift_base.fvecs",c.base),calq=fvecs(c.data+"/sift_learn.fvecs",c.cal_queries),testq=fvecs(c.data+"/sift_query.fvecs",c.test_queries);auto calgt=exact_gt(xb,calq,c.k),testgt=exact_gt(xb,testq,c.k);
    auto lists=hkmeans(xb,c.fine,c.iters);Matrix fine=cr_matrix(centers(xb,lists));BCfg bc;bc.base=c.base;bc.counts={c.coarse,c.fine};bc.modes={"sq4","pq24"};bc.iters=c.iters;bc.pq_train=c.pq_train;bc.pq_iters=c.pq_iters;bc.seed=c.seed;auto tree=b_build(xb,bc,&lists,&fine);std::vector<int>probes{c.coarse_probe,c.fine_probe};auto train=w_query(tree,calq,probes,c.k,&calgt),test=w_query(tree,testq,probes,c.k,&testgt);auto full=w_full(tree,calq,train,testq,test,c.repeats);
    double child_huffman=0;for(const auto&tr:test.trace)for(int p:tr.selected[0])child_huffman+=tree.codec[1]->huffman_bytes(tree.children[0][p]);child_huffman/=test.trace.size();double fine_model=tree.codec[1]->huffman_model_bytes();
    std::ofstream out(c.output);out<<std::setprecision(12)<<"group_slots,adaptive_order,block_size,recall,avg_candidates,active_by_stage,stage_payload_kib,stage_proofs_kib,extra_roots_kib,ideal_child_kib,progressive_child_kib,baseline_child_huffman_kib,ideal_total_kib,total_kib,reduction,selection_mismatches,bound_violations,query_ms,vo_generation_ms\n";
    std::vector<std::tuple<int,bool,int>>configs;if(c.best_only)configs.push_back({16,true,4});else{for(int gs:{4,8,16})for(bool adaptive:{false,true})configs.push_back({gs,adaptive,1});for(int gs:{8,16})for(int block:{4,8,16,32})configs.push_back({gs,true,block});}
    for(auto [gs,adaptive,block]:configs){auto r=pr_evaluate(tree,testq,test,gs,adaptive,block);double ideal=r.staged_payload+r.extra_roots+16.0,progressive=ideal+r.staged_proofs;double ideal_total=full.total*1024.0-child_huffman-fine_model+ideal,total=full.total*1024.0-child_huffman-fine_model+progressive;std::ostringstream active;for(size_t z=0;z<r.active.size();++z){if(z)active<<'/';active<<r.active[z];}
        out<<gs<<','<<adaptive<<','<<block<<','<<test.recall<<','<<r.candidates<<','<<active.str()<<','<<r.staged_payload/1024.0<<','<<r.staged_proofs/1024.0<<','<<r.extra_roots/1024.0<<','<<ideal/1024.0<<','<<progressive/1024.0<<','<<child_huffman/1024.0<<','<<ideal_total/1024.0<<','<<total/1024.0<<','<<1.0-total/(full.total*1024.0)<<','<<r.selection_mismatches<<','<<r.bound_violations<<','<<test.query_ms<<','<<full.vo_ms<<'\n';
        std::cout<<"slots="<<gs<<" adaptive="<<adaptive<<" block="<<block<<" active="<<active.str()<<" ideal="<<ideal/1024.0<<" KiB progressive="<<progressive/1024.0<<" KiB total="<<total/1024.0<<" KiB reduction="<<(1.0-total/(full.total*1024.0))*100<<"% mismatch="<<r.selection_mismatches<<" violations="<<r.bound_violations<<"\n";}
    return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

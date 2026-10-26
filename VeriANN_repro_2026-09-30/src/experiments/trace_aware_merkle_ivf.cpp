#define PROGRESSIVE_SQ_FAST_NO_MAIN
#include "progressive_sq_fast.cpp"
#undef PROGRESSIVE_SQ_FAST_NO_MAIN

#include <functional>
#include <random>
#include <sstream>

// A matched experiment for learning Merkle leaf layouts from IVF access traces.
// The protocol has independent commitments for routing/base-SQ4/SQ6-delta/exact
// data.  Every probed base-SQ4 list is disclosed in full, so only routing,
// refinement, and exact-vector multiproofs contribute sibling hashes.

struct TAMConfig {
    std::string data="../dataset/sift";
    std::string output="trace_aware_merkle_ivf_sift100k.csv";
    int base=100000,train_queries=200,test_queries=100,k=100;
    int nlist=16384,nprobe=640,iters=6,candidates=12,repeats=3;
    uint64_t seed=20260820;
};

static TAMConfig tam_args(int argc,char**argv){
    TAMConfig c;
    for(int i=1;i<argc;++i){
        std::string a=argv[i];
        auto value=[&](){if(++i==argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
        if(a=="--dataset")c.data=value();
        else if(a=="--output")c.output=value();
        else if(a=="--base")c.base=std::stoi(value());
        else if(a=="--train-queries")c.train_queries=std::stoi(value());
        else if(a=="--test-queries")c.test_queries=std::stoi(value());
        else if(a=="--k")c.k=std::stoi(value());
        else if(a=="--nlist")c.nlist=std::stoi(value());
        else if(a=="--nprobe")c.nprobe=std::stoi(value());
        else if(a=="--iters")c.iters=std::stoi(value());
        else if(a=="--candidates")c.candidates=std::stoi(value());
        else if(a=="--repeats")c.repeats=std::stoi(value());
        else if(a=="--seed")c.seed=std::stoull(value());
        else if(a=="--help"){
            std::cout<<"Usage: trace_aware_merkle_ivf_experiment [--dataset DIR] [--base 100000] "
                     <<"[--train-queries 200] [--test-queries 100] [--k 100] "
                     <<"[--nlist 16384] [--nprobe 640] [--candidates 12] "
                     <<"[--repeats 3] [--output FILE]\n";
            std::exit(0);
        }else throw std::runtime_error("unknown option "+a);
    }
    if(c.base<=0||c.train_queries<=0||c.test_queries<=0||c.k<=0||c.nlist<=0||
       c.nprobe<=0||c.nprobe>c.nlist||c.nlist>c.base||c.candidates<1||c.repeats<1)
        throw std::runtime_error("invalid numeric configuration");
    return c;
}

static uint64_t tam_mix(uint64_t x){
    x+=0x9e3779b97f4a7c15ULL;
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;
    return x^(x>>31);
}

// Minimal number of transmitted sibling hashes in a complete binary tree.
// Leaves [n,padded) are public dummy leaves, hence their hashes are derivable
// and never transmitted.  An empty selection costs one hash because the local
// root is needed to reconstruct its authenticated parent list leaf.
static size_t tam_dummy_proof_count(size_t n,const std::vector<int>&selected){
    if(!n)return 0;
    size_t padded=1;while(padded<n)padded<<=1;
    std::vector<size_t>cur;cur.reserve(selected.size());
    for(int x:selected){
        if(x<0||(size_t)x>=n)throw std::runtime_error("proof position out of range");
        cur.push_back((size_t)x);
    }
    std::sort(cur.begin(),cur.end());
    cur.erase(std::unique(cur.begin(),cur.end()),cur.end());
    if(cur.empty())return 1;
    size_t hashes=0,width=1;
    while(padded>1){
        std::vector<size_t>parents;parents.reserve((cur.size()+1)/2);
        for(size_t i=0;i<cur.size();){
            const size_t x=cur[i];
            if((x&1U)==0&&i+1<cur.size()&&cur[i+1]==x+1)i+=2;
            else{
                const size_t sibling=x^1U;
                if(sibling*width<n)++hashes;
                ++i;
            }
            const size_t parent=x>>1;
            if(parents.empty()||parents.back()!=parent)parents.push_back(parent);
        }
        cur.swap(parents);padded>>=1;width<<=1;
    }
    return hashes;
}

static size_t tam_dummy_proof_count_slow(size_t n,const std::vector<int>&selected){
    if(!n)return 0;
    size_t padded=1;while(padded<n)padded<<=1;
    std::vector<int>mark(n);for(int x:selected)if(x>=0&&(size_t)x<n)mark[x]=1;
    std::vector<int>pref(n+1);for(size_t i=0;i<n;++i)pref[i+1]=pref[i]+mark[i];
    auto rec=[&](auto&&self,size_t l,size_t r)->size_t{
        if(l>=n)return 0;
        const size_t rr=std::min(r,n);
        if(pref[rr]-pref[l]==0)return 1;
        if(r-l==1)return 0;
        const size_t m=(l+r)/2;
        return self(self,l,m)+self(self,m,r);
    };
    return rec(rec,0,padded);
}

static void tam_proof_self_test(){
    for(size_t n=1;n<=10;++n)for(unsigned mask=0;mask<(1U<<n);++mask){
        std::vector<int>s;for(size_t i=0;i<n;++i)if(mask&(1U<<i))s.push_back((int)i);
        if(tam_dummy_proof_count(n,s)!=tam_dummy_proof_count_slow(n,s))
            throw std::runtime_error("public-dummy proof counter mismatch");
    }
}

struct TAMTrace {
    std::vector<int> lists,answer;
    std::vector<std::vector<int>> after4,exact6;
};

struct TAMCorpus {
    std::vector<TAMTrace> trace;
    double query_ms=0,visited=0,after4=0,exact6=0,recall=0;
    uint64_t checksum=1469598103934665603ULL;
};

static TAMCorpus tam_build_traces(const Matrix&xb,const Matrix&queries,
                                  const std::vector<std::vector<int>>&lists,
                                  const std::vector<std::vector<float>>&centroids,
                                  const NestedSQFast&sq,int nprobe,int k,
                                  const std::vector<std::vector<int>>*ground_truth){
    TAMCorpus out;out.trace.resize(queries.rows());uint64_t hits=0;
    double query_ns=0;uint64_t visited=0,a4_count=0,e6_count=0;
    for(size_t qi=0;qi<queries.rows();++qi){
        auto start=std::chrono::steady_clock::now();
        std::vector<int>order(lists.size());std::iota(order.begin(),order.end(),0);
        std::partial_sort(order.begin(),order.begin()+nprobe,order.end(),[&](int a,int b){
            return l2(queries[qi],centroids[a].data(),xb.d)<l2(queries[qi],centroids[b].data(),xb.d);
        });
        TAMTrace&t=out.trace[qi];t.lists.assign(order.begin(),order.begin()+nprobe);
        std::priority_queue<std::pair<float,int>>top;
        for(int c:t.lists)for(int id:lists[c]){
            const float distance=l2(queries[qi],xb[id],xb.d);++visited;
            if((int)top.size()<k)top.push({distance,id});
            else if(distance<top.top().first){top.pop();top.push({distance,id});}
        }
        if((int)top.size()!=k)throw std::runtime_error("fewer than k IVF candidates");
        const float tau2=top.top().first;
        while(!top.empty()){t.answer.push_back(top.top().second);top.pop();}
        std::sort(t.answer.begin(),t.answer.end());
        auto query_end=std::chrono::steady_clock::now();
        query_ns+=std::chrono::duration<double,std::nano>(query_end-start).count();

        if(ground_truth){
            std::unordered_set<int>answer(t.answer.begin(),t.answer.end());
            for(int id:(*ground_truth)[qi])hits+=answer.count(id);
        }
        for(int id:t.answer){out.checksum^=tam_mix((qi<<32)^static_cast<uint32_t>(id));out.checksum*=1099511628211ULL;}

        const auto q4=sq.prepare(queries[qi],4,false),q6=sq.prepare(queries[qi],6,false);
        t.after4.resize(t.lists.size());t.exact6.resize(t.lists.size());
        for(size_t u=0;u<t.lists.size();++u){
            const auto&list=lists[t.lists[u]];
            for(size_t p=0;p<list.size();++p){
                const bool pass4=sq.overlaps((size_t)list[p],tau2,q4);
                const bool pass6=sq.overlaps((size_t)list[p],tau2,q6);
                if(pass6&&!pass4)throw std::runtime_error("non-nested 4->6 trace");
                if(pass4)t.after4[u].push_back((int)p),++a4_count;
                if(pass6)t.exact6[u].push_back((int)p),++e6_count;
            }
        }
    }
    const double nq=queries.rows();out.query_ms=query_ns/nq/1e6;out.visited=visited/nq;
    out.after4=a4_count/nq;out.exact6=e6_count/nq;
    if(ground_truth)out.recall=hits/(nq*k);
    return out;
}

static std::vector<int> tam_order_to_positions(const std::vector<int>&order){
    std::vector<int>position(order.size(),-1);
    for(size_t i=0;i<order.size();++i){
        if(order[i]<0||(size_t)order[i]>=order.size()||position[order[i]]!=-1)
            throw std::runtime_error("invalid leaf permutation");
        position[order[i]]=(int)i;
    }
    return position;
}

static std::vector<int> tam_positions_to_order(const std::vector<int>&position){
    std::vector<int>order(position.size(),-1);
    for(size_t id=0;id<position.size();++id){
        if(position[id]<0||(size_t)position[id]>=position.size()||order[position[id]]!=-1)
            throw std::runtime_error("invalid position map");
        order[position[id]]=(int)id;
    }
    return order;
}

template<class Getter>
static std::vector<int> tam_spatial_order(int n,int dimensions,Getter value){
    std::vector<int>order(n);std::iota(order.begin(),order.end(),0);
    std::function<void(int,int)>rec=[&](int begin,int end){
        if(end-begin<=1)return;
        int axis=0;double best=-1;
        for(int j=0;j<dimensions;++j){
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(int z=begin;z<end;++z){double x=value(order[z],j);lo=std::min(lo,x);hi=std::max(hi,x);}
            if(hi-lo>best){best=hi-lo;axis=j;}
        }
        const int middle=begin+(end-begin)/2;
        std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,[&](int a,int b){
            const double x=value(a,axis),y=value(b,axis);return x==y?a<b:x<y;
        });
        rec(begin,middle);rec(middle,end);
    };
    rec(0,n);return order;
}

struct TAMLayout {
    std::string name,detail;
    std::vector<int>routing_position;
    std::vector<std::vector<int>>refinement_position,exact_position;
    double build_ms=0;
};

static TAMLayout tam_identity_layout(const std::vector<std::vector<int>>&lists){
    TAMLayout x;x.name="identity-append";x.detail="current-list-id/current-object-order";
    x.routing_position.resize(lists.size());std::iota(x.routing_position.begin(),x.routing_position.end(),0);
    x.refinement_position.resize(lists.size());x.exact_position.resize(lists.size());
    for(size_t c=0;c<lists.size();++c){
        x.refinement_position[c].resize(lists[c].size());std::iota(x.refinement_position[c].begin(),x.refinement_position[c].end(),0);
        x.exact_position[c]=x.refinement_position[c];
    }
    return x;
}

static TAMLayout tam_random_layout(const std::vector<std::vector<int>>&lists,uint64_t seed){
    auto x=tam_identity_layout(lists);x.name="random";x.detail="fixed-seed-independent-trees";
    std::mt19937_64 rng(seed);std::vector<int>order(lists.size());std::iota(order.begin(),order.end(),0);
    std::shuffle(order.begin(),order.end(),rng);x.routing_position=tam_order_to_positions(order);
    for(size_t c=0;c<lists.size();++c){
        order.resize(lists[c].size());std::iota(order.begin(),order.end(),0);std::shuffle(order.begin(),order.end(),rng);
        x.refinement_position[c]=tam_order_to_positions(order);
        std::shuffle(order.begin(),order.end(),rng);x.exact_position[c]=tam_order_to_positions(order);
    }
    return x;
}

static TAMLayout tam_geometry_layout(const Matrix&xb,const std::vector<std::vector<int>>&lists,
                                     const std::vector<std::vector<float>>&centroids){
    const auto start=std::chrono::steady_clock::now();auto x=tam_identity_layout(lists);
    x.name="geometry-hkmeans-aware";x.detail="centroid-kd/list-local-kd";
    auto routing=tam_spatial_order((int)lists.size(),xb.d,[&](int c,int j){return centroids[c][j];});
    x.routing_position=tam_order_to_positions(routing);
    for(size_t c=0;c<lists.size();++c){
        auto local=tam_spatial_order((int)lists[c].size(),xb.d,[&](int p,int j){return xb[lists[c][p]][j];});
        x.refinement_position[c]=tam_order_to_positions(local);x.exact_position[c]=x.refinement_position[c];
    }
    x.build_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    return x;
}

static std::vector<std::vector<int>> tam_events(int objects,const std::vector<std::vector<int>>&sets){
    std::vector<std::vector<int>>events(objects);
    for(size_t q=0;q<sets.size();++q)for(int id:sets[q]){
        if(id<0||id>=objects)throw std::runtime_error("trace member out of range");
        events[id].push_back((int)q);
    }
    return events;
}

static std::vector<int> tam_frequency_order(const std::vector<std::vector<int>>&events){
    std::vector<int>order(events.size());std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){
        return events[a].size()==events[b].size()?a<b:events[a].size()>events[b].size();
    });
    return order;
}

// MinHash tuples are compact co-access signatures: items sharing many training
// traces tend to share a prefix and therefore land in the same dyadic subtree.
static std::vector<int> tam_signature_order(const std::vector<std::vector<int>>&events,uint64_t seed){
    struct Key {std::array<uint64_t,4>h;size_t frequency;int id;};
    std::vector<Key>keys(events.size());
    for(size_t id=0;id<events.size();++id){
        Key k;k.h.fill(std::numeric_limits<uint64_t>::max());k.frequency=events[id].size();k.id=(int)id;
        for(int q:events[id])for(int z=0;z<4;++z){
            const uint64_t h=tam_mix(static_cast<uint64_t>(q)^tam_mix(seed+0x9e3779b97f4a7c15ULL*z));
            k.h[z]=std::min(k.h[z],h);
        }
        keys[id]=k;
    }
    std::sort(keys.begin(),keys.end(),[](const Key&a,const Key&b){
        const bool ae=a.frequency==0,be=b.frequency==0;if(ae!=be)return !ae;
        if(a.h!=b.h)return a.h<b.h;
        if(a.frequency!=b.frequency)return a.frequency>b.frequency;
        return a.id<b.id;
    });
    std::vector<int>order;order.reserve(keys.size());for(const auto&k:keys)order.push_back(k.id);return order;
}

static uint64_t tam_training_cost(int n,const std::vector<int>&position,
                                  const std::vector<std::vector<int>>&sets){
    uint64_t total=0;std::vector<int>mapped;
    for(const auto&set:sets){mapped.clear();mapped.reserve(set.size());for(int id:set)mapped.push_back(position[id]);
        total+=tam_dummy_proof_count(n,mapped);}
    return total;
}

struct TAMChoice {std::vector<int>position;uint64_t cost=std::numeric_limits<uint64_t>::max();std::string label;};

static TAMChoice tam_choose_layout(int n,const std::vector<std::vector<int>>&sets,
                                   const std::vector<int>&geometry_position,int candidates,uint64_t seed){
    TAMChoice best;const auto events=tam_events(n,sets);
    auto consider=[&](const std::vector<int>&order,const std::string&label){
        const auto position=tam_order_to_positions(order);const uint64_t cost=tam_training_cost(n,position,sets);
        if(cost<best.cost){best.position=position;best.cost=cost;best.label=label;}
    };
    std::vector<int>identity(n);std::iota(identity.begin(),identity.end(),0);consider(identity,"identity");
    consider(tam_positions_to_order(geometry_position),"geometry");
    consider(tam_frequency_order(events),"frequency");
    for(int z=0;z<candidates;++z)consider(tam_signature_order(events,tam_mix(seed+z)),"signature-"+std::to_string(z));
    return best;
}

struct TAMLocalWork {std::vector<std::vector<int>>after4,exact6;};

static TAMLayout tam_trace_layout(const std::vector<std::vector<int>>&lists,const TAMCorpus&train,
                                  const TAMLayout&geometry,int candidates,uint64_t seed){
    const auto start=std::chrono::steady_clock::now();auto x=tam_identity_layout(lists);
    x.name="trace-aware";
    std::vector<std::vector<int>>routing_sets;routing_sets.reserve(train.trace.size());
    std::vector<TAMLocalWork>local(lists.size());
    for(const auto&q:train.trace){
        routing_sets.push_back(q.lists);
        for(size_t u=0;u<q.lists.size();++u){const int c=q.lists[u];
            local[c].after4.push_back(q.after4[u]);local[c].exact6.push_back(q.exact6[u]);}
    }
    const auto route=tam_choose_layout((int)lists.size(),routing_sets,geometry.routing_position,candidates,seed);
    x.routing_position=route.position;size_t changed4=0,changed6=0;
    for(size_t c=0;c<lists.size();++c){
        auto a=tam_choose_layout((int)lists[c].size(),local[c].after4,geometry.refinement_position[c],
                                 candidates,tam_mix(seed^c));
        auto e=tam_choose_layout((int)lists[c].size(),local[c].exact6,geometry.exact_position[c],
                                 candidates,tam_mix(seed^c^0xd1b54a32d192ed03ULL));
        x.refinement_position[c]=std::move(a.position);x.exact_position[c]=std::move(e.position);
        changed4+=a.label!="identity";changed6+=e.label!="identity";
    }
    x.detail="route="+route.label+";changed-refine-lists="+std::to_string(changed4)+
             ";changed-exact-lists="+std::to_string(changed6);
    x.build_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    return x;
}

static void tam_validate_layout(const TAMLayout&layout,const std::vector<std::vector<int>>&lists){
    (void)tam_positions_to_order(layout.routing_position);
    if(layout.refinement_position.size()!=lists.size()||layout.exact_position.size()!=lists.size())
        throw std::runtime_error("layout/list size mismatch");
    for(size_t c=0;c<lists.size();++c){
        if(layout.refinement_position[c].size()!=lists[c].size()||layout.exact_position[c].size()!=lists[c].size())
            throw std::runtime_error("local layout size mismatch");
        (void)tam_positions_to_order(layout.refinement_position[c]);
        (void)tam_positions_to_order(layout.exact_position[c]);
    }
}

struct TAMEval {double routing_hashes=0,refinement_hashes=0,exact_hashes=0,proof_ms=0;};

static TAMEval tam_evaluate(const TAMLayout&layout,const std::vector<std::vector<int>>&lists,
                            const TAMCorpus&corpus,int repeats){
    TAMEval out;
    for(int rep=0;rep<repeats;++rep){
        uint64_t routing=0,refinement=0,exact=0;std::vector<int>mapped;
        const auto start=std::chrono::steady_clock::now();
        for(const auto&q:corpus.trace){
            mapped.clear();mapped.reserve(q.lists.size());for(int c:q.lists)mapped.push_back(layout.routing_position[c]);
            routing+=tam_dummy_proof_count(lists.size(),mapped);
            for(size_t u=0;u<q.lists.size();++u){
                const int c=q.lists[u];mapped.clear();mapped.reserve(q.after4[u].size());
                for(int p:q.after4[u])mapped.push_back(layout.refinement_position[c][p]);
                refinement+=tam_dummy_proof_count(lists[c].size(),mapped);
                mapped.clear();mapped.reserve(q.exact6[u].size());
                for(int p:q.exact6[u])mapped.push_back(layout.exact_position[c][p]);
                exact+=tam_dummy_proof_count(lists[c].size(),mapped);
            }
        }
        const auto end=std::chrono::steady_clock::now();const double nq=corpus.trace.size();
        out.routing_hashes+=routing/nq;out.refinement_hashes+=refinement/nq;out.exact_hashes+=exact/nq;
        out.proof_ms+=std::chrono::duration<double,std::milli>(end-start).count()/nq;
    }
    out.routing_hashes/=repeats;out.refinement_hashes/=repeats;out.exact_hashes/=repeats;out.proof_ms/=repeats;
    return out;
}

static double tam_reduction(double value,double baseline){return baseline?100.0*(1.0-value/baseline):0.0;}

int main(int argc,char**argv)try{
    const TAMConfig cfg=tam_args(argc,argv);tam_proof_self_test();
    std::cerr<<"Loading SIFT base, official learn queries, and held-out test queries...\n";
    const Matrix xb=fvecs(cfg.data+"/sift_base.fvecs",cfg.base);
    const Matrix train_queries=fvecs(cfg.data+"/sift_learn.fvecs",cfg.train_queries);
    const Matrix test_queries=fvecs(cfg.data+"/sift_query.fvecs",cfg.test_queries);
    if(xb.d!=train_queries.d||xb.d!=test_queries.d)throw std::runtime_error("dimension mismatch");
    std::cerr<<"Training hierarchical IVF nlist="<<cfg.nlist<<"...\n";
    const auto lists=hkmeans(xb,cfg.nlist,cfg.iters);const auto centroids=centers(xb,lists);NestedSQFast sq(xb,6);
    std::cerr<<"Computing held-out exact ground truth...\n";const auto ground_truth=exact_gt(xb,test_queries,cfg.k);
    std::cerr<<"Collecting train traces from sift_learn only...\n";
    const auto train=tam_build_traces(xb,train_queries,lists,centroids,sq,cfg.nprobe,cfg.k,nullptr);
    std::cerr<<"Collecting held-out traces from sift_query only...\n";
    const auto test=tam_build_traces(xb,test_queries,lists,centroids,sq,cfg.nprobe,cfg.k,&ground_truth);

    std::vector<TAMLayout>layouts;layouts.push_back(tam_identity_layout(lists));
    auto build_start=std::chrono::steady_clock::now();auto random=tam_random_layout(lists,cfg.seed);
    random.build_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-build_start).count();
    layouts.push_back(std::move(random));
    std::cerr<<"Building geometry/HKMeans-aware layout...\n";layouts.push_back(tam_geometry_layout(xb,lists,centroids));
    std::cerr<<"Learning trace-aware layout using train-only exact hash objectives...\n";
    layouts.push_back(tam_trace_layout(lists,train,layouts.back(),cfg.candidates,cfg.seed));
    for(const auto&layout:layouts)tam_validate_layout(layout,lists);

    std::vector<TAMEval>train_eval,test_eval;
    for(const auto&layout:layouts){train_eval.push_back(tam_evaluate(layout,lists,train,1));test_eval.push_back(tam_evaluate(layout,lists,test,cfg.repeats));}
    const auto&id=test_eval.front();const double id_local=id.refinement_hashes+id.exact_hashes;
    const double id_total=id.routing_hashes+id_local;
    const double nonhash=test.visited*(4.0+sq.bytes(4))+test.after4*sq.bytes(2)+test.exact6*512.0+cfg.nprobe*8.0;
    const double id_vo=nonhash+32.0*id_total;

    std::ofstream output(cfg.output);
    if(!output)throw std::runtime_error("cannot open output "+cfg.output);
    output<<"scheme,detail,base,nlist,nprobe,train_queries,test_queries,recall,checksum,avg_visited,avg_after4,avg_exact6,"
          <<"train_total_hash_bytes,routing_hashes,refinement_hashes,exact_hashes,local_hashes,total_hashes,"
          <<"routing_hash_bytes,base_hash_bytes,local_hash_bytes,total_hash_bytes,routing_reduction_pct,"
          <<"local_reduction_pct,total_hash_reduction_pct,nonhash_bytes,total_vo_bytes,total_vo_reduction_pct,"
          <<"proof_count_ms,query_ms,layout_build_ms\n";
    output<<std::setprecision(12);
    std::cout<<"scheme routing_KiB local_KiB hash_reduction total_VO_KiB proof_ms detail\n";
    for(size_t i=0;i<layouts.size();++i){
        const auto&e=test_eval[i];const double local=e.refinement_hashes+e.exact_hashes,total=e.routing_hashes+local;
        const double routing_bytes=32.0*e.routing_hashes,local_bytes=32.0*local,total_bytes=32.0*total;
        const double train_total=32.0*(train_eval[i].routing_hashes+train_eval[i].refinement_hashes+train_eval[i].exact_hashes);
        const double vo=nonhash+total_bytes;
        output<<layouts[i].name<<','<<layouts[i].detail<<','<<cfg.base<<','<<cfg.nlist<<','<<cfg.nprobe<<','
              <<cfg.train_queries<<','<<cfg.test_queries<<','<<test.recall<<','<<test.checksum<<','<<test.visited<<','
              <<test.after4<<','<<test.exact6<<','<<train_total<<','<<e.routing_hashes<<','<<e.refinement_hashes<<','
              <<e.exact_hashes<<','<<local<<','<<total<<','<<routing_bytes<<",0,"<<local_bytes<<','<<total_bytes<<','
              <<tam_reduction(e.routing_hashes,id.routing_hashes)<<','<<tam_reduction(local,id_local)<<','
              <<tam_reduction(total,id_total)<<','<<nonhash<<','<<vo<<','<<tam_reduction(vo,id_vo)<<','
              <<e.proof_ms<<','<<test.query_ms<<','<<layouts[i].build_ms<<'\n';
        std::cout<<layouts[i].name<<' '<<std::fixed<<std::setprecision(2)<<routing_bytes/1024<<' '
                 <<local_bytes/1024<<' '<<tam_reduction(total,id_total)<<"% "<<vo/1024<<' '
                 <<std::setprecision(4)<<e.proof_ms<<' '<<layouts[i].detail<<'\n';
    }
    std::cerr<<"Protocol assumption: independent routing/base4/refinement2/exact commitments; base4 lists are fully opened; public dummy padding.\n"
             <<"Held-out recall="<<test.recall<<", checksum="<<test.checksum<<", query="<<test.query_ms
             <<" ms; all layouts reuse the identical 4->6 traces.\nWrote "<<cfg.output<<'\n';
    return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}

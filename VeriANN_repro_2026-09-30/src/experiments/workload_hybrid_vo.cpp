#define VO_LATENCY_QUANTIZATION_NO_MAIN
#include "vo_latency_quantization.cpp"
#undef VO_LATENCY_QUANTIZATION_NO_MAIN

#include <functional>
#include <random>

// Joint held-out experiment:
//   verification-trained nested SQ4->SQ6
// + workload-selected authenticated list-ball certificates
// + structure/trace-aware routing Merkle layout.
//
// A list certificate is a 4-bit nested code for its centroid plus an
// outward-rounded covering radius.  The owner authenticates it with the list
// roots.  If dist(q, centroid-cell)-radius > tau, the complete posting list is
// safely excluded without disclosing any per-object SQ4 records.

struct HVConfig {
    std::string data="../dataset/sift",output="workload_hybrid_vo.csv";
    int base=100000,train_queries=200,test_queries=100,k=100;
    int nlist=16384,nprobe=640,coarse_nlist=0,coarse_nprobe=0,iters=6,route_candidates=16,repeats=1;
    uint64_t seed=20260825;
};

static HVConfig hv_args(int argc,char**argv){
    HVConfig c;
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
        else if(a=="--coarse-nlist")c.coarse_nlist=std::stoi(value());
        else if(a=="--coarse-nprobe")c.coarse_nprobe=std::stoi(value());
        else if(a=="--iters")c.iters=std::stoi(value());
        else if(a=="--route-candidates")c.route_candidates=std::stoi(value());
        else if(a=="--repeats")c.repeats=std::stoi(value());
        else if(a=="--seed")c.seed=std::stoull(value());
        else throw std::runtime_error("unknown option "+a);
    }
    if(c.base<=0||c.train_queries<2||c.test_queries<=0||c.k<=0||c.nlist<=0||
       c.nprobe<=0||c.nprobe>c.nlist||c.nlist>c.base||c.route_candidates<1||c.repeats<1||
       ((c.coarse_nlist==0)!=(c.coarse_nprobe==0))||c.coarse_nlist<0||c.coarse_nprobe<0||
       c.coarse_nlist>c.nlist||c.coarse_nprobe>c.coarse_nlist)
        throw std::runtime_error("invalid numeric configuration");
    return c;
}

static std::vector<VLState> hv_states(const Matrix&xb,const Matrix&q,
        const std::vector<std::vector<int>>&lists,const std::vector<std::vector<float>>&centroids,
        int nprobe,int k,const std::vector<std::vector<int>>*ground_truth,double*recall){
    std::vector<VLState>out(q.rows());uint64_t hits=0;
    for(size_t qi=0;qi<q.rows();++qi){
        std::vector<int>order(lists.size());std::iota(order.begin(),order.end(),0);
        std::partial_sort(order.begin(),order.begin()+nprobe,order.end(),[&](int a,int b){
            const float x=l2(q[qi],centroids[a].data(),xb.d),y=l2(q[qi],centroids[b].data(),xb.d);
            return x==y?a<b:x<y;
        });
        auto&s=out[qi];s.lists.assign(order.begin(),order.begin()+nprobe);
        std::priority_queue<std::pair<float,int>>top;
        for(int c:s.lists)for(int id:lists[c]){
            const float d=l2(q[qi],xb[id],xb.d);
            if((int)top.size()<k)top.push({d,id});
            else if(d<top.top().first){top.pop();top.push({d,id});}
        }
        if((int)top.size()!=k)throw std::runtime_error("fewer than k candidates");
        s.tau2=top.top().first;
        while(!top.empty()){s.answer.push_back(top.top().second);top.pop();}
        if(ground_truth){std::unordered_set<int>a(s.answer.begin(),s.answer.end());for(int id:(*ground_truth)[qi])hits+=a.count(id);}
    }
    if(recall)*recall=ground_truth?hits/(q.rows()*(double)k):0.0;
    return out;
}

static std::vector<VLState> hv_hierarchical_states(const Matrix&xb,const Matrix&q,
        const std::vector<std::vector<int>>&lists,const std::vector<std::vector<float>>&centroids,
        const std::vector<std::vector<int>>&groups,const std::vector<std::vector<float>>&coarse_centroids,
        int coarse_nprobe,int nprobe,int k,const std::vector<std::vector<int>>*ground_truth,double*recall){
    std::vector<VLState>out(q.rows());uint64_t hits=0;
    for(size_t qi=0;qi<q.rows();++qi){
        std::vector<int>coarse_order(groups.size());std::iota(coarse_order.begin(),coarse_order.end(),0);
        std::partial_sort(coarse_order.begin(),coarse_order.begin()+coarse_nprobe,coarse_order.end(),[&](int a,int b){
            const float x=l2(q[qi],coarse_centroids[a].data(),xb.d),y=l2(q[qi],coarse_centroids[b].data(),xb.d);
            return x==y?a<b:x<y;
        });
        std::vector<int>candidates;
        for(int z=0;z<coarse_nprobe;++z)candidates.insert(candidates.end(),groups[coarse_order[z]].begin(),groups[coarse_order[z]].end());
        if((int)candidates.size()<nprobe)throw std::runtime_error("coarse shortlist is smaller than nprobe");
        std::partial_sort(candidates.begin(),candidates.begin()+nprobe,candidates.end(),[&](int a,int b){
            const float x=l2(q[qi],centroids[a].data(),xb.d),y=l2(q[qi],centroids[b].data(),xb.d);
            return x==y?a<b:x<y;
        });
        auto&s=out[qi];s.lists.assign(candidates.begin(),candidates.begin()+nprobe);
        std::priority_queue<std::pair<float,int>>top;
        for(int c:s.lists)for(int id:lists[c]){
            const float d=l2(q[qi],xb[id],xb.d);
            if((int)top.size()<k)top.push({d,id});
            else if(d<top.top().first||(d==top.top().first&&id<top.top().second)){top.pop();top.push({d,id});}
        }
        if((int)top.size()!=k)throw std::runtime_error("fewer than k hierarchical candidates");
        s.tau2=top.top().first;
        while(!top.empty()){s.answer.push_back(top.top().second);top.pop();}
        if(ground_truth){std::unordered_set<int>a(s.answer.begin(),s.answer.end());for(int id:(*ground_truth)[qi])hits+=a.count(id);}
    }
    if(recall)*recall=ground_truth?hits/(q.rows()*(double)k):0.0;
    return out;
}

struct HVSummary {
    std::vector<uint8_t>code,lo6,hi6;
    double radius=0;
};

static std::vector<HVSummary> hv_summaries(const Matrix&xb,const std::vector<std::vector<int>>&lists,
        const std::vector<std::vector<float>>&centroids,const NestedRule&q){
    std::vector<HVSummary>out(lists.size());
    for(size_t c=0;c<lists.size();++c){
        auto&s=out[c];s.code.resize(xb.d);s.lo6.assign(xb.d,63);s.hi6.assign(xb.d,0);double r2=0;
        for(int id:lists[c]){double z=0;for(int j=0;j<xb.d;++j){double d=(double)xb[id][j]-centroids[c][j];z+=d*d;
                uint8_t code=q.code[(size_t)id*xb.d+j];s.lo6[j]=std::min(s.lo6[j],code);s.hi6[j]=std::max(s.hi6[j],code);}r2=std::max(r2,z);}
        s.radius=std::nextafter(std::sqrt(r2)*(1.0+1e-12)+1e-9,std::numeric_limits<double>::infinity());
        for(int j=0;j<xb.d;++j){
            const double*lo=q.boundary.data()+(size_t)j*65+1,*hi=q.boundary.data()+(size_t)j*65+64;
            int code=(int)(std::upper_bound(lo,hi,(double)centroids[c][j])-lo);
            s.code[j]=(uint8_t)std::clamp(code,0,63);
            const double a=q.boundary[(size_t)j*65+s.code[j]],b=q.boundary[(size_t)j*65+s.code[j]+1];
            if((double)centroids[c][j]<a||(double)centroids[c][j]>b)throw std::runtime_error("summary centroid outside cell");
        }
    }
    return out;
}

static double hv_block_lower2(const HVSummary&s,const NestedRule::LUT&lut){
    double sum=0;for(size_t j=0;j<s.code.size();++j)sum+=lut.term[lut.off[j]+(s.code[j]>>lut.shift[j])];
    double root=std::nextafter(std::sqrt(sum),0.0),lb=std::max(0.0,root-s.radius);
    lb=std::nextafter(lb,0.0);return std::nextafter(lb*lb,0.0);
}

static double hv_box_lower2(const float*query,const HVSummary&s,const NestedRule&q,int bits){
    const int shift=6-bits;double sum=0;
    for(int j=0;j<q.d;++j){int lo=s.lo6[j]>>shift,hi=s.hi6[j]>>shift;
        double a=std::nextafter(q.boundary[(size_t)j*65+(lo<<shift)],-std::numeric_limits<double>::infinity());
        double b=std::nextafter(q.boundary[(size_t)j*65+((hi+1)<<shift)],std::numeric_limits<double>::infinity());
        double v=query[j]<a?a-query[j]:(query[j]>b?query[j]-b:0.0),term=v*v;
        float safe=(float)term;if((double)safe>term)safe=std::nextafter(safe,0.0f);sum+=std::nextafter(safe,0.0f);
    }
    return std::nextafter(sum,0.0);
}

static int hv_summary_bytes(int type){
    if(type==1)return 68;
    if(type>=4&&type<=6)return (2*128*type+7)/8;
    return 0;
}

static bool hv_summary_prunes(int type,const float*query,double tau2,const HVSummary&s,
        const NestedRule&q,const NestedRule::LUT&q4){
    if(type==1)return hv_block_lower2(s,q4)>tau2;
    if(type>=4&&type<=6)return hv_box_lower2(query,s,q,type)>tau2;
    return false;
}

static uint64_t hv_mix(uint64_t x){
    x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);
}

// Complete Merkle trees are padded to a power of two.  Padding leaves are
// protocol constants, so their hashes are locally derivable and need not be
// transmitted in a multiproof.
static size_t hv_dummy_proof_count(size_t n,const std::vector<int>&selected){
    if(!n)return 0;
    size_t padded=1;while(padded<n)padded<<=1;
    std::vector<size_t>cur;cur.reserve(selected.size());
    for(int x:selected){
        if(x<0||(size_t)x>=n)throw std::runtime_error("proof position out of range");
        cur.push_back((size_t)x);
    }
    std::sort(cur.begin(),cur.end());cur.erase(std::unique(cur.begin(),cur.end()),cur.end());
    if(cur.empty())return 1;
    size_t hashes=0,width=1;
    while(padded>1){
        std::vector<size_t>parents;parents.reserve((cur.size()+1)/2);
        for(size_t i=0;i<cur.size();){
            const size_t x=cur[i];
            if((x&1U)==0&&i+1<cur.size()&&cur[i+1]==x+1)i+=2;
            else{if((x^1U)*width<n)++hashes;++i;}
            const size_t parent=x>>1;
            if(parents.empty()||parents.back()!=parent)parents.push_back(parent);
        }
        cur.swap(parents);padded>>=1;width<<=1;
    }
    return hashes;
}

static std::vector<int> hv_positions(const std::vector<int>&order){
    std::vector<int>p(order.size(),-1);for(size_t i=0;i<order.size();++i)p[order[i]]=(int)i;return p;
}

template<class Getter>
static std::vector<int> hv_spatial_order(int n,int dimensions,Getter value){
    std::vector<int>order(n);std::iota(order.begin(),order.end(),0);
    std::function<void(int,int)>split=[&](int begin,int end){
        if(end-begin<=1)return;int axis=0;double best=-1;
        for(int j=0;j<dimensions;++j){double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(int z=begin;z<end;++z){double v=value(order[z],j);lo=std::min(lo,v);hi=std::max(hi,v);}if(hi-lo>best){best=hi-lo;axis=j;}}
        int mid=begin+(end-begin)/2;std::nth_element(order.begin()+begin,order.begin()+mid,order.begin()+end,[&](int a,int b){
            double x=value(a,axis),y=value(b,axis);return x==y?a<b:x<y;});split(begin,mid);split(mid,end);
    };split(0,n);return order;
}

static uint64_t hv_route_cost(const std::vector<VLState>&states,const std::vector<int>&position,bool public_dummy){
    uint64_t total=0;std::vector<int>mapped;
    for(const auto&s:states){mapped.clear();mapped.reserve(s.lists.size());for(int c:s.lists)mapped.push_back(position[c]);
        total+=public_dummy?hv_dummy_proof_count(position.size(),mapped):proof_count(position.size(),mapped);}
    return total;
}

static std::vector<int> hv_trace_order(int n,const std::vector<VLState>&states,uint64_t seed){
    struct Key{std::array<uint64_t,4>h;int frequency=0,id=0;};
    std::vector<Key>keys(n);for(int id=0;id<n;++id){keys[id].h.fill(std::numeric_limits<uint64_t>::max());keys[id].id=id;}
    for(size_t qi=0;qi<states.size();++qi)for(int id:states[qi].lists){auto&k=keys[id];++k.frequency;
        for(int z=0;z<4;++z)k.h[z]=std::min(k.h[z],hv_mix(qi^hv_mix(seed+0x9e3779b97f4a7c15ULL*z)));}
    std::sort(keys.begin(),keys.end(),[](const Key&a,const Key&b){
        if((a.frequency==0)!=(b.frequency==0))return a.frequency!=0;
        if(a.h!=b.h)return a.h<b.h;if(a.frequency!=b.frequency)return a.frequency>b.frequency;return a.id<b.id;});
    std::vector<int>order;order.reserve(n);for(const auto&k:keys)order.push_back(k.id);return order;
}

struct HVLayout {std::string name;std::vector<int>routing_position;uint64_t train_hashes=0;};

static std::vector<HVLayout> hv_layouts(const Matrix&xb,const std::vector<std::vector<float>>&centroids,
        const std::vector<VLState>&train,int candidates,uint64_t seed){
    std::vector<HVLayout>out;std::vector<int>identity(centroids.size());std::iota(identity.begin(),identity.end(),0);
    auto add=[&](std::string name,const std::vector<int>&order){HVLayout x;x.name=std::move(name);x.routing_position=hv_positions(order);x.train_hashes=hv_route_cost(train,x.routing_position,false);out.push_back(std::move(x));};
    add("identity",identity);
    auto geometry=hv_spatial_order((int)centroids.size(),xb.d,[&](int c,int j){return centroids[c][j];});add("geometry",geometry);
    std::vector<int>frequency=identity;std::vector<int>count(centroids.size());for(const auto&s:train)for(int c:s.lists)++count[c];
    std::stable_sort(frequency.begin(),frequency.end(),[&](int a,int b){return count[a]==count[b]?a<b:count[a]>count[b];});add("frequency",frequency);
    for(int z=0;z<candidates;++z)add("trace-"+std::to_string(z),hv_trace_order((int)centroids.size(),train,hv_mix(seed+z)));
    auto best=std::min_element(out.begin()+2,out.end(),[](const HVLayout&a,const HVLayout&b){return a.train_hashes<b.train_hashes;});
    HVLayout learned=*best;learned.name="trace-aware("+best->name+")";
    // Public-dummy proofs have a different cost surface from ordinary
    // multiproofs. Geometry can beat the workload signatures once leaves
    // become microclusters, so select over every available layout.
    auto dummy_best=std::min_element(out.begin(),out.end(),[&](const HVLayout&a,const HVLayout&b){
        return hv_route_cost(train,a.routing_position,true)<hv_route_cost(train,b.routing_position,true);});
    HVLayout dummy=*dummy_best;dummy.name="trace-aware-public-dummy("+dummy_best->name+")";
    dummy.train_hashes=hv_route_cost(train,dummy.routing_position,true);
    return {out[0],out[1],std::move(learned),std::move(dummy)};
}

static double hv_list_progressive_cost(const Matrix&xb,const float*query,double tau2,
        const std::vector<int>&list,const NestedRule&q,const NestedRule::LUT&q4,const NestedRule::LUT&q6){
    std::vector<int>a4,a6;a4.reserve(list.size());a6.reserve(list.size());
    for(size_t p=0;p<list.size();++p){bool x4=q.overlaps(list[p],tau2,q4),x6=q.overlaps(list[p],tau2,q6);
        if(x6&&!x4)throw std::runtime_error("non-nested object certificate");if(x4)a4.push_back((int)p);if(x6)a6.push_back((int)p);}
    return list.size()*68.0+a4.size()*32.0+a6.size()*512.0+
           proof_count(list.size(),a4)*32.0+proof_count(list.size(),a6)*32.0;
}

struct HVPlanStat {uint32_t visits=0;std::array<double,4>gain{};};

static std::vector<HVPlanStat> hv_plan_stats(const Matrix&xb,const Matrix&queries,const std::vector<VLState>&states,
        int begin,int end,const std::vector<std::vector<int>>&lists,const NestedRule&q,const std::vector<HVSummary>&summary){
    static const std::array<int,4>types{1,4,5,6};
    std::vector<HVPlanStat>stats(lists.size());auto b4=uniform_bits(xb.d,4),b6=uniform_bits(xb.d,6);
    for(int qi=begin;qi<end;++qi){auto q4=q.prepare(queries[qi],b4),q6=q.prepare(queries[qi],b6);
        for(int c:states[qi].lists){auto&s=stats[c];++s.visits;
            double base=hv_list_progressive_cost(xb,queries[qi],states[qi].tau2,lists[c],q,q4,q6);
            for(size_t z=0;z<types.size();++z){int type=types[z],bytes=hv_summary_bytes(type);
                s.gain[z]+=hv_summary_prunes(type,queries[qi],states[qi].tau2,summary[c],q,q4)?base-bytes:-bytes;}}}
    return stats;
}

static std::vector<uint8_t> hv_make_plan(const std::vector<HVPlanStat>&stats,double margin){
    static const std::array<int,4>types{1,4,5,6};std::vector<uint8_t>plan(stats.size());
    for(size_t c=0;c<stats.size();++c)if(stats[c].visits>=2){double best=margin;int choice=0;
        for(size_t z=0;z<types.size();++z){double gain=stats[c].gain[z]/stats[c].visits;if(gain>best){best=gain;choice=types[z];}}plan[c]=(uint8_t)choice;}
    return plan;
}

struct HVEval {
    double total=0,summary_payload=0,base_payload=0,refine_payload=0,exact_payload=0,local=0,routing=0;
    double refinement_merkle=0,exact_merkle=0;
    double visited=0,pruned_lists=0,pruned_vectors=0,after4=0,exact=0,enabled_visits=0,ms=0;
    uint64_t block_violations=0,object_violations=0;
};

struct HVLosslessTail {
    std::vector<uint16_t>bytes;
    double average_bytes=0;
    int max_cell_values=0;
};

static int hv_rank_bits(int values){
    int bits=0,capacity=1;while(capacity<values){capacity<<=1;++bits;}return bits;
}

// The SIFT coordinates are integer-valued.  Given an authenticated 6-bit VT
// cell, encode only the rank of the original integer inside that cell.  The
// public VT boundaries determine the rank alphabet, so this tail is lossless
// and needs no learned codebook in the VO.
static HVLosslessTail hv_lossless_tail(const Matrix&xb,const NestedRule&q){
    using Support=std::array<std::vector<uint8_t>,64>;
    std::vector<Support>support(xb.d);
    for(int j=0;j<xb.d;++j){
        int lo=(int)std::ceil(q.boundary[(size_t)j*65]),hi=(int)std::floor(q.boundary[(size_t)j*65+64]);
        lo=std::clamp(lo,0,255);hi=std::clamp(hi,0,255);
        for(int value=lo;value<=hi;++value){
            const double*first=q.boundary.data()+(size_t)j*65+1,*last=q.boundary.data()+(size_t)j*65+64;
            int code=(int)(std::upper_bound(first,last,(double)value)-first);
            support[j][std::clamp(code,0,63)].push_back((uint8_t)value);
        }
    }
    HVLosslessTail out;out.bytes.resize(xb.rows());uint64_t total=0;
    for(size_t id=0;id<xb.rows();++id){int bits=0;std::vector<int>rank(xb.d),width(xb.d);
        for(int j=0;j<xb.d;++j){
            const double value=xb[id][j];const int integer=(int)std::lround(value);
            if(integer<0||integer>255||std::abs(value-integer)>1e-6)throw std::runtime_error("lossless tail requires uint8 coordinates");
            const int code=q.code[id*(size_t)xb.d+j];const auto&alphabet=support[j][code];
            auto it=std::lower_bound(alphabet.begin(),alphabet.end(),(uint8_t)integer);
            if(it==alphabet.end()||*it!=integer)throw std::runtime_error("lossless tail reconstruction failed");
            rank[j]=(int)(it-alphabet.begin());width[j]=hv_rank_bits((int)alphabet.size());
            if(alphabet[rank[j]]!=integer)throw std::runtime_error("lossless tail rank mismatch");
            bits+=width[j];out.max_cell_values=std::max(out.max_cell_values,(int)alphabet.size());
        }
        out.bytes[id]=(uint16_t)((bits+7)/8);std::vector<uint8_t>packed(out.bytes[id]);int offset=0;
        for(int j=0;j<xb.d;++j)for(int b=0;b<width[j];++b,++offset)if(rank[j]&(1<<b))packed[offset>>3]|=(uint8_t)(1U<<(offset&7));
        offset=0;
        for(int j=0;j<xb.d;++j){int decoded_rank=0;
            for(int b=0;b<width[j];++b,++offset)decoded_rank|=((packed[offset>>3]>>(offset&7))&1U)<<b;
            const int code=q.code[id*(size_t)xb.d+j];const auto&alphabet=support[j][code];
            if(decoded_rank>=(int)alphabet.size()||alphabet[decoded_rank]!=(int)std::lround(xb[id][j]))
                throw std::runtime_error("lossless tail pack/decode self-test failed");
        }
        total+=out.bytes[id];
    }
    out.average_bytes=total/(double)xb.rows();return out;
}

struct HVProtocol {
    bool lossless_tail=false;
    bool positional_ids=false;
    bool public_dummy=false;
    bool fused_commitment=false;
};

static HVEval hv_evaluate(const Matrix&xb,const Matrix&queries,const std::vector<VLState>&states,int begin,int end,
        const std::vector<std::vector<int>>&lists,const NestedRule&q,const std::vector<HVSummary>&summary,
        const std::vector<uint8_t>&plan,const HVLayout&layout,int repeats,bool safety_check){
    HVEval out;auto b4=uniform_bits(xb.d,4),b6=uniform_bits(xb.d,6);
    for(int rep=0;rep<repeats;++rep){HVEval e;auto start=std::chrono::steady_clock::now();std::vector<int>mapped,a4,a6;
        for(int qi=begin;qi<end;++qi){auto q4=q.prepare(queries[qi],b4),q6=q.prepare(queries[qi],b6);mapped.clear();
            for(int c:states[qi].lists)mapped.push_back(layout.routing_position[c]);e.routing+=proof_count(lists.size(),mapped)*32.0+states[qi].lists.size()*8.0;
            for(int c:states[qi].lists){const auto&list=lists[c];e.visited+=list.size();bool pruned=false;
                if(plan[c]){++e.enabled_visits;e.summary_payload+=hv_summary_bytes(plan[c]);
                    pruned=hv_summary_prunes(plan[c],queries[qi],states[qi].tau2,summary[c],q,q4);
                    if(pruned){++e.pruned_lists;e.pruned_vectors+=list.size();if(safety_check)for(int id:list)if(l2(queries[qi],xb[id],xb.d)<=states[qi].tau2)++e.block_violations;}}
                if(pruned)continue;e.base_payload+=list.size()*68.0;a4.clear();a6.clear();
                for(size_t p=0;p<list.size();++p){int id=list[p];bool x4=q.overlaps(id,states[qi].tau2,q4),x6=q.overlaps(id,states[qi].tau2,q6);
                    if(x6&&!x4)throw std::runtime_error("non-nested evaluation trace");if(x4)a4.push_back((int)p);if(x6)a6.push_back((int)p);
                    if(safety_check){double exact_distance=l2(queries[qi],xb[id],xb.d);e.object_violations+=q.lower2(id,q4)>exact_distance||q.lower2(id,q6)>exact_distance;}}
                e.after4+=a4.size();e.exact+=a6.size();e.refine_payload+=a4.size()*32.0;e.exact_payload+=a6.size()*512.0;
                e.local+=proof_count(list.size(),a4)*32.0+proof_count(list.size(),a6)*32.0;
            }
        }
        e.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();double n=end-begin;
        e.summary_payload/=n;e.base_payload/=n;e.refine_payload/=n;e.exact_payload/=n;e.local/=n;e.routing/=n;e.visited/=n;e.pruned_lists/=n;e.pruned_vectors/=n;e.after4/=n;e.exact/=n;e.enabled_visits/=n;e.ms/=n;
        out.summary_payload+=e.summary_payload;out.base_payload+=e.base_payload;out.refine_payload+=e.refine_payload;out.exact_payload+=e.exact_payload;out.local+=e.local;out.routing+=e.routing;
        out.visited+=e.visited;out.pruned_lists+=e.pruned_lists;out.pruned_vectors+=e.pruned_vectors;out.after4+=e.after4;out.exact+=e.exact;out.enabled_visits+=e.enabled_visits;out.ms+=e.ms;
        out.block_violations+=e.block_violations;out.object_violations+=e.object_violations;
    }
    double r=repeats;out.summary_payload/=r;out.base_payload/=r;out.refine_payload/=r;out.exact_payload/=r;out.local/=r;out.routing/=r;out.visited/=r;out.pruned_lists/=r;out.pruned_vectors/=r;out.after4/=r;out.exact/=r;out.enabled_visits/=r;out.ms/=r;
    out.total=out.summary_payload+out.base_payload+out.refine_payload+out.exact_payload+out.local+out.routing;return out;
}

struct HVResidualSQ {
    int d=0,max_bits=6,bins=64;
    std::vector<uint8_t>code;
    const std::vector<std::vector<float>>&centroids;
    const std::vector<HVSummary>&summary;

    HVResidualSQ(const Matrix&xb,const std::vector<std::vector<int>>&lists,
            const std::vector<std::vector<float>>&centers,const std::vector<HVSummary>&summaries)
      :d(xb.d),code(xb.rows()*(size_t)xb.d),centroids(centers),summary(summaries){
        for(size_t c=0;c<lists.size();++c){double radius=summary[c].radius;
            for(int id:lists[c])for(int j=0;j<d;++j){double u=((double)xb[id][j]-centroids[c][j])/radius;
                int z=std::clamp((int)std::floor((u+1.0)*0.5*bins),0,bins-1);code[(size_t)id*d+j]=(uint8_t)z;
                double a=-1.0+2.0*z/bins,b=-1.0+2.0*(z+1)/bins;
                if(u<a-1e-12||u>b+1e-12)throw std::runtime_error("normalized residual outside code cell");}}
    }

    int bytes(int bits)const{return(d*bits+7)/8;}

    double lower2(const float*query,int list,int id,int bits)const{
        int shift=max_bits-bits,n=1<<bits;double radius=summary[list].radius,sum=0;size_t off=(size_t)id*d;
        for(int j=0;j<d;++j){int prefix=code[off+j]>>shift;
            double a=centroids[list][j]+radius*(-1.0+2.0*prefix/n);
            double b=centroids[list][j]+radius*(-1.0+2.0*(prefix+1)/n);
            a=std::nextafter(a,-std::numeric_limits<double>::infinity());b=std::nextafter(b,std::numeric_limits<double>::infinity());
            double v=query[j]<a?a-query[j]:(query[j]>b?query[j]-b:0.0),term=v*v;
            float safe=(float)term;if((double)safe>term)safe=std::nextafter(safe,0.0f);sum+=std::nextafter(safe,0.0f);
        }
        return std::nextafter(sum,0.0);
    }
};

static HVEval hv_residual_evaluate(const Matrix&xb,const Matrix&queries,const std::vector<VLState>&states,int begin,int end,
        const std::vector<std::vector<int>>&lists,const HVResidualSQ&q,const std::vector<int>&levels,
        const HVLayout&layout,int repeats,bool safety_check){
    HVEval out;
    for(int rep=0;rep<repeats;++rep){HVEval e;auto start=std::chrono::steady_clock::now();std::vector<int>mapped,active,next;
        for(int qi=begin;qi<end;++qi){mapped.clear();for(int c:states[qi].lists)mapped.push_back(layout.routing_position[c]);
            e.routing+=proof_count(lists.size(),mapped)*32.0+states[qi].lists.size()*8.0;e.summary_payload+=states[qi].lists.size()*4.0;
            for(int c:states[qi].lists){const auto&list=lists[c];e.visited+=list.size();e.base_payload+=list.size()*(4.0+q.bytes(levels[0]));active.clear();
                for(size_t p=0;p<list.size();++p){int id=list[p];double lb=q.lower2(queries[qi],c,id,levels[0]);if(lb<=states[qi].tau2)active.push_back((int)p);
                    if(safety_check)e.object_violations+=lb>l2(queries[qi],xb[id],xb.d);}
                e.after4+=active.size();
                for(size_t z=1;z<levels.size();++z){e.local+=proof_count(list.size(),active)*32.0;
                    e.refine_payload+=active.size()*q.bytes(levels[z]-levels[z-1]);next.clear();
                    for(int p:active)if(q.lower2(queries[qi],c,list[p],levels[z])<=states[qi].tau2)next.push_back(p);active.swap(next);}
                e.exact+=active.size();e.exact_payload+=active.size()*512.0;e.local+=proof_count(list.size(),active)*32.0;
            }
        }
        e.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();double n=end-begin;
        e.summary_payload/=n;e.base_payload/=n;e.refine_payload/=n;e.exact_payload/=n;e.local/=n;e.routing/=n;e.visited/=n;e.after4/=n;e.exact/=n;e.ms/=n;
        out.summary_payload+=e.summary_payload;out.base_payload+=e.base_payload;out.refine_payload+=e.refine_payload;out.exact_payload+=e.exact_payload;out.local+=e.local;out.routing+=e.routing;out.visited+=e.visited;out.after4+=e.after4;out.exact+=e.exact;out.ms+=e.ms;out.object_violations+=e.object_violations;
    }
    double r=repeats;out.summary_payload/=r;out.base_payload/=r;out.refine_payload/=r;out.exact_payload/=r;out.local/=r;out.routing/=r;out.visited/=r;out.after4/=r;out.exact/=r;out.ms/=r;
    out.total=out.summary_payload+out.base_payload+out.refine_payload+out.exact_payload+out.local+out.routing;return out;
}

struct HVObjectStat {uint32_t visits=0;std::array<uint32_t,7>active{};};

static std::vector<int> hv_downgrade_order(const Matrix&xb,const Matrix&queries,
        const std::vector<VLState>&states,int begin,int end,const std::vector<std::vector<int>>&lists,const NestedRule&q){
    auto b3=uniform_bits(xb.d,3),b4=uniform_bits(xb.d,4);std::vector<double>harm(xb.d);
    for(int qi=begin;qi<end;++qi){auto q3=q.prepare(queries[qi],b3),q4=q.prepare(queries[qi],b4);std::vector<double>c3(xb.d),c4(xb.d);
        for(int c:states[qi].lists)for(int id:lists[c]){size_t off=(size_t)id*xb.d;double sum4=0;
            for(int j=0;j<xb.d;++j){int code=q.code[off+j];c3[j]=q3.term[q3.off[j]+(code>>q3.shift[j])];c4[j]=q4.term[q4.off[j]+(code>>q4.shift[j])];sum4+=c4[j];}
            if(sum4<=states[qi].tau2)continue;double margin=sum4-states[qi].tau2;
            for(int j=0;j<xb.d;++j){double loss=std::max(0.0,c4[j]-c3[j]);harm[j]+=loss/(margin+loss+1.0);}}}
    std::vector<int>order(xb.d);std::iota(order.begin(),order.end(),0);std::stable_sort(order.begin(),order.end(),[&](int a,int b){return harm[a]==harm[b]?a<b:harm[a]<harm[b];});return order;
}

static std::vector<int> hv_downgrade_bits(int d,const std::vector<int>&order,int downgraded){
    auto bits=uniform_bits(d,4);for(int z=0;z<std::min(d,downgraded);++z)bits[order[z]]=3;return bits;
}

static HVEval hv_bits_evaluate(const Matrix&xb,const Matrix&queries,const std::vector<VLState>&states,int begin,int end,
        const std::vector<std::vector<int>>&lists,const NestedRule&q,const std::vector<int>&base_bits,
        const HVLayout&layout,int repeats,bool safety_check){
    HVEval out;auto max_bits=uniform_bits(xb.d,6);int base_bytes=(bit_sum(base_bits)+7)/8,delta_bytes=(bit_sum(max_bits)-bit_sum(base_bits)+7)/8;
    for(int rep=0;rep<repeats;++rep){HVEval e;auto start=std::chrono::steady_clock::now();std::vector<int>mapped,refine,exact;
        for(int qi=begin;qi<end;++qi){auto first=q.prepare(queries[qi],base_bits),last=q.prepare(queries[qi],max_bits);mapped.clear();
            for(int c:states[qi].lists)mapped.push_back(layout.routing_position[c]);e.routing+=proof_count(lists.size(),mapped)*32.0+states[qi].lists.size()*8.0;
            for(int c:states[qi].lists){const auto&list=lists[c];e.visited+=list.size();e.base_payload+=list.size()*(4.0+base_bytes);refine.clear();exact.clear();
                for(size_t p=0;p<list.size();++p){int id=list[p];bool pass=q.overlaps(id,states[qi].tau2,first);if(pass){refine.push_back((int)p);pass=q.overlaps(id,states[qi].tau2,last);}if(pass)exact.push_back((int)p);
                    if(safety_check){double distance=l2(queries[qi],xb[id],xb.d);e.object_violations+=q.lower2(id,first)>distance||q.lower2(id,last)>distance;}}
                e.after4+=refine.size();e.exact+=exact.size();e.refine_payload+=refine.size()*delta_bytes;e.exact_payload+=exact.size()*512.0;
                e.local+=proof_count(list.size(),refine)*32.0+proof_count(list.size(),exact)*32.0;
            }
        }
        e.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();double n=end-begin;
        e.base_payload/=n;e.refine_payload/=n;e.exact_payload/=n;e.local/=n;e.routing/=n;e.visited/=n;e.after4/=n;e.exact/=n;e.ms/=n;
        out.base_payload+=e.base_payload;out.refine_payload+=e.refine_payload;out.exact_payload+=e.exact_payload;out.local+=e.local;out.routing+=e.routing;out.visited+=e.visited;out.after4+=e.after4;out.exact+=e.exact;out.ms+=e.ms;out.object_violations+=e.object_violations;
    }
    double r=repeats;out.base_payload/=r;out.refine_payload/=r;out.exact_payload/=r;out.local/=r;out.routing/=r;out.visited/=r;out.after4/=r;out.exact/=r;out.ms/=r;
    out.total=out.base_payload+out.refine_payload+out.exact_payload+out.local+out.routing;return out;
}

static HVEval hv_protocol_evaluate(const Matrix&xb,const Matrix&queries,const std::vector<VLState>&states,int begin,int end,
        const std::vector<std::vector<int>>&lists,const NestedRule&q,const std::vector<int>&base_bits,
        const HVLayout&layout,const HVLosslessTail&tail,const HVProtocol&protocol,int repeats,bool safety_check){
    HVEval out;auto max_bits=uniform_bits(xb.d,6);
    const int base_bytes=(bit_sum(base_bits)+7)/8;
    const int delta_bytes=(bit_sum(max_bits)-bit_sum(base_bits)+7)/8;
    auto hashes=[&](size_t n,const std::vector<int>&selected){
        return protocol.public_dummy?hv_dummy_proof_count(n,selected):proof_count(n,selected);
    };
    for(int rep=0;rep<repeats;++rep){
        HVEval e;auto start=std::chrono::steady_clock::now();std::vector<int>mapped,refine,exact;
        for(int qi=begin;qi<end;++qi){
            auto first=q.prepare(queries[qi],base_bits),last=q.prepare(queries[qi],max_bits);mapped.clear();
            for(int c:states[qi].lists)mapped.push_back(layout.routing_position[c]);
            e.routing+=hashes(lists.size(),mapped)*32.0+states[qi].lists.size()*8.0;
            for(int c:states[qi].lists){
                const auto&list=lists[c];e.visited+=list.size();
                e.base_payload+=list.size()*(base_bytes+(protocol.positional_ids?0.0:4.0));
                refine.clear();exact.clear();
                for(size_t p=0;p<list.size();++p){
                    int id=list[p];bool pass=q.overlaps(id,states[qi].tau2,first);
                    if(pass){refine.push_back((int)p);pass=q.overlaps(id,states[qi].tau2,last);}
                    if(pass)exact.push_back((int)p);
                    if(safety_check){
                        double distance=l2(queries[qi],xb[id],xb.d);
                        e.object_violations+=q.lower2(id,first)>distance||q.lower2(id,last)>distance;
                    }
                }
                e.after4+=refine.size();e.exact+=exact.size();
                e.refine_payload+=refine.size()*(delta_bytes+(protocol.fused_commitment?32.0:0.0));
                for(int p:exact){int id=list[p];e.exact_payload+=protocol.lossless_tail?tail.bytes[id]:512.0;
                    if(protocol.positional_ids)e.exact_payload+=4.0;}
                e.refinement_merkle+=hashes(list.size(),refine)*32.0;
                if(!protocol.fused_commitment)e.exact_merkle+=hashes(list.size(),exact)*32.0;
            }
        }
        e.local=e.refinement_merkle+e.exact_merkle;
        e.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        double n=end-begin;
        e.base_payload/=n;e.refine_payload/=n;e.exact_payload/=n;e.refinement_merkle/=n;e.exact_merkle/=n;
        e.local/=n;e.routing/=n;e.visited/=n;e.after4/=n;e.exact/=n;e.ms/=n;
        out.base_payload+=e.base_payload;out.refine_payload+=e.refine_payload;out.exact_payload+=e.exact_payload;
        out.refinement_merkle+=e.refinement_merkle;out.exact_merkle+=e.exact_merkle;out.local+=e.local;
        out.routing+=e.routing;out.visited+=e.visited;out.after4+=e.after4;out.exact+=e.exact;out.ms+=e.ms;
        out.object_violations+=e.object_violations;
    }
    double r=repeats;
    out.base_payload/=r;out.refine_payload/=r;out.exact_payload/=r;out.refinement_merkle/=r;out.exact_merkle/=r;
    out.local/=r;out.routing/=r;out.visited/=r;out.after4/=r;out.exact/=r;out.ms/=r;
    out.total=out.base_payload+out.refine_payload+out.exact_payload+out.local+out.routing;
    return out;
}

static std::vector<HVObjectStat> hv_object_stats(const Matrix&xb,const Matrix&queries,
        const std::vector<VLState>&states,int begin,int end,const std::vector<std::vector<int>>&lists,const NestedRule&q){
    std::vector<HVObjectStat>stats(xb.rows());std::array<std::vector<int>,7>bits;
    for(int b=2;b<=6;++b)bits[b]=uniform_bits(xb.d,b);
    for(int qi=begin;qi<end;++qi){std::array<NestedRule::LUT,7>lut;for(int b=2;b<=6;++b)lut[b]=q.prepare(queries[qi],bits[b]);
        for(int c:states[qi].lists)for(int id:lists[c]){auto&s=stats[id];++s.visits;for(int b=2;b<=6;++b)s.active[b]+=q.overlaps(id,states[qi].tau2,lut[b]);}}
    return stats;
}

static std::vector<uint8_t> hv_object_plan(const std::vector<HVObjectStat>&stats,double alpha){
    std::array<double,7>global_active{},global_visits{};for(const auto&s:stats)for(int b=2;b<=6;++b){global_active[b]+=s.active[b];global_visits[b]+=s.visits;}
    std::array<double,7>prior{};for(int b=2;b<=6;++b)prior[b]=global_visits[b]?global_active[b]/global_visits[b]:1.0;
    std::vector<uint8_t>plan(stats.size(),4);
    for(size_t id=0;id<stats.size();++id){const auto&s=stats[id];if(!s.visits)continue;double best=std::numeric_limits<double>::infinity();int choice=4;
        for(int b=2;b<=6;++b){double probability=(s.active[b]+alpha*prior[b])/(s.visits+alpha);
            double score=5.0+16.0*b+(b<6?probability*(16.0*(6-b)+32.0):0.0);
            if(score<best){best=score;choice=b;}}
        plan[id]=(uint8_t)choice;
    }
    return plan;
}

static HVEval hv_adaptive_evaluate(const Matrix&xb,const Matrix&queries,const std::vector<VLState>&states,int begin,int end,
        const std::vector<std::vector<int>>&lists,const NestedRule&q,const std::vector<uint8_t>&plan,
        const HVLayout&layout,int repeats,bool safety_check){
    HVEval out;std::array<std::vector<int>,7>bits;for(int b=2;b<=6;++b)bits[b]=uniform_bits(xb.d,b);
    for(int rep=0;rep<repeats;++rep){HVEval e;auto start=std::chrono::steady_clock::now();std::vector<int>mapped,refine,exact;
        for(int qi=begin;qi<end;++qi){std::array<NestedRule::LUT,7>lut;for(int b=2;b<=6;++b)lut[b]=q.prepare(queries[qi],bits[b]);
            mapped.clear();for(int c:states[qi].lists)mapped.push_back(layout.routing_position[c]);e.routing+=proof_count(lists.size(),mapped)*32.0+states[qi].lists.size()*8.0;
            for(int c:states[qi].lists){const auto&list=lists[c];e.visited+=list.size();refine.clear();exact.clear();
                for(size_t p=0;p<list.size();++p){int id=list[p],b=plan[id];e.base_payload+=5.0+q.d*b/8.0;
                    bool pass=q.overlaps(id,states[qi].tau2,lut[b]);if(b<6&&pass){refine.push_back((int)p);e.refine_payload+=q.d*(6-b)/8.0;pass=q.overlaps(id,states[qi].tau2,lut[6]);}
                    if(pass)exact.push_back((int)p);
                    if(safety_check){double distance=l2(queries[qi],xb[id],xb.d);e.object_violations+=q.lower2(id,lut[b])>distance||q.lower2(id,lut[6])>distance;}}
                e.after4+=refine.size();e.exact+=exact.size();e.exact_payload+=exact.size()*512.0;
                e.local+=proof_count(list.size(),refine)*32.0+proof_count(list.size(),exact)*32.0;
            }
        }
        e.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();double n=end-begin;
        e.base_payload/=n;e.refine_payload/=n;e.exact_payload/=n;e.local/=n;e.routing/=n;e.visited/=n;e.after4/=n;e.exact/=n;e.ms/=n;
        out.base_payload+=e.base_payload;out.refine_payload+=e.refine_payload;out.exact_payload+=e.exact_payload;out.local+=e.local;out.routing+=e.routing;out.visited+=e.visited;out.after4+=e.after4;out.exact+=e.exact;out.ms+=e.ms;out.object_violations+=e.object_violations;
    }
    double r=repeats;out.base_payload/=r;out.refine_payload/=r;out.exact_payload/=r;out.local/=r;out.routing/=r;out.visited/=r;out.after4/=r;out.exact/=r;out.ms/=r;
    out.total=out.base_payload+out.refine_payload+out.exact_payload+out.local+out.routing;return out;
}

#ifndef WORKLOAD_HYBRID_VO_NO_MAIN
int main(int argc,char**argv)try{
    const auto cfg=hv_args(argc,argv);std::cerr<<"Loading base, official training queries, and held-out queries...\n";
    Matrix xb=fvecs(cfg.data+"/sift_base.fvecs",cfg.base),trainq=fvecs(cfg.data+"/sift_learn.fvecs",cfg.train_queries),testq=fvecs(cfg.data+"/sift_query.fvecs",cfg.test_queries);
    std::cerr<<"Training hierarchical IVF nlist="<<cfg.nlist<<"...\n";auto lists=hkmeans(xb,cfg.nlist,cfg.iters);auto centroids=centers(xb,lists);
    std::vector<std::vector<int>>coarse_groups;std::vector<std::vector<float>>coarse_centroids;
    if(cfg.coarse_nlist){
        std::cerr<<"Grouping fine centroids for deterministic two-stage routing, coarse="<<cfg.coarse_nlist<<"...\n";
        Matrix center_matrix;center_matrix.d=xb.d;center_matrix.x.reserve((size_t)cfg.nlist*xb.d);
        for(const auto&c:centroids)center_matrix.x.insert(center_matrix.x.end(),c.begin(),c.end());
        coarse_groups=hkmeans(center_matrix,cfg.coarse_nlist,cfg.iters);coarse_centroids=centers(center_matrix,coarse_groups);
    }
    auto gt=exact_gt(xb,testq,cfg.k);double recall=0;
    auto query_start=std::chrono::steady_clock::now();
    auto train=cfg.coarse_nlist?
        hv_hierarchical_states(xb,trainq,lists,centroids,coarse_groups,coarse_centroids,cfg.coarse_nprobe,cfg.nprobe,cfg.k,nullptr,nullptr):
        hv_states(xb,trainq,lists,centroids,cfg.nprobe,cfg.k,nullptr,nullptr);
    double train_query_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-query_start).count()/cfg.train_queries;
    query_start=std::chrono::steady_clock::now();
    auto test=cfg.coarse_nlist?
        hv_hierarchical_states(xb,testq,lists,centroids,coarse_groups,coarse_centroids,cfg.coarse_nprobe,cfg.nprobe,cfg.k,&gt,&recall):
        hv_states(xb,testq,lists,centroids,cfg.nprobe,cfg.k,&gt,&recall);
    double test_query_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-query_start).count()/cfg.test_queries;
    std::cerr<<"Training verification-tree nested boundaries from official learn queries only...\n";
    auto boundaries=verification_tree_bounds(xb,trainq,lists,train,0,cfg.train_queries);NestedRule quantizer(xb,std::move(boundaries));
    if(!quantizer.contains(xb))throw std::runtime_error("trained quantizer containment failure");
    std::cerr<<"Building certified list summaries and proof-aware routing layouts...\n";auto summaries=hv_summaries(xb,lists,centroids,quantizer);
    int split=cfg.train_queries/2;
    auto layouts=hv_layouts(xb,centroids,train,cfg.route_candidates,cfg.seed);
    // Trace signatures are high-capacity at very fine IVF granularities. Fit
    // them on one half and select between trace and the static layouts on the
    // other half, so geometry is not rejected by an in-sample comparison.
    std::vector<VLState>layout_fit(train.begin(),train.begin()+split);
    std::vector<VLState>layout_validation(train.begin()+split,train.end());
    auto cv_layouts=hv_layouts(xb,centroids,layout_fit,cfg.route_candidates,cfg.seed);
    auto cv_best=std::min_element(cv_layouts.begin(),cv_layouts.begin()+3,[&](const HVLayout&a,const HVLayout&b){
        return hv_route_cost(layout_validation,a.routing_position,true)<hv_route_cost(layout_validation,b.routing_position,true);
    });
    layouts[3]=*cv_best;
    layouts[3].name="heldout-public-dummy("+cv_best->name+")";
    layouts[3].train_hashes=hv_route_cost(train,layouts[3].routing_position,true);
    auto selection=hv_plan_stats(xb,trainq,train,0,split,lists,quantizer,summaries);
    std::vector<double>margins{-32,0,16,32,64,96,128,192,256};double best_margin=0,best=std::numeric_limits<double>::infinity();
    for(double margin:margins){auto plan=hv_make_plan(selection,margin);auto e=hv_evaluate(xb,trainq,train,split,cfg.train_queries,lists,quantizer,summaries,plan,layouts[0],1,false);
        if(e.total<best){best=e.total;best_margin=margin;}}
    auto all_stats=hv_plan_stats(xb,trainq,train,0,cfg.train_queries,lists,quantizer,summaries);auto learned_plan=hv_make_plan(all_stats,best_margin);
    std::cerr<<"Learning per-object base precision with train-only shrinkage selection...\n";
    auto object_selection=hv_object_stats(xb,trainq,train,0,split,lists,quantizer);std::vector<double>alphas{0,1,2,5,10,20,50,100};
    double best_alpha=0,best_adaptive=std::numeric_limits<double>::infinity();
    for(double alpha:alphas){auto plan=hv_object_plan(object_selection,alpha);auto e=hv_adaptive_evaluate(xb,trainq,train,split,cfg.train_queries,lists,quantizer,plan,layouts[2],1,false);
        if(e.total<best_adaptive){best_adaptive=e.total;best_alpha=alpha;}}
    auto object_all=hv_object_stats(xb,trainq,train,0,cfg.train_queries,lists,quantizer);auto object_plan=hv_object_plan(object_all,best_alpha);
    std::array<size_t,7>object_bits{};for(uint8_t b:object_plan)++object_bits[b];
    std::cerr<<"Learning a global workload-aware 3/4-bit dimension budget...\n";auto dimension_selection=hv_downgrade_order(xb,trainq,train,0,split,lists,quantizer);
    std::vector<int>downgrade_counts{0,8,16,24,32,48,64,80,96,112,128};int best_downgraded=0;double best_dimension=std::numeric_limits<double>::infinity();
    for(int count:downgrade_counts){auto bits=hv_downgrade_bits(xb.d,dimension_selection,count);auto e=hv_bits_evaluate(xb,trainq,train,split,cfg.train_queries,lists,quantizer,bits,layouts[2],1,false);
        if(e.total<best_dimension){best_dimension=e.total;best_downgraded=count;}}
    auto dimension_all=hv_downgrade_order(xb,trainq,train,0,cfg.train_queries,lists,quantizer);auto dimension_bits=hv_downgrade_bits(xb.d,dimension_all,best_downgraded);
    std::cerr<<"Building a lossless integer tail and jointly selecting the final bit budget...\n";
    auto lossless_tail=hv_lossless_tail(xb,quantizer);
    HVProtocol optimized_protocol;optimized_protocol.lossless_tail=true;optimized_protocol.positional_ids=true;optimized_protocol.public_dummy=true;
    int best_protocol_downgraded=0;double best_protocol_vo=std::numeric_limits<double>::infinity();
    for(int count:downgrade_counts){
        auto bits=hv_downgrade_bits(xb.d,dimension_selection,count);
        auto e=hv_protocol_evaluate(xb,trainq,train,split,cfg.train_queries,lists,quantizer,bits,layouts[3],lossless_tail,optimized_protocol,1,false);
        if(e.total<best_protocol_vo){best_protocol_vo=e.total;best_protocol_downgraded=count;}
    }
    auto protocol_bits=hv_downgrade_bits(xb.d,dimension_all,best_protocol_downgraded);
    auto independent_train=hv_protocol_evaluate(xb,trainq,train,0,cfg.train_queries,lists,quantizer,protocol_bits,layouts[3],lossless_tail,optimized_protocol,1,false);
    HVProtocol fused_protocol=optimized_protocol;fused_protocol.fused_commitment=true;
    auto fused_train=hv_protocol_evaluate(xb,trainq,train,0,cfg.train_queries,lists,quantizer,protocol_bits,layouts[3],lossless_tail,fused_protocol,1,false);
    const bool use_fused=fused_train.total<independent_train.total;
    std::vector<uint8_t>none(lists.size()),all_ball(lists.size(),1),all_box4(lists.size(),4),all_box6(lists.size(),6);
    size_t enabled=0;std::array<size_t,7>type_count{};for(uint8_t type:learned_plan)if(type){++enabled;++type_count[type];}
    struct Row{std::string name;const std::vector<uint8_t>*plan;const HVLayout*layout;};std::vector<Row>rows;
    rows.push_back({"vt-progressive-identity",&none,&layouts[0]});rows.push_back({"vt-progressive-geometry",&none,&layouts[1]});rows.push_back({"vt-progressive-trace",&none,&layouts[2]});
    rows.push_back({"all-list-ball-trace",&all_ball,&layouts[2]});rows.push_back({"all-list-box4-trace",&all_box4,&layouts[2]});rows.push_back({"all-list-box6-trace",&all_box6,&layouts[2]});
    rows.push_back({"trained-list-summary-identity",&learned_plan,&layouts[0]});rows.push_back({"trained-list-summary-geometry",&learned_plan,&layouts[1]});rows.push_back({"trained-list-summary-trace",&learned_plan,&layouts[2]});
    std::vector<HVEval>result;for(size_t i=0;i<rows.size();++i)result.push_back(hv_evaluate(xb,testq,test,0,cfg.test_queries,lists,quantizer,summaries,*rows[i].plan,*rows[i].layout,cfg.repeats,i+1==rows.size()));
    double baseline=result[0].total;std::ofstream out(cfg.output);if(!out)throw std::runtime_error("cannot open output");
    out<<"scheme,base,nlist,nprobe,train_queries,test_queries,recall,plan_margin,enabled_lists,enabled_ball,enabled_box4,enabled_box5,enabled_box6,enabled_ratio,avg_visited,avg_enabled_visits,avg_pruned_lists,avg_pruned_vectors,avg_after4,avg_exact,summary_payload,base_payload,refine_payload,exact_payload,local_merkle,routing,total_vo,reduction_vs_vt_identity,eval_ms,block_violations,object_violations,route_train_hashes\n"<<std::setprecision(12);
    std::cout<<"scheme VO_KiB reduction pruned_vectors exact routing_KiB local_KiB\n";
    for(size_t i=0;i<rows.size();++i){auto&e=result[i];double reduction=100.0*(1.0-e.total/baseline);out<<rows[i].name<<','<<cfg.base<<','<<cfg.nlist<<','<<cfg.nprobe<<','<<cfg.train_queries<<','<<cfg.test_queries<<','<<recall<<','<<best_margin<<','<<enabled<<','<<type_count[1]<<','<<type_count[4]<<','<<type_count[5]<<','<<type_count[6]<<','<<enabled/(double)lists.size()<<','<<e.visited<<','<<e.enabled_visits<<','<<e.pruned_lists<<','<<e.pruned_vectors<<','<<e.after4<<','<<e.exact<<','<<e.summary_payload<<','<<e.base_payload<<','<<e.refine_payload<<','<<e.exact_payload<<','<<e.local<<','<<e.routing<<','<<e.total<<','<<reduction<<','<<e.ms<<','<<e.block_violations<<','<<e.object_violations<<','<<rows[i].layout->train_hashes<<'\n';
        std::cout<<rows[i].name<<' '<<std::fixed<<std::setprecision(2)<<e.total/1024<<' '<<reduction<<"% "<<e.pruned_vectors<<' '<<e.exact<<' '<<e.routing/1024<<' '<<e.local/1024<<'\n';}

    auto adaptive_train=hv_adaptive_evaluate(xb,trainq,train,0,cfg.train_queries,lists,quantizer,object_plan,layouts[2],1,false);
    auto adaptive_test=hv_adaptive_evaluate(xb,testq,test,0,cfg.test_queries,lists,quantizer,object_plan,layouts[2],cfg.repeats,true);
    std::ofstream aout(cfg.output+".adaptive.csv");aout<<"scheme,alpha,bits2,bits3,bits4,bits5,bits6,train_vo,test_vo,reduction_vs_vt_identity,reduction_vs_vt_trace,avg_refined,avg_exact,base_payload,refine_payload,exact_payload,local_merkle,routing,eval_ms,violations,recall\n"<<std::setprecision(12);
    aout<<"object-adaptive->SQ6,"<<best_alpha<<','<<object_bits[2]<<','<<object_bits[3]<<','<<object_bits[4]<<','<<object_bits[5]<<','<<object_bits[6]<<','<<adaptive_train.total<<','<<adaptive_test.total<<','<<100.0*(1-adaptive_test.total/baseline)<<','<<100.0*(1-adaptive_test.total/result[2].total)<<','<<adaptive_test.after4<<','<<adaptive_test.exact<<','<<adaptive_test.base_payload<<','<<adaptive_test.refine_payload<<','<<adaptive_test.exact_payload<<','<<adaptive_test.local<<','<<adaptive_test.routing<<','<<adaptive_test.ms<<','<<adaptive_test.object_violations<<','<<recall<<'\n';
    std::cout<<"object-adaptive->SQ6 "<<adaptive_test.total/1024<<" KiB reduction="<<100.0*(1-adaptive_test.total/baseline)<<"% vs identity, "<<100.0*(1-adaptive_test.total/result[2].total)<<"% vs trace VT; refined="<<adaptive_test.after4<<", exact="<<adaptive_test.exact<<", alpha="<<best_alpha<<'\n';
    auto dimension_train=hv_bits_evaluate(xb,trainq,train,0,cfg.train_queries,lists,quantizer,dimension_bits,layouts[2],1,false);
    auto dimension_test=hv_bits_evaluate(xb,testq,test,0,cfg.test_queries,lists,quantizer,dimension_bits,layouts[2],cfg.repeats,true);
    std::ofstream dout(cfg.output+".dimadaptive.csv");dout<<"scheme,downgraded_dimensions,base_bits,base_bytes,train_vo,test_vo,reduction_vs_vt_identity,reduction_vs_vt_trace,avg_refined,avg_exact,base_payload,refine_payload,exact_payload,local_merkle,routing,eval_ms,violations,recall\n"<<std::setprecision(12);
    dout<<"dimension-adaptive->SQ6,"<<best_downgraded<<','<<bit_sum(dimension_bits)<<','<<(bit_sum(dimension_bits)+7)/8<<','<<dimension_train.total<<','<<dimension_test.total<<','<<100.0*(1-dimension_test.total/baseline)<<','<<100.0*(1-dimension_test.total/result[2].total)<<','<<dimension_test.after4<<','<<dimension_test.exact<<','<<dimension_test.base_payload<<','<<dimension_test.refine_payload<<','<<dimension_test.exact_payload<<','<<dimension_test.local<<','<<dimension_test.routing<<','<<dimension_test.ms<<','<<dimension_test.object_violations<<','<<recall<<'\n';
    std::cout<<"dimension-adaptive->SQ6 "<<dimension_test.total/1024<<" KiB reduction="<<100.0*(1-dimension_test.total/baseline)<<"% vs identity, "<<100.0*(1-dimension_test.total/result[2].total)<<"% vs trace VT; downgraded="<<best_downgraded<<", refined="<<dimension_test.after4<<", exact="<<dimension_test.exact<<'\n';

    HVProtocol legacy_protocol,tail_protocol;tail_protocol.lossless_tail=true;
    HVProtocol position_protocol=tail_protocol;position_protocol.positional_ids=true;
    auto protocol_legacy=hv_protocol_evaluate(xb,testq,test,0,cfg.test_queries,lists,quantizer,dimension_bits,layouts[2],lossless_tail,legacy_protocol,cfg.repeats,true);
    auto protocol_tail=hv_protocol_evaluate(xb,testq,test,0,cfg.test_queries,lists,quantizer,dimension_bits,layouts[2],lossless_tail,tail_protocol,cfg.repeats,true);
    auto protocol_position=hv_protocol_evaluate(xb,testq,test,0,cfg.test_queries,lists,quantizer,dimension_bits,layouts[2],lossless_tail,position_protocol,cfg.repeats,true);
    auto protocol_dummy=hv_protocol_evaluate(xb,testq,test,0,cfg.test_queries,lists,quantizer,dimension_bits,layouts[3],lossless_tail,optimized_protocol,cfg.repeats,true);
    auto protocol_independent=hv_protocol_evaluate(xb,testq,test,0,cfg.test_queries,lists,quantizer,protocol_bits,layouts[3],lossless_tail,optimized_protocol,cfg.repeats,true);
    auto protocol_fused=hv_protocol_evaluate(xb,testq,test,0,cfg.test_queries,lists,quantizer,protocol_bits,layouts[3],lossless_tail,fused_protocol,cfg.repeats,true);
    const auto&protocol_final=use_fused?protocol_fused:protocol_independent;
    struct ProtocolRow{std::string name;const HVEval*value;int downgraded;bool tail,position,dummy,fused,selected;};
    std::vector<ProtocolRow>protocol_rows{
        {"dimension-adaptive-legacy",&protocol_legacy,best_downgraded,false,false,false,false,false},
        {"+lossless-tail",&protocol_tail,best_downgraded,true,false,false,false,false},
        {"+position-addressed-ids",&protocol_position,best_downgraded,true,true,false,false,false},
        {"+public-dummy-proofs",&protocol_dummy,best_downgraded,true,true,true,false,false},
        {"joint-bit-budget-independent",&protocol_independent,best_protocol_downgraded,true,true,true,false,!use_fused},
        {"joint-bit-budget-fused",&protocol_fused,best_protocol_downgraded,true,true,true,true,use_fused}
    };
    std::ofstream pout(cfg.output+".protocol.csv");
    pout<<"scheme,selected,downgraded_dimensions,base_bits,lossless_tail,position_ids,public_dummy,fused_commitment,tail_avg_bytes,tail_max_cell_values,avg_visited,avg_refined,avg_exact,base_payload,refine_payload,exact_payload,refinement_merkle,exact_merkle,local_merkle,routing,total_vo,reduction_vs_dimension_legacy,reduction_vs_vt_trace,eval_ms,ann_query_ms,violations,recall,routing_layout,coarse_nlist,coarse_nprobe\n"<<std::setprecision(12);
    for(const auto&row:protocol_rows){const auto&e=*row.value;
        pout<<row.name<<','<<row.selected<<','<<row.downgraded<<','<<128*4-row.downgraded<<','<<row.tail<<','<<row.position<<','<<row.dummy<<','<<row.fused<<','<<lossless_tail.average_bytes<<','<<lossless_tail.max_cell_values<<','<<e.visited<<','<<e.after4<<','<<e.exact<<','<<e.base_payload<<','<<e.refine_payload<<','<<e.exact_payload<<','<<e.refinement_merkle<<','<<e.exact_merkle<<','<<e.local<<','<<e.routing<<','<<e.total<<','<<100.0*(1-e.total/protocol_legacy.total)<<','<<100.0*(1-e.total/result[2].total)<<','<<e.ms<<','<<test_query_ms<<','<<e.object_violations<<','<<recall<<','<<layouts[3].name<<','<<cfg.coarse_nlist<<','<<cfg.coarse_nprobe<<'\n';
        std::cout<<row.name<<' '<<e.total/1024<<" KiB reduction="<<100.0*(1-e.total/result[2].total)<<"% vs trace VT"<<(row.selected?" [selected]":"")<<'\n';
    }
    auto&final=result.back();if(final.block_violations||final.object_violations||adaptive_test.object_violations||dimension_test.object_violations||protocol_final.object_violations)throw std::runtime_error("unsafe certificate detected");
    std::cerr<<"Held-out recall="<<recall<<", ANN query="<<test_query_ms<<" ms (train trace="<<train_query_ms<<" ms), coarse="<<cfg.coarse_nlist<<'x'<<cfg.coarse_nprobe<<", routing layout="<<layouts[3].name<<", trained margin="<<best_margin<<", enabled lists="<<enabled<<'/'<<lists.size()
             <<" (ball="<<type_count[1]<<",box4="<<type_count[4]<<",box5="<<type_count[5]<<",box6="<<type_count[6]<<"), violations=0. Wrote "<<cfg.output<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
#endif

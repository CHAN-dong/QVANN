

#define WORKLOAD_HYBRID_VO_NO_MAIN
#include "workload_hybrid_vo.cpp"
#undef WORKLOAD_HYBRID_VO_NO_MAIN

#include <array>
#include <random>
#include <sstream>

// Stateless-verifier experiment. Every query VO contains all routing material
// needed to replay cluster selection. No centroid or leaf directory is assumed
// cached at the verifier.

struct CRCfg {
    std::string data="../dataset/sift",output="complete_routing_vo.csv",codec="pq8",coarse_codec,residual_codec;
    int base=100000,train_queries=200,test_queries=100,k=100;
    int nlist=16384,nprobe=640,coarse_nlist=0,coarse_nprobe=0,beam=0,max_block=16,iters=6,repeats=1;
    int pq_train=20000,pq_iters=5;uint64_t seed=20260826,coarse_seed=0,residual_seed=0;bool balanced_groups=false;
};

static CRCfg cr_args(int argc,char**argv){
    CRCfg c;
    for(int i=1;i<argc;++i){
        std::string a=argv[i];auto v=[&](){if(++i==argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
        if(a=="--dataset")c.data=v();else if(a=="--output")c.output=v();else if(a=="--codec")c.codec=v();
        else if(a=="--coarse-codec")c.coarse_codec=v();else if(a=="--residual-codec")c.residual_codec=v();
        else if(a=="--base")c.base=std::stoi(v());else if(a=="--train-queries")c.train_queries=std::stoi(v());
        else if(a=="--test-queries")c.test_queries=std::stoi(v());else if(a=="--k")c.k=std::stoi(v());
        else if(a=="--nlist")c.nlist=std::stoi(v());else if(a=="--nprobe")c.nprobe=std::stoi(v());
        else if(a=="--coarse-nlist")c.coarse_nlist=std::stoi(v());else if(a=="--coarse-nprobe")c.coarse_nprobe=std::stoi(v());
        else if(a=="--beam")c.beam=std::stoi(v());
        else if(a=="--max-block")c.max_block=std::stoi(v());
        else if(a=="--balanced-groups")c.balanced_groups=true;
        else if(a=="--iters")c.iters=std::stoi(v());else if(a=="--repeats")c.repeats=std::stoi(v());
        else if(a=="--pq-train")c.pq_train=std::stoi(v());else if(a=="--pq-iters")c.pq_iters=std::stoi(v());
        else if(a=="--seed")c.seed=std::stoull(v());else if(a=="--coarse-seed")c.coarse_seed=std::stoull(v());
        else if(a=="--residual-seed")c.residual_seed=std::stoull(v());else throw std::runtime_error("unknown option "+a);
    }
    auto valid_codec=[](const std::string&s){return s.empty()||s=="exact"||s=="sq4"||s=="pq8"||s=="pq16"||s=="pq24"||s=="pq32"||s=="pq16x8"||s=="pq16n8"||s=="pq24n8"||s=="pq32n8"||s=="pq24f16"||s=="pq24cb8"||s=="pq24cb4"||s=="pq24cb2";};
    if(!valid_codec(c.codec)||!valid_codec(c.coarse_codec)||!valid_codec(c.residual_codec))
        throw std::runtime_error("codec must be exact, sq4, pq8, pq16, pq24, pq32, or pq16x8");
    if(c.base<c.nlist||c.nprobe<1||c.nprobe>c.nlist||c.train_queries<2||c.test_queries<1||c.k<1||c.repeats<1||
       ((c.coarse_nlist==0)!=(c.coarse_nprobe==0))||c.coarse_nlist<0||c.coarse_nprobe<0||
       c.coarse_nlist>c.nlist||c.coarse_nprobe>c.coarse_nlist||c.beam<0||c.max_block<1)throw std::runtime_error("invalid configuration");
    return c;
}

static Matrix cr_matrix(const std::vector<std::vector<float>>&v){
    Matrix out;if(v.empty())return out;out.d=(int)v[0].size();out.x.reserve(v.size()*(size_t)out.d);
    for(const auto&r:v)out.x.insert(out.x.end(),r.begin(),r.end());return out;
}

static double cr_leaf_parameter_bytes(const NestedRule&q,const std::vector<int>&bits){
    // Stateless verifier receives the learned verification-tree boundaries
    // and the per-dimension base-bit plan on every query.  SIFT coordinates
    // are integral, so every learned cell edge is an integer or half-integer.
    // Transmit 2*edge losslessly as uint16_t instead of an IEEE-754 double.
    for(double edge:q.boundary){
        const long encoded=std::lround(2.0*edge);
        if(encoded<0||encoded>510||std::abs(edge-0.5*encoded)>1e-12)
            throw std::runtime_error("verification boundary is not uint16 half-integer encodable");
    }
    return q.boundary.size()*sizeof(uint16_t)+bits.size();
}

class CRCodec {
    std::string mode_;int d_=0,m_=16,ds_=0,k_=16,cb_storage_bits_=32;
    std::vector<float>lo_,step_,cb_;
    std::vector<uint8_t>fine_code_,coarse_code_;
    bool norm_corrected_=false;float norm_lo_=0,norm_step_=0;
    std::vector<uint8_t>fine_norm_,coarse_norm_;
    int entropy_slots_=0,entropy_alphabet_=0;std::vector<double>entropy_bits_;std::vector<uint8_t>huffman_bits_;

    void train_pq(const Matrix&x,int sample,int iters,uint64_t seed){
        ds_=d_/m_;cb_.resize((size_t)m_*k_*ds_);sample=std::min<int>(sample,x.rows());
        std::vector<int>sample_id(sample);for(int i=0;i<sample;++i)sample_id[i]=(uint64_t)i*x.rows()/sample;
        std::mt19937_64 rng(seed);std::vector<double>sum((size_t)k_*ds_);std::vector<int>count(k_);
        for(int s=0;s<m_;++s){
            for(int z=0;z<k_;++z){int id=sample_id[((uint64_t)z*sample/k_+rng()%std::max(1,sample/k_))%sample];
                std::copy(x[id]+s*ds_,x[id]+(s+1)*ds_,cb_.begin()+(size_t)(s*k_+z)*ds_);}
            for(int it=0;it<iters;++it){
                std::fill(sum.begin(),sum.end(),0);std::fill(count.begin(),count.end(),0);
                for(int id:sample_id){const float*p=x[id]+s*ds_;int best=0;float bd=std::numeric_limits<float>::infinity();
                    for(int z=0;z<k_;++z){float dis=l2(p,cb_.data()+(size_t)(s*k_+z)*ds_,ds_);if(dis<bd){bd=dis;best=z;}}
                    ++count[best];for(int j=0;j<ds_;++j)sum[(size_t)best*ds_+j]+=p[j];}
                for(int z=0;z<k_;++z)if(count[z])for(int j=0;j<ds_;++j)cb_[(size_t)(s*k_+z)*ds_+j]=sum[(size_t)z*ds_+j]/count[z];
            }
        }
    }

    static uint16_t float_to_half(float value){
        uint32_t x;std::memcpy(&x,&value,sizeof(x));uint32_t sign=(x>>16)&0x8000U,mantissa=x&0x7fffffU;int exponent=(int)((x>>23)&0xffU)-127+15;
        if(exponent<=0){if(exponent<-10)return sign;mantissa=(mantissa|0x800000U)>>(1-exponent);if(mantissa&0x1000U)mantissa+=0x2000U;return sign|(mantissa>>13);}
        if(exponent>=31)return sign|0x7c00U;if(mantissa&0x1000U){mantissa+=0x2000U;if(mantissa&0x800000U){mantissa=0;if(++exponent>=31)return sign|0x7c00U;}}
        return sign|((uint32_t)exponent<<10)|(mantissa>>13);
    }
    static float half_to_float(uint16_t h){
        int exponent=(h>>10)&31,mantissa=h&1023;float value;if(exponent==0)value=std::ldexp((float)mantissa,-24);else if(exponent==31)value=std::numeric_limits<float>::infinity();else value=std::ldexp((float)(1024+mantissa),exponent-25);return (h&0x8000)?-value:value;
    }

    void compress_codebook(){
        if(mode_.find("f16")!=std::string::npos){cb_storage_bits_=16;for(float&x:cb_)x=half_to_float(float_to_half(x));return;}
        int bits=mode_.find("cb8")!=std::string::npos?8:(mode_.find("cb4")!=std::string::npos?4:(mode_.find("cb2")!=std::string::npos?2:32));if(bits==32)return;cb_storage_bits_=bits;int levels=(1<<bits)-1;
        for(int s=0;s<m_;++s)for(int j=0;j<ds_;++j){float lo=std::numeric_limits<float>::infinity(),hi=-lo;for(int z=0;z<k_;++z){float x=cb_[(size_t)(s*k_+z)*ds_+j];lo=std::min(lo,x);hi=std::max(hi,x);}float step=(hi-lo)/levels;
            for(int z=0;z<k_;++z){float&x=cb_[(size_t)(s*k_+z)*ds_+j];x=step?lo+std::clamp((int)std::lround((x-lo)/step),0,levels)*step:lo;}}
    }

    void encode_matrix(const Matrix&x,std::vector<uint8_t>&out)const{
        if(mode_=="exact")return;
        if(mode_=="sq4"){
            out.resize(x.rows()*(size_t)d_);for(size_t i=0;i<x.rows();++i)for(int j=0;j<d_;++j){
                int z=step_[j]?std::clamp((int)std::lround((x[i][j]-lo_[j])/step_[j]),0,15):0;out[i*(size_t)d_+j]=(uint8_t)z;}
        }else{
            out.resize(x.rows()*(size_t)m_);for(size_t i=0;i<x.rows();++i)for(int s=0;s<m_;++s){
                int best=0;float bd=std::numeric_limits<float>::infinity();for(int z=0;z<k_;++z){
                    float dis=l2(x[i]+s*ds_,cb_.data()+(size_t)(s*k_+z)*ds_,ds_);if(dis<bd){bd=dis;best=z;}}
                out[i*(size_t)m_+s]=(uint8_t)best;}
        }
    }

    void encode_norm(const Matrix&x,const std::vector<uint8_t>&code,std::vector<uint8_t>&out){
        if(!norm_corrected_||x.rows()==0)return;std::vector<float>delta(x.rows());float lo=std::numeric_limits<float>::infinity(),hi=-lo;
        for(size_t i=0;i<x.rows();++i){float exact=0,reconstructed=0;for(int j=0;j<d_;++j)exact+=x[i][j]*x[i][j];
            for(int s=0;s<m_;++s){const float*w=cb_.data()+(size_t)(s*k_+code[i*(size_t)m_+s])*ds_;for(int j=0;j<ds_;++j)reconstructed+=w[j]*w[j];}
            delta[i]=exact-reconstructed;lo=std::min(lo,delta[i]);hi=std::max(hi,delta[i]);}
        if(fine_norm_.empty()){norm_lo_=lo;norm_step_=(hi-lo)/255.0f;}else{
            float old_hi=norm_lo_+255.0f*norm_step_;float new_lo=std::min(norm_lo_,lo),new_hi=std::max(old_hi,hi);
            if(new_lo!=norm_lo_||new_hi!=old_hi)throw std::runtime_error("shared norm range must be trained on the encoded fine matrix first");}
        out.resize(x.rows());for(size_t i=0;i<x.rows();++i)out[i]=norm_step_?(uint8_t)std::clamp((int)std::lround((delta[i]-norm_lo_)/norm_step_),0,255):0;
    }

    void train_entropy_model(const Matrix&x){
        if(mode_=="exact"||x.rows()==0)return;entropy_slots_=mode_=="sq4"?d_:m_;entropy_alphabet_=mode_=="sq4"?16:k_;
        std::vector<uint64_t>count((size_t)entropy_slots_*entropy_alphabet_,1);int stride=mode_=="sq4"?d_:m_;
        for(size_t i=0;i<x.rows();++i)for(int s=0;s<entropy_slots_;++s)++count[(size_t)s*entropy_alphabet_+fine_code_[i*(size_t)stride+s]];
        entropy_bits_.resize(count.size());for(int s=0;s<entropy_slots_;++s){double total=x.rows()+entropy_alphabet_;
            for(int z=0;z<entropy_alphabet_;++z)entropy_bits_[(size_t)s*entropy_alphabet_+z]=-std::log2(count[(size_t)s*entropy_alphabet_+z]/total);}
        huffman_bits_.assign(count.size(),0);for(int s=0;s<entropy_slots_;++s){int a=entropy_alphabet_;std::vector<int>parent(2*a-1,-1);using Item=std::pair<uint64_t,int>;
            std::priority_queue<Item,std::vector<Item>,std::greater<Item>>heap;for(int z=0;z<a;++z)heap.push({count[(size_t)s*a+z],z});int next=a;
            while(heap.size()>1){auto x=heap.top();heap.pop();auto y=heap.top();heap.pop();parent[x.second]=parent[y.second]=next;heap.push({x.first+y.first,next++});}
            for(int z=0;z<a;++z){int depth=0;for(int v=z;parent[v]>=0;v=parent[v])++depth;huffman_bits_[(size_t)s*a+z]=std::max(1,depth);}}
    }

public:
    CRCodec(const std::string&mode,const Matrix&fine,const Matrix&coarse,int sample,int iters,uint64_t seed):mode_(mode),d_(fine.d){
        norm_corrected_=mode_.size()>2&&mode_.substr(mode_.size()-2)=="n8";
        if(mode_.rfind("pq16x8",0)==0){m_=16;k_=256;}else if(mode_.rfind("pq24",0)==0){m_=32;k_=64;}
        else if(mode_.rfind("pq32",0)==0)m_=64;else if(mode_.rfind("pq16",0)==0)m_=32;
        if(mode_.rfind("pq",0)==0&&d_%m_)throw std::runtime_error("PQ subspace count must divide dimension");
        if(mode_=="sq4"){
            lo_.assign(d_,std::numeric_limits<float>::infinity());std::vector<float>hi(d_,-std::numeric_limits<float>::infinity());step_.resize(d_);
            for(size_t i=0;i<fine.rows();++i)for(int j=0;j<d_;++j){lo_[j]=std::min(lo_[j],fine[i][j]);hi[j]=std::max(hi[j],fine[i][j]);}
            for(int j=0;j<d_;++j)step_[j]=(hi[j]-lo_[j])/15.0f;
        }else if(mode_.rfind("pq",0)==0){train_pq(fine,sample,iters,seed);compress_codebook();}
        encode_matrix(fine,fine_code_);encode_norm(fine,fine_code_,fine_norm_);train_entropy_model(fine);if(coarse.rows()){encode_matrix(coarse,coarse_code_);encode_norm(coarse,coarse_code_,coarse_norm_);}
    }
    int code_bytes()const{return mode_=="exact"?d_*4:(mode_=="sq4"?(d_+1)/2:(k_==256?m_:(k_==64?(m_*6+7)/8:(m_+1)/2)))+(norm_corrected_?1:0);}
    int parameter_bytes()const{return mode_=="exact"?0:(mode_=="sq4"?d_*8:((int)(cb_.size()*cb_storage_bits_+7)/8+(cb_storage_bits_<=8?d_*8:0)+(norm_corrected_?8:0)));}
    int entropy_model_bytes()const{return entropy_slots_*entropy_alphabet_*2;}
    int huffman_model_bytes()const{int width=4;for(uint8_t length:huffman_bits_){if(length>31)throw std::runtime_error("Huffman length exceeds 5-bit model");if(length>15)width=5;}return (entropy_slots_*entropy_alphabet_*width+7)/8;}
    double entropy_bytes(const std::vector<int>&ids)const{
        if(entropy_bits_.empty())return ids.size()*(double)code_bytes();int stride=mode_=="sq4"?d_:m_;double bits=0;
        for(int id:ids)for(int s=0;s<entropy_slots_;++s)bits+=entropy_bits_[(size_t)s*entropy_alphabet_+fine_code_[(size_t)id*stride+s]];
        if(norm_corrected_)bits+=ids.size()*8.0;return bits/8.0;
    }
    double huffman_bytes(const std::vector<int>&ids)const{
        if(huffman_bits_.empty())return ids.size()*(double)code_bytes();int stride=mode_=="sq4"?d_:m_;uint64_t bits=0;
        for(int id:ids)for(int s=0;s<entropy_slots_;++s)bits+=huffman_bits_[(size_t)s*entropy_alphabet_+fine_code_[(size_t)id*stride+s]];
        if(norm_corrected_)bits+=ids.size()*8;return (bits+7)/8.0;
    }
    uint64_t huffman_bit_count(const std::vector<int>&ids)const{
        if(huffman_bits_.empty())return ids.size()*(uint64_t)code_bytes()*8;int stride=mode_=="sq4"?d_:m_;uint64_t bits=0;
        for(int id:ids)for(int s=0;s<entropy_slots_;++s)bits+=huffman_bits_[(size_t)s*entropy_alphabet_+fine_code_[(size_t)id*stride+s]];
        if(norm_corrected_)bits+=ids.size()*8;return bits;
    }
    int slots()const{return mode_=="sq4"?d_:m_;}
    int alphabet()const{return mode_=="sq4"?16:k_;}
    uint8_t symbol(size_t id,int slot,bool coarse=false)const{
        if(mode_=="exact")throw std::runtime_error("exact codec has no symbols");
        const auto&code=coarse?coarse_code_:fine_code_;int stride=mode_=="sq4"?d_:m_;
        if(slot<0||slot>=stride||id*(size_t)stride+slot>=code.size())throw std::runtime_error("codec symbol out of range");
        return code[id*(size_t)stride+slot];
    }
    std::vector<float>prepare(const float*q)const{
        if(mode_.rfind("pq",0)!=0)return {};std::vector<float>lut((size_t)m_*k_);
        for(int s=0;s<m_;++s)for(int z=0;z<k_;++z)lut[(size_t)s*k_+z]=l2(q+s*ds_,cb_.data()+(size_t)(s*k_+z)*ds_,ds_);return lut;
    }
    float distance(const float*q,const std::vector<float>&lut,const Matrix&centers,size_t id,bool coarse)const{
        if(mode_=="exact")return l2(q,centers[id],d_);
        const auto&code=coarse?coarse_code_:fine_code_;
        if(mode_=="sq4"){float sum=0;size_t off=id*(size_t)d_;for(int j=0;j<d_;++j){float v=q[j]-(lo_[j]+code[off+j]*step_[j]);sum+=v*v;}return sum;}
        float sum=0;size_t off=id*(size_t)m_;for(int s=0;s<m_;++s)sum+=lut[(size_t)s*k_+code[off+s]];
        if(norm_corrected_){const auto&nc=coarse?coarse_norm_:fine_norm_;sum+=norm_lo_+nc[id]*norm_step_;}return sum;
    }
    std::vector<float>decode(const Matrix&centers,size_t id,bool coarse=false)const{
        if(mode_=="exact")return std::vector<float>(centers[id],centers[id]+d_);const auto&code=coarse?coarse_code_:fine_code_;std::vector<float>out(d_);
        if(mode_=="sq4"){size_t off=id*(size_t)d_;for(int j=0;j<d_;++j)out[j]=lo_[j]+code[off+j]*step_[j];}
        else{size_t off=id*(size_t)m_;for(int s=0;s<m_;++s)std::copy(cb_.begin()+(size_t)(s*k_+code[off+s])*ds_,cb_.begin()+(size_t)(s*k_+code[off+s]+1)*ds_,out.begin()+s*ds_);}
        return out;
    }
};

struct CRStates {std::vector<VLState>state;std::vector<std::vector<int>>coarse_selected;double candidates=0,query_ms=0,recall=0;};

static CRStates cr_states(const Matrix&xb,const Matrix&q,const std::vector<std::vector<int>>&lists,const Matrix&fine_centers,
        const std::vector<std::vector<int>>&groups,const Matrix&coarse_centers,const CRCodec&codec,
        int coarse_nprobe,int nprobe,int k,const std::vector<std::vector<int>>*gt){
    CRStates out;out.state.resize(q.rows());out.coarse_selected.resize(q.rows());uint64_t hits=0,candidate_total=0;
    auto start=std::chrono::steady_clock::now();
    for(size_t qi=0;qi<q.rows();++qi){auto lut=codec.prepare(q[qi]);std::vector<int>candidate;
        if(groups.empty()){candidate.resize(lists.size());std::iota(candidate.begin(),candidate.end(),0);}
        else{
            std::vector<int>order(groups.size());std::iota(order.begin(),order.end(),0);
            std::partial_sort(order.begin(),order.begin()+coarse_nprobe,order.end(),[&](int a,int b){
                float x=codec.distance(q[qi],lut,coarse_centers,a,true),y=codec.distance(q[qi],lut,coarse_centers,b,true);return x==y?a<b:x<y;});
            out.coarse_selected[qi].assign(order.begin(),order.begin()+coarse_nprobe);
            for(int z=0;z<coarse_nprobe;++z)candidate.insert(candidate.end(),groups[order[z]].begin(),groups[order[z]].end());
        }
        if((int)candidate.size()<nprobe)throw std::runtime_error("routing shortlist smaller than nprobe");candidate_total+=candidate.size();
        std::partial_sort(candidate.begin(),candidate.begin()+nprobe,candidate.end(),[&](int a,int b){
            float x=codec.distance(q[qi],lut,fine_centers,a,false),y=codec.distance(q[qi],lut,fine_centers,b,false);return x==y?a<b:x<y;});
        auto&s=out.state[qi];s.lists.assign(candidate.begin(),candidate.begin()+nprobe);std::priority_queue<std::pair<float,int>>top;
        for(int c:s.lists)for(int id:lists[c]){float d=l2(q[qi],xb[id],xb.d);if((int)top.size()<k)top.push({d,id});
            else if(d<top.top().first||(d==top.top().first&&id<top.top().second)){top.pop();top.push({d,id});}}
        if((int)top.size()!=k)throw std::runtime_error("fewer than k routed objects");s.tau2=top.top().first;
        while(!top.empty()){s.answer.push_back(top.top().second);top.pop();}
        if(gt){std::unordered_set<int>a(s.answer.begin(),s.answer.end());for(int id:(*gt)[qi])hits+=a.count(id);}
    }
    out.query_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/q.rows();
    out.candidates=candidate_total/(double)q.rows();out.recall=gt?hits/(q.rows()*(double)k):0;return out;
}

struct CRRoute {double parameters=0,centroid_codes=0,group_metadata=0,proofs=0,total=0;};

static CRRoute cr_route_bytes(const CRStates&queries,const std::vector<std::vector<int>>&groups,int nlist,const CRCodec&codec,const HVLayout&layout){
    CRRoute out;out.parameters=96.0+codec.parameter_bytes(); // signed index root + codec parameters
    if(groups.empty()){
        out.centroid_codes=nlist*(double)codec.code_bytes();
        for(const auto&s:queries.state){std::vector<int>mapped;mapped.reserve(s.lists.size());for(int leaf:s.lists)mapped.push_back(layout.routing_position[leaf]);
            out.group_metadata+=s.lists.size()*4.0;out.proofs+=hv_dummy_proof_count(nlist,mapped)*32.0;}
    }else{
        std::vector<int>group_of(nlist,-1),position(nlist,-1);for(size_t g=0;g<groups.size();++g)for(size_t p=0;p<groups[g].size();++p){group_of[groups[g][p]]=(int)g;position[groups[g][p]]=(int)p;}
        out.centroid_codes=groups.size()*(double)codec.code_bytes()+queries.candidates*codec.code_bytes()*queries.state.size();
        std::vector<std::vector<int>>selected(groups.size());
        for(size_t qi=0;qi<queries.state.size();++qi){
            const auto&cg=queries.coarse_selected[qi];out.group_metadata+=cg.size()*68.0;out.proofs+=hv_dummy_proof_count(groups.size(),cg)*32.0;
            for(auto&v:selected)v.clear();for(int leaf:queries.state[qi].lists)selected[group_of[leaf]].push_back(position[leaf]);
            for(int g:cg)if(!selected[g].empty()){out.group_metadata+=selected[g].size()*4.0;out.proofs+=hv_dummy_proof_count(groups[g].size(),selected[g])*32.0;}
        }
    }
    double nq=queries.state.size();out.group_metadata/=nq;out.proofs/=nq;
    if(!groups.empty())out.centroid_codes=groups.size()*(double)codec.code_bytes()+queries.candidates*codec.code_bytes();
    out.total=out.parameters+out.centroid_codes+out.group_metadata+out.proofs;return out;
}

static double cr_leaf_list_cost(const Matrix&xb,const float*query,double tau2,const std::vector<int>&list,
        const NestedRule&q,const std::vector<int>&bits,const HVLosslessTail&tail,const HVProtocol&protocol){
    auto first=q.prepare(query,bits),last=q.prepare(query,uniform_bits(xb.d,6));std::vector<int>refine,exact;
    for(size_t p=0;p<list.size();++p){bool pass=q.overlaps(list[p],tau2,first);if(pass){refine.push_back(p);pass=q.overlaps(list[p],tau2,last);}if(pass)exact.push_back(p);}
    int base_bytes=(bit_sum(bits)+7)/8,delta_bytes=(6*xb.d-bit_sum(bits)+7)/8;double cost=list.size()*base_bytes;
    cost+=refine.size()*(delta_bytes+(protocol.fused_commitment?32.0:0.0));for(int p:exact)cost+=tail.bytes[list[p]]+(protocol.positional_ids?4.0:0.0);
    auto hashes=[&](const std::vector<int>&s){return protocol.public_dummy?hv_dummy_proof_count(list.size(),s):proof_count(list.size(),s);};
    cost+=hashes(refine)*32.0;if(!protocol.fused_commitment)cost+=hashes(exact)*32.0;return cost;
}

static std::vector<uint8_t> cr_summary_plan(const Matrix&xb,const Matrix&queries,const std::vector<VLState>&states,
        const std::vector<std::vector<int>>&lists,const NestedRule&q,const std::vector<HVSummary>&summary,
        const std::vector<int>&bits,const HVLosslessTail&tail,const HVProtocol&protocol){
    static const std::array<int,4>types{1,4,5,6};std::vector<HVPlanStat>stats(lists.size());auto q4bits=uniform_bits(xb.d,4),max_bits=uniform_bits(xb.d,6);
    int base_bytes=(bit_sum(bits)+7)/8,delta_bytes=(bit_sum(max_bits)-bit_sum(bits)+7)/8;std::vector<int>refine,exact;
    for(size_t qi=0;qi<queries.rows();++qi){auto first=q.prepare(queries[qi],bits),last=q.prepare(queries[qi],max_bits),q4=q.prepare(queries[qi],q4bits);
        for(int c:states[qi].lists){auto&s=stats[c];++s.visits;const auto&list=lists[c];refine.clear();exact.clear();
            for(size_t p=0;p<list.size();++p){bool pass=q.overlaps(list[p],states[qi].tau2,first);if(pass){refine.push_back(p);pass=q.overlaps(list[p],states[qi].tau2,last);}if(pass)exact.push_back(p);}
            double base=list.size()*base_bytes+refine.size()*(delta_bytes+(protocol.fused_commitment?32.0:0.0));for(int p:exact)base+=tail.bytes[list[p]]+(protocol.positional_ids?4.0:0.0);
            auto hashes=[&](const std::vector<int>&v){return protocol.public_dummy?hv_dummy_proof_count(list.size(),v):proof_count(list.size(),v);};base+=hashes(refine)*32.0;if(!protocol.fused_commitment)base+=hashes(exact)*32.0;
            for(size_t z=0;z<types.size();++z){int type=types[z],bytes=hv_summary_bytes(type);
                s.gain[z]+=hv_summary_prunes(type,queries[qi],states[qi].tau2,summary[c],q,q4)?base-bytes:-bytes;}}}
    return hv_make_plan(stats,0.0);
}

static HVEval cr_summary_evaluate(const Matrix&xb,const Matrix&queries,const std::vector<VLState>&states,
        const std::vector<std::vector<int>>&lists,const NestedRule&q,const std::vector<HVSummary>&summary,const std::vector<uint8_t>&plan,
        const std::vector<int>&bits,const HVLosslessTail&tail,const HVProtocol&protocol,int repeats,bool safety_check){
    HVEval out;auto max_bits=uniform_bits(xb.d,6),q4bits=uniform_bits(xb.d,4);int base_bytes=(bit_sum(bits)+7)/8,delta_bytes=(bit_sum(max_bits)-bit_sum(bits)+7)/8;
    auto hashes=[&](size_t n,const std::vector<int>&s){return protocol.public_dummy?hv_dummy_proof_count(n,s):proof_count(n,s);};
    for(int rep=0;rep<repeats;++rep){HVEval e;auto start=std::chrono::steady_clock::now();std::vector<int>refine,exact;
        for(size_t qi=0;qi<queries.rows();++qi){auto first=q.prepare(queries[qi],bits),last=q.prepare(queries[qi],max_bits),q4=q.prepare(queries[qi],q4bits);
            for(int c:states[qi].lists){const auto&list=lists[c];e.visited+=list.size();if(plan[c]){++e.enabled_visits;e.summary_payload+=hv_summary_bytes(plan[c]);
                    if(hv_summary_prunes(plan[c],queries[qi],states[qi].tau2,summary[c],q,q4)){++e.pruned_lists;e.pruned_vectors+=list.size();
                        if(safety_check)for(int id:list)if(l2(queries[qi],xb[id],xb.d)<=states[qi].tau2)++e.block_violations;continue;}}
                e.base_payload+=list.size()*base_bytes;refine.clear();exact.clear();for(size_t p=0;p<list.size();++p){int id=list[p];bool pass=q.overlaps(id,states[qi].tau2,first);
                    if(pass){refine.push_back(p);pass=q.overlaps(id,states[qi].tau2,last);}if(pass)exact.push_back(p);if(safety_check){double dis=l2(queries[qi],xb[id],xb.d);e.object_violations+=q.lower2(id,first)>dis||q.lower2(id,last)>dis;}}
                e.after4+=refine.size();e.exact+=exact.size();e.refine_payload+=refine.size()*(delta_bytes+(protocol.fused_commitment?32.0:0.0));
                for(int p:exact)e.exact_payload+=tail.bytes[list[p]]+(protocol.positional_ids?4.0:0.0);e.refinement_merkle+=hashes(list.size(),refine)*32.0;
                if(!protocol.fused_commitment)e.exact_merkle+=hashes(list.size(),exact)*32.0;}}
        e.local=e.refinement_merkle+e.exact_merkle;e.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();double n=queries.rows();
        e.summary_payload/=n;e.base_payload/=n;e.refine_payload/=n;e.exact_payload/=n;e.refinement_merkle/=n;e.exact_merkle/=n;e.local/=n;e.visited/=n;e.pruned_lists/=n;e.pruned_vectors/=n;e.after4/=n;e.exact/=n;e.enabled_visits/=n;e.ms/=n;
        out.summary_payload+=e.summary_payload;out.base_payload+=e.base_payload;out.refine_payload+=e.refine_payload;out.exact_payload+=e.exact_payload;out.refinement_merkle+=e.refinement_merkle;out.exact_merkle+=e.exact_merkle;out.local+=e.local;
        out.visited+=e.visited;out.pruned_lists+=e.pruned_lists;out.pruned_vectors+=e.pruned_vectors;out.after4+=e.after4;out.exact+=e.exact;out.enabled_visits+=e.enabled_visits;out.ms+=e.ms;out.block_violations+=e.block_violations;out.object_violations+=e.object_violations;}
    double r=repeats;out.summary_payload/=r;out.base_payload/=r;out.refine_payload/=r;out.exact_payload/=r;out.refinement_merkle/=r;out.exact_merkle/=r;out.local/=r;out.visited/=r;out.pruned_lists/=r;out.pruned_vectors/=r;out.after4/=r;out.exact/=r;out.enabled_visits/=r;out.ms/=r;
    out.total=out.summary_payload+out.base_payload+out.refine_payload+out.exact_payload+out.local;return out;
}

#ifndef COMPLETE_ROUTING_VO_NO_MAIN
int main(int argc,char**argv)try{
    auto cfg=cr_args(argc,argv);std::cerr<<"Loading data...\n";Matrix xb=fvecs(cfg.data+"/sift_base.fvecs",cfg.base),trainq=fvecs(cfg.data+"/sift_learn.fvecs",cfg.train_queries),testq=fvecs(cfg.data+"/sift_query.fvecs",cfg.test_queries);
    std::cerr<<"Training fine IVF nlist="<<cfg.nlist<<"...\n";auto lists=hkmeans(xb,cfg.nlist,cfg.iters);auto fine_vector=centers(xb,lists);Matrix fine_centers=cr_matrix(fine_vector);
    std::vector<std::vector<int>>groups;std::vector<std::vector<float>>coarse_vector;Matrix coarse_centers;
    if(cfg.coarse_nlist){std::cerr<<"Training coarse routing groups="<<cfg.coarse_nlist<<"...\n";groups=hkmeans(fine_centers,cfg.coarse_nlist,cfg.iters);coarse_vector=centers(fine_centers,groups);coarse_centers=cr_matrix(coarse_vector);}
    std::cerr<<"Training/encoding routing codec="<<cfg.codec<<"...\n";CRCodec codec(cfg.codec,fine_centers,coarse_centers,cfg.pq_train,cfg.pq_iters,cfg.seed);
    auto gt=exact_gt(xb,testq,cfg.k);auto train=cr_states(xb,trainq,lists,fine_centers,groups,coarse_centers,codec,cfg.coarse_nprobe,cfg.nprobe,cfg.k,nullptr);
    auto test=cr_states(xb,testq,lists,fine_centers,groups,coarse_centers,codec,cfg.coarse_nprobe,cfg.nprobe,cfg.k,&gt);
    std::cerr<<"Training leaf verification codes...\n";auto boundaries=verification_tree_bounds(xb,trainq,lists,train.state,0,cfg.train_queries);NestedRule quantizer(xb,std::move(boundaries));
    if(!quantizer.contains(xb))throw std::runtime_error("quantizer containment failure");auto tail=hv_lossless_tail(xb,quantizer);
    HVLayout identity;identity.name="identity";identity.routing_position.resize(lists.size());std::iota(identity.routing_position.begin(),identity.routing_position.end(),0);
    auto learned_layouts=groups.empty()?hv_layouts(xb,fine_vector,train.state,16,cfg.seed):std::vector<HVLayout>{identity};
    const HVLayout&layout=groups.empty()?learned_layouts[3]:identity;
    int split=cfg.train_queries/2;auto order=hv_downgrade_order(xb,trainq,train.state,0,split,lists,quantizer);std::vector<int>counts{0,8,16,24,32,48,64,80,96,112,128};
    HVProtocol independent;independent.lossless_tail=true;independent.positional_ids=true;independent.public_dummy=true;
    int best_count=0;double best=std::numeric_limits<double>::infinity();for(int count:counts){auto bits=hv_downgrade_bits(xb.d,order,count);
        auto e=hv_protocol_evaluate(xb,trainq,train.state,split,cfg.train_queries,lists,quantizer,bits,layout,tail,independent,1,false);double leaf=e.total-e.routing;if(leaf<best){best=leaf;best_count=count;}}
    auto all_order=hv_downgrade_order(xb,trainq,train.state,0,cfg.train_queries,lists,quantizer);auto bits=hv_downgrade_bits(xb.d,all_order,best_count);
    auto independent_train=hv_protocol_evaluate(xb,trainq,train.state,0,cfg.train_queries,lists,quantizer,bits,layout,tail,independent,1,false);
    HVProtocol fused=independent;fused.fused_commitment=true;auto fused_train=hv_protocol_evaluate(xb,trainq,train.state,0,cfg.train_queries,lists,quantizer,bits,layout,tail,fused,1,false);
    const bool use_fused=(fused_train.total-fused_train.routing)<(independent_train.total-independent_train.routing);const HVProtocol&protocol=use_fused?fused:independent;
    auto summaries=hv_summaries(xb,lists,fine_vector,quantizer);auto summary_plan=cr_summary_plan(xb,trainq,train.state,lists,quantizer,summaries,bits,tail,protocol);
    auto leaf=cr_summary_evaluate(xb,testq,test.state,lists,quantizer,summaries,summary_plan,bits,tail,protocol,cfg.repeats,true);double leaf_total=leaf.total;
    auto route=cr_route_bytes(test,groups,cfg.nlist,codec,layout);double leaf_parameters=cr_leaf_parameter_bytes(quantizer,bits);route.parameters+=leaf_parameters;route.total+=leaf_parameters;double total=leaf_total+route.total;
    std::ofstream out(cfg.output);if(!out)throw std::runtime_error("cannot open output");out<<std::setprecision(12);
    out<<"codec,base,nlist,nprobe,coarse_nlist,coarse_nprobe,train_queries,test_queries,recall,avg_candidate_centroids,avg_visited,avg_pruned_lists,avg_pruned_vectors,avg_refined,avg_exact,base_bits,summary_payload,base_payload,refine_payload,exact_payload,local_merkle,routing_parameters,routing_centroid_codes,routing_group_metadata,routing_proofs,routing_total,leaf_total,total_vo,ann_query_ms,vo_generation_ms,violations\n";
    out<<cfg.codec<<','<<cfg.base<<','<<cfg.nlist<<','<<cfg.nprobe<<','<<cfg.coarse_nlist<<','<<cfg.coarse_nprobe<<','<<cfg.train_queries<<','<<cfg.test_queries<<','<<test.recall<<','<<test.candidates<<','<<leaf.visited<<','<<leaf.pruned_lists<<','<<leaf.pruned_vectors<<','<<leaf.after4<<','<<leaf.exact<<','<<bit_sum(bits)<<','<<leaf.summary_payload<<','<<leaf.base_payload<<','<<leaf.refine_payload<<','<<leaf.exact_payload<<','<<leaf.local<<','<<route.parameters<<','<<route.centroid_codes<<','<<route.group_metadata<<','<<route.proofs<<','<<route.total<<','<<leaf_total<<','<<total<<','<<test.query_ms<<','<<leaf.ms<<','<<(leaf.object_violations+leaf.block_violations)<<'\n';
    std::cout<<"codec="<<cfg.codec<<" recall="<<test.recall<<" candidates="<<test.candidates<<" visited="<<leaf.visited<<" pruned="<<leaf.pruned_vectors<<" route="<<route.total/1024<<" KiB leaf="<<leaf_total/1024<<" KiB total="<<total/1024<<" KiB query="<<test.query_ms<<" ms violations="<<(leaf.object_violations+leaf.block_violations)<<'\n';
    return (leaf.object_violations+leaf.block_violations)?2:0;
}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
#endif

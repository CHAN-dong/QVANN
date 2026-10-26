// Plaintext high-dimensional MR-tree, based on Chen et al., PVLDB 7(1), 5.1.1.
// Spatial bulk loading is a documented adaptation, NOT the authors' R*-tree code.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
using Bytes=std::vector<unsigned char>;
using Hash=std::array<unsigned char,32>;
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
void need(bool b,const char*s){if(!b)throw std::runtime_error(s);}
template<class T>void add(Bytes&b,const T&v){auto p=(const unsigned char*)&v;b.insert(b.end(),p,p+sizeof(T));}
void raw(Bytes&b,const void*p,size_t n){auto x=(const unsigned char*)p;b.insert(b.end(),x,x+n);}
Hash sha(const void*p,size_t n){Hash h;SHA256((const unsigned char*)p,n,h.data());return h;}
Hash sha(const Bytes&b){return sha(b.data(),b.size());}
std::string hex(Hash h){static char s[]="0123456789abcdef";std::string x;for(auto b:h){x+=s[b>>4];x+=s[b&15];}return x;}
struct Reader{const Bytes&b;size_t pos=0;const unsigned char*get(size_t n){need(pos<=b.size()&&n<=b.size()-pos,"truncated VO");auto p=b.data()+pos;pos+=n;return p;}template<class T>T scalar(){T x;memcpy(&x,get(sizeof(T)),sizeof(T));return x;}};
struct Fvecs{
 int fd=-1;void*mem=MAP_FAILED;size_t bytes=0,n=0;uint32_t d=0;
 Fvecs(std::string path,size_t count=0){fd=open(path.c_str(),O_RDONLY);need(fd>=0,"open fvecs");struct stat st{};need(fstat(fd,&st)==0,"fstat");bytes=st.st_size;need(bytes>=4,"empty fvecs");mem=mmap(nullptr,bytes,PROT_READ,MAP_PRIVATE,fd,0);need(mem!=MAP_FAILED,"mmap");memcpy(&d,mem,4);need(d>0&&d<=4096,"bad dimension");need(bytes%(4*(d+1))==0,"fvecs size");n=bytes/(4*(d+1));if(count){need(count<=n,"insufficient base");n=count;}}
 const float*row(size_t i)const{need(i<n,"row out of range");return (const float*)((const char*)mem+4*(d+1)*i+4);}
 void validate()const{for(size_t i=0;i<n;++i){uint32_t rd;memcpy(&rd,(const char*)mem+4*(d+1)*i,4);need(rd==d,"fvec dimension mismatch");for(uint32_t j=0;j<d;++j)need(std::isfinite(row(i)[j]),"nonfinite vector");}}
 ~Fvecs(){if(mem!=MAP_FAILED)munmap(mem,bytes);if(fd>=0)close(fd);}
};
double dist(const float*a,const float*b,uint32_t d){double s=0;for(uint32_t j=0;j<d;++j){double x=double(a[j])-b[j];s+=x*x;}return s;}
double bound(const std::vector<float>&lo,const std::vector<float>&hi,const float*q){double s=0;for(size_t j=0;j<lo.size();++j){double x=q[j]<lo[j]?double(lo[j])-q[j]:(q[j]>hi[j]?double(q[j])-hi[j]:0);s+=x*x;}return s;}
using Hit=std::pair<double,uint32_t>;
using Hits=std::priority_queue<Hit>;
void offer(Hits&h,Hit x,uint32_t k){if(h.size()<k)h.push(x);else if(x<h.top()){h.pop();h.push(x);}}
std::vector<uint32_t>answer(Hits h){std::vector<Hit>a;while(!h.empty()){a.push_back(h.top());h.pop();}std::sort(a.begin(),a.end());std::vector<uint32_t>r;for(auto x:a)r.push_back(x.second);return r;}
struct Node{uint32_t begin=0,end=0;std::vector<uint32_t>children;std::vector<float>lo,hi;Hash content{},hash{};bool leaf()const{return children.empty();}};
Hash nodehash(const std::vector<float>&lo,const std::vector<float>&hi,Hash content){Bytes b;add(b,uint32_t(0x4e4f4445));raw(b,lo.data(),lo.size()*4);raw(b,hi.data(),hi.size()*4);raw(b,content.data(),32);return sha(b);}
struct Tree{
 const Fvecs&base;uint32_t fanout=16,leafcap=64,root=0;std::vector<uint32_t>order;std::vector<Node>nodes;
 Tree(const Fvecs&b,uint32_t leaf=64):base(b),leafcap(leaf){need(leaf>0,"leaf capacity");order.resize(base.n);std::iota(order.begin(),order.end(),0);nodes.reserve(base.n/leaf*2+100);root=build(0,base.n,0);}
 // Recursive spatial packing: split on largest sampled range, then group 16 parts.
 // No dimensionality reduction, no approximate distances, no query-specific index.
 void partition(uint32_t begin,uint32_t end,uint32_t groups,std::vector<std::pair<uint32_t,uint32_t>>&parts,uint32_t depth){
  if(groups==1){parts.emplace_back(begin,end);return;}
  std::vector<float>lo(base.d,INFINITY),hi(base.d,-INFINITY);
  uint32_t ns=std::min<uint32_t>(256,end-begin);
  for(uint32_t s=0;s<ns;++s){auto p=base.row(order[begin+uint64_t(s)*(end-begin)/ns]);for(uint32_t j=0;j<base.d;++j){lo[j]=std::min(lo[j],p[j]);hi[j]=std::max(hi[j],p[j]);}}
  uint32_t axis=depth%base.d;for(uint32_t j=0;j<base.d;++j)if(double(hi[j])-lo[j]>double(hi[axis])-lo[axis])axis=j;
  uint32_t left=groups/2,mid=begin+uint64_t(end-begin)*left/groups;
  std::nth_element(order.begin()+begin,order.begin()+mid,order.begin()+end,[&](uint32_t a,uint32_t b){float x=base.row(a)[axis],y=base.row(b)[axis];return x==y?a<b:x<y;});
  partition(begin,mid,left,parts,depth+1);partition(mid,end,groups-left,parts,depth+1);
 }
 uint32_t build(uint32_t begin,uint32_t end,uint32_t depth){
  uint32_t ix=nodes.size();nodes.emplace_back();nodes[ix].begin=begin;nodes[ix].end=end;
  if(end-begin>leafcap){std::vector<std::pair<uint32_t,uint32_t>>parts;partition(begin,end,std::min<uint32_t>(fanout,(end-begin+leafcap-1)/leafcap),parts,depth);for(auto p:parts){uint32_t c=build(p.first,p.second,depth+1);nodes[ix].children.push_back(c);}}
  auto&n=nodes[ix];n.lo.assign(base.d,INFINITY);n.hi.assign(base.d,-INFINITY);Bytes b;
  if(n.leaf()){add(b,uint32_t(1));add(b,end-begin);for(uint32_t i=begin;i<end;++i){uint32_t id=order[i];auto p=base.row(id);add(b,id);raw(b,p,base.d*4);for(uint32_t j=0;j<base.d;++j){n.lo[j]=std::min(n.lo[j],p[j]);n.hi[j]=std::max(n.hi[j],p[j]);}}}
  else{add(b,uint32_t(2));add(b,uint32_t(n.children.size()));for(auto c:n.children){raw(b,nodes[c].hash.data(),32);for(uint32_t j=0;j<base.d;++j){n.lo[j]=std::min(n.lo[j],nodes[c].lo[j]);n.hi[j]=std::max(n.hi[j],nodes[c].hi[j]);}}}
  n.content=sha(b);n.hash=nodehash(n.lo,n.hi,n.content);return ix;
 }
 Hash anchor()const{Bytes b;add(b,uint32_t(0x4d523031));add(b,uint32_t(base.n));add(b,base.d);add(b,fanout);add(b,leafcap);raw(b,nodes[root].hash.data(),32);return sha(b);}
 void save(std::string path)const{
  std::ofstream f(path,std::ios::binary);auto put=[&](const void*p,size_t n){f.write((const char*)p,n);};
  uint32_t meta[]={0x4d523031,uint32_t(base.n),base.d,fanout,leafcap,uint32_t(nodes.size()),root};put(meta,sizeof(meta));
  // Spatially ordered vectors and IDs; no duplicate raw-vector copy in snapshot.
  for(auto id:order){put(&id,4);put(base.row(id),base.d*4);}
  for(auto&n:nodes){uint32_t h[]={n.begin,n.end,uint32_t(n.children.size())};put(h,sizeof(h));put(n.children.data(),n.children.size()*4);put(n.lo.data(),base.d*4);put(n.hi.data(),base.d*4);put(n.content.data(),32);put(n.hash.data(),32);}need(bool(f),"save snapshot");
 }
};
struct Keys{
 EVP_PKEY*p=nullptr;std::array<unsigned char,32>pub{};std::array<unsigned char,64>sig{};
 explicit Keys(Hash anchor){EVP_PKEY_CTX*c=EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519,nullptr);need(c&&EVP_PKEY_keygen_init(c)==1&&EVP_PKEY_keygen(c,&p)==1,"keygen");EVP_PKEY_CTX_free(c);size_t l=32;need(EVP_PKEY_get_raw_public_key(p,pub.data(),&l)==1&&l==32,"public key");EVP_MD_CTX*m=EVP_MD_CTX_new();l=64;need(EVP_DigestSignInit(m,nullptr,nullptr,nullptr,p)==1&&EVP_DigestSign(m,sig.data(),&l,anchor.data(),32)==1&&l==64,"sign");EVP_MD_CTX_free(m);}
 ~Keys(){EVP_PKEY_free(p);}
};
bool signature(Hash h,const unsigned char*sig,const std::array<unsigned char,32>&pub){auto p=EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519,nullptr,pub.data(),32);auto m=EVP_MD_CTX_new();bool ok=p&&m&&EVP_DigestVerifyInit(m,nullptr,nullptr,nullptr,p)==1&&EVP_DigestVerify(m,sig,64,h.data(),32)==1;EVP_MD_CTX_free(m);EVP_PKEY_free(p);return ok;}
struct Search{std::vector<unsigned char>opened;std::vector<uint32_t>ids;uint64_t examined=0,expanded=0;};
Search search(const Tree&t,const float*q,uint32_t k){
 Search s;s.opened.resize(t.nodes.size());Hits hits;using Item=std::pair<double,uint32_t>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>>pq;pq.push({bound(t.nodes[t.root].lo,t.nodes[t.root].hi,q),t.root});
 while(!pq.empty()){auto [lb,ix]=pq.top();pq.pop();if(hits.size()==k&&lb>hits.top().first)break;auto&n=t.nodes[ix];s.opened[ix]=1;++s.expanded;if(n.leaf()){for(uint32_t i=n.begin;i<n.end;++i){auto id=t.order[i];offer(hits,{dist(t.base.row(id),q,t.base.d),id},k);++s.examined;}}
  else for(auto c:n.children){double b=bound(t.nodes[c].lo,t.nodes[c].hi,q);if(hits.size()<k||b<=hits.top().first)pq.push({b,c});}}
 s.ids=answer(hits);return s;
}
void encode_node(Bytes&b,const Tree&t,const Search&s,uint32_t ix){auto&n=t.nodes[ix];if(!s.opened[ix]){add(b,uint8_t(0));raw(b,n.lo.data(),t.base.d*4);raw(b,n.hi.data(),t.base.d*4);raw(b,n.content.data(),32);}
 else if(n.leaf()){add(b,uint8_t(1));add(b,n.end-n.begin);for(uint32_t i=n.begin;i<n.end;++i){auto id=t.order[i];add(b,id);raw(b,t.base.row(id),t.base.d*4);}}
 else{add(b,uint8_t(2));add(b,uint32_t(n.children.size()));for(auto c:n.children)encode_node(b,t,s,c);}}
Bytes encode(const Tree&t,const Search&s,const float*q,uint32_t k,const Keys&key){Bytes b;b.reserve(s.examined*(t.base.d*4+4)+t.nodes.size()*16);add(b,uint32_t(0x564f3031));add(b,uint32_t(t.base.n));add(b,t.base.d);add(b,k);auto a=t.anchor();raw(b,a.data(),32);raw(b,key.sig.data(),64);auto qh=sha(q,t.base.d*4);raw(b,qh.data(),32);for(auto id:s.ids)add(b,id);encode_node(b,t,s,t.root);return b;}
struct Decoded{std::vector<float>lo,hi;Hash hash;};
// Verifier has no Tree/base access: only public dimensions/root/key, q, k and wire.
struct Verifier{
 Reader r;const float*q;uint32_t n,d,k,leafcap,fanout;Hash expected;std::array<unsigned char,32>pub;Hits hits;double min_pruned=INFINITY;uint64_t points=0,nodes=0;std::vector<uint8_t>seen;
 Verifier(const Bytes&b,const float*qq,uint32_t nn,uint32_t dd,uint32_t kk,uint32_t lc,uint32_t fo,Hash root,std::array<unsigned char,32>pk):r{b},q(qq),n(nn),d(dd),k(kk),leafcap(lc),fanout(fo),expected(root),pub(pk),seen(nn,0){}
 Decoded node(uint32_t depth=0){need(depth<64&&++nodes<=uint64_t(n)*2+1,"invalid tree size");auto type=r.scalar<uint8_t>();Decoded z;z.lo.assign(d,INFINITY);z.hi.assign(d,-INFINITY);Hash content;
  if(type==0){memcpy(z.lo.data(),r.get(d*4),d*4);memcpy(z.hi.data(),r.get(d*4),d*4);memcpy(content.data(),r.get(32),32);for(uint32_t j=0;j<d;++j)need(std::isfinite(z.lo[j])&&std::isfinite(z.hi[j])&&z.lo[j]<=z.hi[j],"bad MBB");min_pruned=std::min(min_pruned,bound(z.lo,z.hi,q));}
  else if(type==1){uint32_t count=r.scalar<uint32_t>();need(count>0&&count<=leafcap,"invalid leaf count");size_t start=r.pos;std::vector<float>v(d);for(uint32_t i=0;i<count;++i){auto id=r.scalar<uint32_t>();need(id<n&&!seen[id],"duplicate or invalid id");seen[id]=1;memcpy(v.data(),r.get(d*4),d*4);for(uint32_t j=0;j<d;++j){need(std::isfinite(v[j]),"nonfinite leaf");z.lo[j]=std::min(z.lo[j],v[j]);z.hi[j]=std::max(z.hi[j],v[j]);}offer(hits,{dist(v.data(),q,d),id},k);++points;}
   Bytes b;add(b,uint32_t(1));add(b,count);raw(b,r.b.data()+start,r.pos-start);content=sha(b);}
  else if(type==2){uint32_t count=r.scalar<uint32_t>();need(count>=2&&count<=fanout,"invalid fanout");Bytes b;add(b,uint32_t(2));add(b,count);for(uint32_t i=0;i<count;++i){auto c=node(depth+1);raw(b,c.hash.data(),32);for(uint32_t j=0;j<d;++j){z.lo[j]=std::min(z.lo[j],c.lo[j]);z.hi[j]=std::max(z.hi[j],c.hi[j]);}}content=sha(b);}
  else throw std::runtime_error("bad node type");z.hash=nodehash(z.lo,z.hi,content);return z;
 }
 bool run(){try{
  need(r.scalar<uint32_t>()==0x564f3031&&r.scalar<uint32_t>()==n&&r.scalar<uint32_t>()==d&&r.scalar<uint32_t>()==k,"VO header mismatch");Hash a;memcpy(a.data(),r.get(32),32);need(a==expected&&signature(a,r.get(64),pub),"root/signature");Hash qh;memcpy(qh.data(),r.get(32),32);need(qh==sha(q,d*4),"query mismatch");std::vector<uint32_t>claimed;for(uint32_t i=0;i<k;++i)claimed.push_back(r.scalar<uint32_t>());auto z=node();need(r.pos==r.b.size(),"trailing bytes");Bytes b;add(b,uint32_t(0x4d523031));add(b,n);add(b,d);add(b,fanout);add(b,leafcap);raw(b,z.hash.data(),32);need(sha(b)==a,"Merkle root mismatch");need(hits.size()==k&&answer(hits)==claimed,"answer mismatch");need(min_pruned>hits.top().first,"unproved omitted subtree");return true;
 }catch(const std::exception&){return false;}}
};
// Independent exhaustive oracle: no tree, no search bounds; separate full scan.
std::vector<std::vector<uint32_t>>oracle(const Fvecs&b,const Fvecs&q,uint32_t off,uint32_t nq,uint32_t maxk){std::vector<Hits>h(nq);for(uint32_t id=0;id<b.n;++id)for(uint32_t j=0;j<nq;++j)offer(h[j],{dist(b.row(id),q.row(off+j),b.d),id},maxk);std::vector<std::vector<uint32_t>>o;for(auto x:h)o.push_back(answer(x));return o;}
int main(int argc,char**argv)try{
 need(argc==9,"usage: mr base.fvecs query.fvecs N output-dir k-list queries repeats leafcap");std::string out=argv[4];std::filesystem::create_directories(out);uint32_t nq=std::stoul(argv[6]),reps=std::stoul(argv[7]),lc=std::stoul(argv[8]);std::vector<uint32_t>ks;std::string list=argv[5];for(size_t p=0;p<list.size();){size_t e=list.find(',',p);ks.push_back(std::stoul(list.substr(p,e-p)));if(e==std::string::npos)break;p=e+1;}
 uint32_t maxk=*std::max_element(ks.begin(),ks.end()),off=100;Fvecs base(argv[1],std::stoull(argv[3])),query(argv[2]);need(query.d==base.d&&query.n>=off+nq&&base.n>=maxk,"input sizes");base.validate();query.validate();
 auto t=Clock::now();Tree tree(base,lc);Keys keys(tree.anchor());double build_ms=elapsed(t);t=Clock::now();tree.save(out+"/serving.snapshot");{std::ofstream f(out+"/serving.snapshot",std::ios::app|std::ios::binary);f.write((char*)keys.sig.data(),64);f.write((char*)keys.pub.data(),32);}double save_ms=elapsed(t);
 std::cout<<"BUILT N="<<base.n<<" D="<<base.d<<" nodes="<<tree.nodes.size()<<" build_s="<<build_ms/1000<<std::endl;
 t=Clock::now();auto truth=oracle(base,query,off,nq,maxk);double oracle_ms=elapsed(t);std::cout<<"ORACLE seconds="<<oracle_ms/1000<<std::endl;
 std::ofstream csv(out+"/samples.csv");csv<<"k,query_id,repeat,recall,query_ms,search_ms,encode_ms,verify_ms,vo_bytes,examined,expanded,valid\n"<<std::setprecision(17);
 uint32_t attack_tests=0;uint64_t maxvo=0;
 for(auto k:ks){for(uint32_t rep=0;rep<reps;++rep)for(uint32_t j=0;j<nq;++j){auto q=query.row(off+j);t=Clock::now();auto s=search(tree,q,k);double search_ms=elapsed(t);t=Clock::now();auto wire=encode(tree,s,q,k,keys);double encode_ms=elapsed(t);maxvo=std::max<uint64_t>(maxvo,wire.size());t=Clock::now();Verifier v(wire,q,base.n,base.d,k,tree.leafcap,tree.fanout,tree.anchor(),keys.pub);bool valid=v.run();double verify_ms=elapsed(t);need(valid,"honest verification failed");std::vector<uint32_t>gt(truth[j].begin(),truth[j].begin()+k);need(s.ids==gt,"oracle exactness failed");
  csv<<k<<','<<off+j<<','<<rep<<",1,"<<search_ms+encode_ms<<','<<search_ms<<','<<encode_ms<<','<<verify_ms<<','<<wire.size()<<','<<s.examined<<','<<s.expanded<<",1\n";csv.flush();
  if(rep==0&&j==0){auto reject=[&](const Bytes&b,const float*qq,uint32_t kk){Verifier bad(b,qq,base.n,base.d,kk,tree.leafcap,tree.fanout,tree.anchor(),keys.pub);need(!bad.run(),"tamper accepted");++attack_tests;};
   // Signature, query commitment, returned ID, node/coordinates/hash, truncation,
   // appended garbage, wrong external query and wrong external k.
   for(size_t pos:{size_t(48),size_t(112),size_t(144),wire.size()/2,wire.size()-1}){auto b=wire;b[pos]^=1;reject(b,q,k);}auto b=wire;b.pop_back();reject(b,q,k);b=wire;b.push_back(0);reject(b,q,k);std::vector<float>wrong(q,q+base.d);wrong[0]=std::nextafter(wrong[0],INFINITY);reject(wire,wrong.data(),k);reject(wire,q,k+1);
   if(k==ks.front()){std::ofstream f(out+"/example.vo",std::ios::binary);f.write((char*)wire.data(),wire.size());}}
 }std::cout<<"DONE k="<<k<<" samples="<<nq*reps<<std::endl;}
 struct rusage ru{};getrusage(RUSAGE_SELF,&ru);std::ofstream meta(out+"/native.json");meta<<std::setprecision(17)<<"{\"N\":"<<base.n<<",\"D\":"<<base.d<<",\"fanout\":"<<tree.fanout<<",\"leaf_capacity\":"<<lc<<",\"nodes\":"<<tree.nodes.size()<<",\"build_seconds\":"<<build_ms/1000<<",\"snapshot_write_seconds\":"<<save_ms/1000<<",\"index_bytes\":"<<std::filesystem::file_size(out+"/serving.snapshot")<<",\"oracle_seconds\":"<<oracle_ms/1000<<",\"peak_rss_KiB\":"<<ru.ru_maxrss<<",\"max_vo_bytes\":"<<maxvo<<",\"tamper_tests\":"<<attack_tests<<",\"root\":\""<<hex(tree.anchor())<<"\",\"threads\":1}";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}

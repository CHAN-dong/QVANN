// After timing: compare exact float64 ground truth with the existing project's
// scalar float32 squared_l2 + ID tie-break, for every evaluated query and k.
#define main chen_benchmark_unused
#include "mr.cpp"
#undef main
float project_distance(const float*a,const float*b,uint32_t d){float s=0;for(uint32_t j=0;j<d;++j){float x=a[j]-b[j];s+=x*x;}return s;}
int main(int argc,char**argv)try{
 need(argc==6,"compatibility base query N k-list output.csv");Fvecs b(argv[1],std::stoull(argv[3])),q(argv[2]);std::vector<uint32_t>ks;std::string list=argv[4];for(size_t p=0;p<list.size();){auto e=list.find(',',p);ks.push_back(std::stoul(list.substr(p,e-p)));if(e==std::string::npos)break;p=e+1;}
 uint32_t maxk=*std::max_element(ks.begin(),ks.end());auto exact=oracle(b,q,100,20,maxk);std::vector<Hits>heaps(20);
 for(uint32_t id=0;id<b.n;++id)for(uint32_t j=0;j<20;++j)offer(heaps[j],{project_distance(b.row(id),q.row(100+j),b.d),id},maxk);
 std::ofstream f(argv[5]);f<<"query_id,k,shared_float32_recall,exact_float64_recall\n"<<std::setprecision(17);
 for(uint32_t j=0;j<20;++j){auto other=answer(heaps[j]);for(auto k:ks){uint32_t hits=0;for(uint32_t i=0;i<k;++i)if(std::find(other.begin(),other.begin()+k,exact[j][i])!=other.begin()+k)++hits;f<<100+j<<','<<k<<','<<double(hits)/k<<",1\n";}}
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}

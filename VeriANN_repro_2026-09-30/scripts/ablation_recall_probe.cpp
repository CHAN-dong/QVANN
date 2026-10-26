// Fresh recall-only execution of the unchanged six ablation search paths.
#include "ablation_recall_protocol_include.cpp"

int main(int argc,char**argv)try{
    namespace a=ablation;namespace p=paper_repro;namespace o=optimized;
    auto c=p::args(argc,argv);p::require(c.ks==std::vector<int>{10},"k must be10");
    p::require(c.efs.size()==6,"supply parameters in variant1..6 order");
    p::keccak=false;p::build_threads=1;omp_set_num_threads(1);
    auto base=vann::read_fvecs(c.dataset+"/sift_base.fvecs",c.base),queries=p::read_queries(c);
    p::require(base.rows()==size_t(c.base)&&queries.rows()==size_t(c.queries),"short dataset");
    auto data=p::data_hash(base);auto gt=p::ground_truth(base,queries,10);
    auto stem=c.cache+"/"+p::hex(data).substr(0,16);
    auto graph=stem+"_M"+std::to_string(c.M)+"_efc"+std::to_string(c.efc)+".index";
    auto ivfpath=stem+"_ivf256_train65536_iter12_seed42_v1.bin";
    p::require(std::filesystem::exists(graph)&&std::filesystem::exists(ivfpath),"original caches required; will not retrain");
    auto h=vann::build_hnsw(base,c.M,c.efc,graph);a::IVF ivf(base,256,ivfpath);
    base.values.clear();base.values.shrink_to_fit();o::Codec codec(h);
    std::ofstream truth(c.output+"/ground_truth.csv");truth<<"query,answer\n";
    for(int qi=0;qi<c.queries;++qi){truth<<qi+c.queryoffset<<",\"";for(auto id:gt[qi])truth<<id<<' ';truth<<"\"\n";}
    std::ofstream f(c.output+"/recall.csv");f<<"variant,query,repeat,parameter,hits,recall_at_k,answer\n"<<std::setprecision(17);
    for(int rep=0;rep<c.repeats;++rep)for(int qi=0;qi<c.queries;++qi){
        std::map<int,std::vector<uint32_t>>answers;
        for(int v=1;v<=6;++v){
            auto result=a::search(h,codec,v,a::is_ivf(v)?&ivf:nullptr,queries.row(qi),10,c.efs[v-1]);
            p::require(result.answer.size()==10,"not a complete top10");answers[v]=result.answer;
            int hits=0;for(auto id:result.answer)hits+=std::find(gt[qi].begin(),gt[qi].end(),id)!=gt[qi].end();
            f<<v<<','<<qi+c.queryoffset<<','<<rep<<','<<c.efs[v-1]<<','<<hits<<','<<double(hits)/10<<",\"";
            for(auto id:result.answer)f<<id<<' ';f<<"\"\n";
        }
        for(auto pair:std::vector<std::pair<int,int>>{{1,3},{2,4},{5,6}})
            p::require(answers.at(pair.first)==answers.at(pair.second),"paired variants changed answers");
        if(qi+1==c.queries){f.flush();std::cout<<"RECALL_REPEAT_COMPLETE "<<rep<<std::endl;}
    }
    std::ofstream meta(c.output+"/identity.json");meta<<"{\"dataset_sha256\":\""<<p::hex(data)<<"\",\"N\":"<<c.base<<",\"D\":"<<h.dimension()<<",\"queries\":"<<c.queries<<",\"repeats\":"<<c.repeats<<",\"query_offset\":"<<c.queryoffset<<",\"threads\":1}";
    std::ofstream(c.output+"/PASS.txt")<<"Fresh recall runs; complete top10; exact ground truth; paired answers unchanged. No proof-time measurement.\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}

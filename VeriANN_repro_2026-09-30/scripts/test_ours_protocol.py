"""Test wire verification, including omissions with authentic Merkle subproofs.

Compiles a test translation unit from the exact benchmark source; only include
paths and the benchmark main name are changed. The formal benchmark is untouched.
Run after the sequential timed study has finished to avoid CPU interference.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
from run_ours_benchmark import ROOT


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',type=Path,default=ROOT/'src/experiments/veriann_optimized_benchmark.cpp')
    parser.add_argument('--output',type=Path,default=ROOT/'experiments/work9-ours-20261015/protocol_validation.json')
    args=parser.parse_args()
    source=args.source.resolve()
    code=source.read_text()
    code=code.replace('#include "refinement_merkle_ordering.cpp"',
                      '#include "'+str(ROOT/'src/experiments/refinement_merkle_ordering.cpp')+'"')
    code=code.replace('#include "../paper_repro_common.hpp"',
                      '#include "'+str(ROOT/'src/paper_repro_common.hpp')+'"')
    code=code.replace('int main(int argc,char**argv)try{','int benchmark_main_unused(int argc,char**argv)try{')
    generator=code[code.index('Packet generate('):code.index('// The verifier receives')]
    malicious=generator.replace('Packet generate(','Packet omit_refinement(')
    malicious=malicious.replace('const float*q,int k,int E){','const float*q,int k,int E,Id omitted){')
    original='for(Id id:t.disclosed)if(codec.lower(q,codec.sq(id))<=tau)'
    assert original in malicious
    malicious=malicious.replace(original,'for(Id id:t.disclosed)if(id!=omitted&&codec.lower(q,codec.sq(id))<=tau)')
    code+='\nnamespace optimized {\n'+malicious+'\n}\n'
    code+=r'''
int main(int argc,char**argv)try{
    using namespace optimized;namespace p=paper_repro;
    p::require(argc==3,"usage: test <data> <index>");
    auto base=vann::read_fvecs(std::string(argv[1])+"/sift_base.fvecs",100000);
    auto qs=vann::read_fvecs(std::string(argv[1])+"/sift_query.fvecs",10);
    auto data=p::data_hash(base);omp_set_num_threads(1);auto h=vann::build_hnsw(base,16,100,argv[2]);
    Codec codec(h),client(codec,true);ADS ads(h,codec,data);
    int honest=0,omitted=0,shortened=0,reordered=0;
    for(int qi=0;qi<10;++qi)for(int E:{16,64,128})for(int k:{1,10,50}){
        const float*q=qs.row(qi);
        auto trace=walk(codec,q,E,ads.anchor.entry,ads.anchor.maxlevel,[&](Id id){return codec.sq(id);},[&](Id id,Id l){return neighbors(h,id,l);});
        auto exact_for=[&](const Walk&t){std::vector<std::pair<float,Id>>v;for(Id id:t.disclosed)v.emplace_back(vann::squared_l2(q,h.vector(id),h.dimension()),id);std::sort(v.begin(),v.end());return v;};
        auto exact=exact_for(trace);if(exact.size()<size_t(k))continue;
        std::vector<Id>answer;for(int i=0;i<k;++i)answer.push_back(exact[i].second);float tau=exact[k-1].first;
        auto packet=generate(h,codec,ads,trace,answer,tau,q,k,E);
        p::require(verify(packet.wire,ads.anchor,client,q,k,E),"honest VO rejected");++honest;
        if(k>1){auto wrong=packet.wire;for(int j=0;j<4;++j)std::swap(wrong[48+j],wrong[52+j]);
            p::require(!verify(wrong,ads.anchor,client,q,k,E),"reordered valid answers accepted");++reordered;}
        for(Id id:trace.disclosed)if(std::find(answer.begin(),answer.end(),id)==answer.end()&&codec.lower(q,codec.sq(id))<=tau){
            auto wrong=omit_refinement(h,codec,ads,trace,answer,tau,q,k,E,id);
            p::require(!verify(wrong.wire,ads.anchor,client,q,k,E),"authentic proof with required refinement omitted accepted");++omitted;break;
        }
        auto short_trace=walk(codec,q,E/2,ads.anchor.entry,ads.anchor.maxlevel,[&](Id id){return codec.sq(id);},[&](Id id,Id l){return neighbors(h,id,l);});
        auto short_exact=exact_for(short_trace);if(short_exact.size()>=size_t(k)){
            std::vector<Id>a;for(int i=0;i<k;++i)a.push_back(short_exact[i].second);
            // Honest Merkle proofs and valid query/E header, but search stopped early.
            auto wrong=generate(h,codec,ads,short_trace,a,short_exact[k-1].first,q,k,E);
            p::require(!verify(wrong.wire,ads.anchor,client,q,k,E),"authentic proof with incomplete traversal accepted");++shortened;
        }
    }
    p::require(honest>=80&&omitted>=20&&shortened>=80&&reordered>=50,"insufficient protocol cases");
    std::cout<<"{\"status\":\"PASS\",\"honest\":"<<honest<<",\"valid_proof_refinement_omissions_rejected\":"<<omitted
             <<",\"valid_proof_incomplete_traversals_rejected\":"<<shortened<<",\"reordered_answers_rejected\":"<<reordered<<"}"<<std::endl;
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
'''
    generated=ROOT/'build/test_ours_protocol.cpp';generated.write_text(code)
    faiss=ROOT/'third_party/LearnedEarlyTer-20SIGMOD'
    upstream=ROOT/'ANNProof-24FGCS/upstream/source/similarity_search'
    binary=ROOT/'build/test_ours_protocol'
    compile=['g++','-O3','-std=c++17','-fopenmp','-I'+str(ROOT/'src'),'-I'+str(ROOT/'third_party'),
             '-I'+str(faiss),'-I'+str(faiss/'include'),'-I'+str(upstream/'include'),str(generated),
             str(upstream/'src/Keccak256.cc'),'-o',str(binary),'-L'+str(faiss),'-L'+str(faiss/'lib'),
             '-lfaiss','-l_lightgbm','-lcrypto','-lpthread','-ldl']
    env=os.environ.copy();env['OMP_NUM_THREADS']=env['OPENBLAS_NUM_THREADS']='1'
    env['LD_LIBRARY_PATH']=':'.join(map(str,[ROOT/'third_party/runtime/lib',faiss,faiss/'lib']))
    subprocess.run(compile,cwd=ROOT,env=env,check=True,stdout=subprocess.DEVNULL)
    output=subprocess.check_output([str(binary),str(ROOT/'dataset/sift'),str(ROOT/'merkle_hnsw/sift100k_M16_efc100.index')],env=env,text=True)
    result=json.loads(output.strip().splitlines()[-1])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__':main()

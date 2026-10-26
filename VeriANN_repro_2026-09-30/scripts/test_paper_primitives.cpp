#include "../src/paper_repro_common.hpp"
using namespace paper_repro;
int main() try {
    size_t cases=0;
    for(bool mode:{false,true}) {
        keccak=mode;
        for(uint32_t n:{1,2,3,7,8,9,31,32,33,65}) {
            std::vector<Hash> leaves;
            for(uint32_t i=0;i<n;++i){Bytes b;u32(b,i);leaves.push_back(hash(b));}
            Tree tree(leaves);
            for(bool paths:{false,true})for(int stride:{1,2,5}) {
                std::set<uint32_t> positions;std::map<uint32_t,Hash> targets;
                for(uint32_t i=0;i<n;i+=stride){positions.insert(i);targets[i]=leaves[i];}
                auto proof=tree.prove(positions,paths);
                require(Tree::verify(tree.root(),n,targets,proof),"valid sparse proof rejected");
                auto modified=targets;modified.begin()->second[0]^=1;
                require(!Tree::verify(tree.root(),n,modified,proof),"tampered leaf accepted");
                auto root=tree.root();root[0]^=1;
                require(!Tree::verify(root,n,targets,proof),"wrong root accepted");
                if(!proof.empty()){auto changed=proof;changed[0].value[0]^=1;require(!Tree::verify(tree.root(),n,targets,changed),"tampered sibling accepted");}
                ++cases;
            }
        }
    }
    Bytes bytes;u32(bytes,42);u64(bytes,UINT64_MAX);f32(bytes,1.5);string(bytes,"prefix");
    Reader reader{bytes};require(reader.u32()==42&&reader.u64()==UINT64_MAX&&reader.f32()==1.5&&reader.string()=="prefix","wire roundtrip failed");reader.end();
    bool failed=false;bytes.pop_back();try{Reader truncated{bytes};truncated.u32();truncated.u64();truncated.f32();truncated.string();}catch(...){failed=true;}require(failed,"truncated string accepted");
    std::cout<<"PASS "<<cases<<" sparse Merkle cases, both hashes, both proof modes, padding and wire bounds\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}

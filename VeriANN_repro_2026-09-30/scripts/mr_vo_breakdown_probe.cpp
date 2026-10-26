// Reuse the unchanged plaintext MR-tree search, encoder and verifier.
#define main archived_mr_main
#include "../Chen13-MR/mr.cpp"
#undef main
int main(int argc,char**argv)try{
    need(argc==5,"usage: probe base.fvecs query.fvecs N output");
    std::string out=argv[4];std::filesystem::create_directories(out);
    Fvecs base(argv[1],std::stoull(argv[3])),queries(argv[2]);base.validate();queries.validate();
    need(queries.d==base.d&&queries.n>=120,"queries");Tree tree(base,64);Keys keys(tree.anchor());
    std::ofstream f(out+"/breakdown.csv");f<<"query,repeat,vo_bytes,T_bytes,A_bytes,frontier_nodes,opened_nodes,examined,answer,valid\n";
    for(int rep=0;rep<3;++rep)for(int qi=100;qi<120;++qi){
        auto q=queries.row(qi);auto s=search(tree,q,10);auto wire=encode(tree,s,q,10,keys);
        Verifier v(wire,q,base.n,base.d,10,tree.leafcap,tree.fanout,tree.anchor(),keys.pub);need(v.run(),"verification");
        uint64_t frontier=0,opened=0;
        std::function<void(uint32_t)>count=[&](uint32_t id){if(!s.opened[id]){++frontier;return;}++opened;for(auto c:tree.nodes[id].children)count(c);};count(tree.root);
        // T: MBBs, vector/ID data and traversal/control framing.
        // A: signed root(32+64), query commitment32 and frontier digests32 each.
        uint64_t auth=128+frontier*32;
        uint64_t expected=184+opened*5+s.examined*(4+base.d*4)+frontier*(1+base.d*8+32);
        need(expected==wire.size(),"byte accounting mismatch");
        f<<qi<<','<<rep<<','<<wire.size()<<','<<wire.size()-auth<<','<<auth<<','<<frontier<<','<<opened<<','<<s.examined<<',';for(auto id:s.ids)f<<id<<' ';f<<",1\n";f.flush();
        if(qi==100&&rep==0){std::ofstream w(out+"/query0.vo",std::ios::binary);w.write((char*)wire.data(),wire.size());}
        if(qi==119)std::cout<<"REPEAT_COMPLETE "<<rep<<std::endl;
    }
    std::ofstream(out+"/PASS.txt")<<"60 full VOs generated, verified, and structurally accounted.\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}

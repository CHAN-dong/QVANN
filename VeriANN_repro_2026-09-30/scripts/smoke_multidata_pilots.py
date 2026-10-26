"""Check search-only selectors against full authenticated protocols at d=960."""
from run_work9_multidata import *

def main():
    initialize();dataset='gist1m';w=width(dataset);checks=[]
    for method in METHODS:
        prior=pilot(dataset,method,1000,[64],[1],'smoke-1000',w)
        out=RESULTS/'smoke'/method;out.parent.mkdir(parents=True,exist_ok=True)
        if not (out/'PASS.txt').exists():
            if method=='Ours':
                cmd=[sys.executable,str(ROOT/'scripts/run_ours_benchmark.py'),'--source',str(ROOT/'build/veriann_lut_benchmark.cpp'),'--binary',str(ROOT/'build/veriann_lut_benchmark'),'--E','64']
            else:
                folder='ANNProof-24FGCS' if method=='ANNProof' else 'VLAH-25TrustCom'
                cmd=[sys.executable,str(ROOT/folder/'run.py'),'--ef-search','64','--probes','1','--lsh-width',str(w)]
            cmd+=['--dataset',str(ROOT/'dataset'/dataset),'--base','1000','--queries',str(QUERY_COUNT),'--query-offset',str(QUERY_OFFSET),'--repeats',str(REPEATS),'--build-threads','16','--M',str(INDEX_M),'--ef-construction',str(INDEX_EFC),'--k','10','--output',str(out),'--skip-build']
            launch(cmd,out,STUDY/('smoke-'+method+'.log'))
        compare_pilot(out,[dict(ef=64,probes=1,pilot=str(prior))]);checks.append(dict(method=method,status='PASS',output=str(out)))
    (STUDY/'pilot_smoke_validation.json').write_text(json.dumps(dict(status='PASS',checks=checks),indent=2)+'\n')
    event(action='pilot_equivalence_smoke_passed',methods=3,dimension=960)

if __name__=='__main__':main()

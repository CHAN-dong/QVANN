"""Build search-only calibration binaries without changing protocol sources."""
import json
import subprocess
from work9_multidata import ROOT,STUDY,sha

def main():
    STUDY.mkdir(parents=True,exist_ok=True)
    original=ROOT/'build/veriann_lut_benchmark.cpp'
    code=original.read_text();signature='int main(int argc,char**argv)'
    assert code.count(signature)==1
    include=ROOT/'build/multidata_lut_include.cpp'
    include.write_text(code.replace(signature,'int archived_lut_protocol_main(int argc,char**argv)'))
    e=json.loads((ROOT/'ANNProof-24FGCS/results/work9-20261015/sift1m-recall-curve/environment.json').read_text())
    records=[]
    for method,kind in [('ANNProof',1),('VLAH',2),('Ours',3)]:
        cmd=e['build_command'].copy()
        cmd[cmd.index(str(ROOT/'ANNProof-24FGCS/annproof.cpp'))]=str(ROOT/'src/experiments/multidata_search_calibration.cpp')
        binary=ROOT/'build'/('multidata_pilot_'+method);cmd[cmd.index('-o')+1]=str(binary)
        cmd.insert(1,'-DPILOT_KIND='+str(kind))
        log=STUDY/('build-pilot-'+method+'.log')
        with log.open('w') as f:status=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
        if status.returncode:raise RuntimeError(log.read_text()[-6000:])
        records.append(dict(method=method,command=cmd,binary_sha256=sha(binary)))
        print('Built '+method,flush=True)
    (STUDY/'pilot_build.json').write_text(json.dumps(dict(builds=records,source_sha256=sha(ROOT/'src/experiments/multidata_search_calibration.cpp'),lut_source_sha256=sha(original),calibration_include_sha256=sha(include),only_lut_change='rename unused original main function'),indent=2)+'\n')

if __name__=='__main__':main()

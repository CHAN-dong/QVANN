"""Match the two out-of-band Ours scale points on the fixed query workload.

All initial independent-calibration/evaluation records remain available. These
additional measurements are explicitly workload matching, not held-out estimates.
"""
import subprocess
import sys
from run_work9_ours import run
from fill_work9_ours import ROOT,runs,size_evaluation


def main():
    dataset=ROOT/'dataset/bigann10m'
    run('size-500000-matched-recall',dataset,n=500000,E='112,128,144')
    run('size-10000000-matched-recall',dataset,n=10000000,E='320,384,448,512')
    measurements=runs()
    for n in [100000,500000,1000000,5000000,10000000]:
        selected=size_evaluation(measurements,n)
        assert selected and 94<=selected['recall_percent']<=96,(n,selected)
    subprocess.run([sys.executable,str(ROOT/'scripts/fill_work9_ours.py'),'--update-original'],cwd=ROOT,check=True)


if __name__=='__main__':main()

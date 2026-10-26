"""Independent saved-file and workbook consistency audit after measurement."""
import json,platform,os,subprocess
from pathlib import Path
import openpyxl,numpy,faiss
from work9_index_study import ROOT,STUDY,OUT,DATA,sha,write,env
w=openpyxl.load_workbook(ROOT.parent/'work9.xlsx')
evidence=[]
for ds,(title,n,d,*_) in DATA.items():
    graph_hashes={}
    for method,row in [('VLAH',9),('ANNProof',10),('Ours',11),('V3DB',12)]:
        p=OUT/ds/method;r=json.loads((p/'measurement.json').read_text())
        assert r['N']==n and r['D']==d and r['threads']==1 and r['status']=='measured'
        assert abs(w[title][f'I{row}'].value-r['total_bytes']/1e6)<1e-8
        assert abs(w[title][f'J{row}'].value-r['construct_seconds'])<1e-8
        if method in ['ANNProof','Ours']:
            idx=p/'cache/global.index'
            # Read using the same vendored research Faiss that wrote the file.
            checked=subprocess.run([str(ROOT/'build/inspect_cached_graph'),str(idx),str(d),str(n)],env=env(),capture_output=True,text=True,check=True)
            (p/'index-readback.txt').write_text(checked.stdout)
            graph_hashes[method]=sha(idx)
        elif method=='V3DB':
            with numpy.load(p/'search.snapshot') as f:
                assert f['codes'].shape==(n,r['M']) and f['centers'].shape==(r['n_list'],d)
                offsets=f['group_offsets'];ids=f['group_ids']
                assert offsets[0]==0 and offsets[-1]==n and len(ids)==n
                assert numpy.max(numpy.diff(offsets))<=r['capacity']
                assert numpy.array_equal(numpy.sort(ids),numpy.arange(n))
            with numpy.load(p/'auth.snapshot') as f:assert len(f['list_roots'])==r['n_list'] and f['roots'].shape==(2,)
        else:
            indexes=list((p/'cache').glob('*.index'))
            assert len(indexes)==(n+19999)//20000
            count=0
            for idx in indexes:
                begin=int(idx.stem.split('-')[1]);size=min(20000,n-begin)
                subprocess.run([str(ROOT/'build/inspect_cached_graph'),str(idx),str(d),str(size)],env=env(),capture_output=True,text=True,check=True);count+=size
            assert count==n
    evidence.append(dict(dataset=ds,shared_ANNProof_Ours_graph_identical=graph_hashes['ANNProof']==graph_hashes['Ours'],graph_hashes=graph_hashes))
cpu=next((line.split(':',1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')),'unknown')
write(STUDY/'environment.json',dict(platform=platform.platform(),CPU=cpu,logical_CPUs=os.cpu_count(),available_affinity=list(os.sched_getaffinity(0)),RAM_kB=next(line for line in Path('/proc/meminfo').read_text().splitlines() if line.startswith('MemTotal')),per_process_address_space_limit_GiB=50,numpy=numpy.__version__,faiss=faiss.__version__,openpyxl=openpyxl.__version__,construction_threads=1,parallel_timing_jobs=1,build_repeats=1))
write(STUDY/'independent-audit.json',dict(status='PASS',checked_measurements=16,checked_cells=32,validated_saved_index_dimensions=True,validated_v3db_list_membership=True,workbook_sha256=sha(ROOT.parent/'work9.xlsx'),datasets=evidence))
print('PASS: 16 measurements, 32 workbook cells, saved indexes and V3DB membership validated.')

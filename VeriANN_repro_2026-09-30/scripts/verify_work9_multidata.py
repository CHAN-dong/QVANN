"""Audit new workbook cells, protocol evidence, scope and preserved original sheets."""
from work9_multidata import *

def main():
    wb=openpyxl.load_workbook(OUTPUT);backup=openpyxl.load_workbook(BACKUP)
    for name in backup.sheetnames:
        a,b=backup[name],wb[name]
        assert set(map(str,a.merged_cells.ranges))==set(map(str,b.merged_cells.ranges)),name
        assert len(a._charts)==len(b._charts),(name,'original chart count changed')
        for row in a:
            for c in row:assert b[c.coordinate].value==c.value,(name,c.coordinate,'original changed')
    source_rows={}
    for row in wb['新增数据单元格来源'].iter_rows(min_row=2,values_only=True):source_rows[(row[0],row[1])]=row
    record_files=list(STUDY.glob('evaluation-*.json'))
    assert len(record_files)==45,('incomplete matrix',len(record_files))
    dataset_hashes={};outputs={};settings={}
    for file in record_files:
        job=json.loads(file.read_text());out=Path(job['output']);outputs[str(out)]=job
        key=(job['dataset'],job['base'])
        manifest=json.loads((out/'manifest.json').read_text())
        assert (manifest['base'],manifest['query_offset'],manifest['repeats'],manifest['M'],manifest['ef_construction'],manifest['query_threads'])==(job['base'],QUERY_OFFSET,REPEATS,INDEX_M,INDEX_EFC,1)
        if manifest['queries']!=QUERY_COUNT:
            assert manifest['queries']==100
            assert job['dataset']=='gist1m' and job['method']=='ANNProof'
            assert out.name in ['ANNProof-curve-1000000','ANNProof-k-1000000']
        assert job['query_count']==QUERY_COUNT
        assert manifest['metric']=='l2' and manifest['proof']=='multiproof'
        if key in dataset_hashes:assert dataset_hashes[key]==manifest['dataset_sha256']
        dataset_hashes[key]=manifest['dataset_sha256']
        if job['method']=='VLAH':
            assert math.isclose(manifest['lsh_width'],job['width'],rel_tol=5e-6)
            assert float((out/'lsh_parameters.txt').read_text().splitlines()[0].split()[3])==job['width']
        settings[file.name]=job
    measurements_count=0;raw_measurements_count=0;tamper_count=0;lookup={}
    for path,job in outputs.items():
        out=Path(path);assert (out/'PASS.txt').exists()
        attacks=list(csv.DictReader((out/'tamper_tests.csv').open()))
        assert len(attacks)=={'ANNProof':8,'VLAH':11,'Ours':13}[job['method']]
        assert all(r['rejected']=='1' for r in attacks);tamper_count+=len(attacks)
        rows=list(csv.DictReader((out/'per_query.csv').open()));assert all(r['verified']=='1' for r in rows)
        raw_measurements_count+=len(rows)
        assert (out/'query0.vo').stat().st_size==int(rows[0]['vo_bytes'])
        rows=[r for r in rows if QUERY_OFFSET<=int(r['query'])<QUERY_OFFSET+QUERY_COUNT]
        measurements_count+=len(rows)
        for r in rows:
            key=(path,int(r['k']),int(r['ef_search']),int(r['probes']))
            lookup.setdefault(key,[]).append(r)
        env=json.loads((out/'environment.json').read_text())
        for file,expected in env['source_sha256'].items():assert sha(ROOT/file)==expected,file
        if (out/'pilot_equivalence.json').exists():assert json.loads((out/'pilot_equivalence.json').read_text())['status']=='PASS'
    for rows in lookup.values():
        assert len(rows)==QUERY_COUNT*REPEATS
        assert {(int(r['query']),int(r['repeat'])) for r in rows}=={(q,t) for q in range(QUERY_OFFSET,QUERY_OFFSET+QUERY_COUNT) for t in range(3)}
    numeric=0;not_applicable=0
    for dataset,(name,n,d) in DATASETS.items():
        ws=wb[name]
        assert set(map(str,ws.merged_cells.ranges))==set(map(str,backup['Sheet1'].merged_cells.ranges))
        for row in sum(SIZE_ROWS.values(),[]):
            for col in [9,10]:assert ws.cell(row,col).value=='不适用';not_applicable+=1
        for addr in coordinates():
            c=ws[addr]
            if c.value=='不适用':continue
            assert isinstance(c.value,(int,float)) and math.isfinite(c.value),(name,addr,'pending')
            entry=source_rows[(name,addr)];_,_,value,metric,k,ef,probes,raw=entry
            assert math.isclose(c.value,value,rel_tol=1e-12)
            rows=lookup[(str(Path(raw).parent),k,ef,probes)]
            if metric=='recall_percent':actual=statistics.fmean(float(r['recall_at_k'])*100 for r in rows)
            elif metric=='query_ms':actual=statistics.fmean(float(r['search_ms'])+float(r['vo_generation_ms']) for r in rows)
            elif metric=='verify_ms':actual=statistics.fmean(float(r['verification_ms']) for r in rows)
            elif metric=='vo_kb':actual=statistics.fmean(int(r['vo_bytes'])/1000 for r in rows)
            else:raise AssertionError(metric)
            assert math.isclose(c.value,actual,rel_tol=1e-12,abs_tol=1e-12),(name,addr)
            assert str(Path(raw).parent) in c.comment.text
            numeric+=1
        for method,pairs in CURVE_ROWS.items():
            for rr,_ in pairs:
                values=[ws.cell(rr,col).value/100 for col in range(6,17)]
                assert all(value>=target-1e-12 for value,target in zip(values,TARGETS))
                assert values[-1]>=.99-1e-12
    assert numeric==873 and not_applicable==54 and len(source_rows)==873
    assert len(wb['新增数据召回曲线']._charts)==9
    assert sha(SOURCE)==sha(OUTPUT),'original workbook not updated'
    result=dict(status='PASS',numeric_cells=numeric,not_applicable_cells=not_applicable,
                experiment_records=len(record_files),unique_formal_runs=len(outputs),formal_measurements=measurements_count,
                raw_measurements_preserved=raw_measurements_count,query_count=QUERY_COUNT,repeats=REPEATS,scope="quick preview",tamper_rejections=tamper_count,original_updated=True,workbook_sha256=sha(OUTPUT),backup_sha256=sha(BACKUP))
    (STUDY/'final_validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

if __name__=='__main__':main()

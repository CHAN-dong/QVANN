"""Compare stronger candidate graphs before publishing the fixed-graph sweep."""
import csv,math,statistics,sys
from pathlib import Path
import work9_nytimes_annproof_full as run
u=run.u;S=run.S;O=run.O
assert not (S/'APPLIED.json').exists()
extra='--extra' in sys.argv
if (S/'GRAPH_SELECTED.json').exists() and not extra:raise SystemExit('Already selected')
r=u.read(S/'FINISHED.json');previous=r['curve_graph'];candidates=[]
def assess(ch,tag):
 valid=[p for p in ch['points'] if p['valid']]
 if min(p['recall'] for p in valid)>.425:return None
 good=[p for p in valid if p['recall']>=.95-1e-12]
 if not good:return None
 p=min(good,key=lambda p:p['ef'])
 # Search-only disclosure volume is the calibration objective. No timing from
 # these pilots is used as query/VO/verification measurement.
 paths=list((O/'pilots').glob(tag+'*/pilot.csv'));rows=[]
 for path in paths:
  rows.extend(x for x in csv.DictReader(path.open()) if int(x['ef_search'])==p['ef'])
 assert len(rows)==20,(tag,p['ef'],len(rows))
 score=statistics.fmean(int(x['visited']) for x in rows)*(1024+ch['M']*8+32)
 return dict(graph=ch,score=score,recall95=p,selection='minimum calibrated disclosure-volume proxy at >=95%, among fixed graphs reaching <=42.5% at the low end; timing/VO always remeasured')
if extra:candidates=u.read(S/'GRAPH_SELECTED.json')['candidates']
else:candidates.append(assess(previous,f'curve-M{previous["M"]}-efc{previous["efc"]}'))
grid=list(range(10,129))+list(range(136,513,8))+list(range(544,2049,32))+[3072,4096,6144,8192]
for m,efc in ([(12,12),(10,10)] if extra else [(4,100),(6,20)]):
 tag=f'candidate-M{m}-efc{efc}';cache=S/'cache'/tag;pp=run.pilot(tag,289761,m,efc,grid,cache)
 valid=[p for p in pp if p['valid']]
 if min(p['recall'] for p in valid)>.425:
  u.write(S/(tag+'-excluded.json'),dict(reason='smallest legal efSearch already exceeds 42.5% recall',minimum_recall=min(p['recall'] for p in valid)));continue
 if max(p['recall'] for p in valid)<.95:
  for e in [16384,32768,65536,131072,289761]:
   pp+=run.pilot(tag+'-extend-'+str(e),289761,m,efc,[e],cache)
   if any(p['valid'] and p['recall']>=.95 for p in pp):break
 ch=dict(M=m,efc=efc,cache=str(cache),points=pp)
 a=assess(ch,tag)
 if a:candidates.append(a)
best=min((c for c in candidates if c),key=lambda c:c['score']);ch=best['graph']
if (ch['M'],ch['efc'])!=(previous['M'],previous['efc']):
 pp=ch['points'];tag=f'candidate-M{ch["M"]}-efc{ch["efc"]}'
 if max(p['recall'] for p in pp if p['valid'])<.99:
  known={p['ef'] for p in pp}
  for e in [16384,32768,65536,131072,289761]:
   if e in known:continue
   pp+=run.pilot(tag+'-extend-'+str(e),289761,ch['M'],ch['efc'],[e],ch['cache'])
   if any(p['valid'] and p['recall']>=.99 for p in pp):break
 valid=[p for p in pp if p['valid']];selected=[]
 for target in run.TARGETS:
  good=[p for p in valid if p['recall']>=target-1e-12]
  selected.append(dict(target=target,choice=min(good,key=lambda p:p['ef']) if good else None))
 measured=run.measure(f'selected-fixed-graph-recall-M{ch["M"]}-efc{ch["efc"]}',289761,ch['M'],ch['efc'],ch['cache'],sorted({p['choice']['ef'] for p in selected if p['choice']}))
 u.write(S/'FINISHED-before-graph-selection.json',r)
 r.update(curve_graph=ch,curve_selection=selected,curve_measurements=measured)
 u.write(S/'FINISHED.json',r);u.write(S/'curve-choice.json',ch);u.write(S/'curve-selection.json',selected)
u.write(S/'GRAPH_SELECTED.json',dict(candidates=candidates,selected=best))
print('SELECTED_FIXED_GRAPH',ch['M'],ch['efc'],'score',best['score'],flush=True)

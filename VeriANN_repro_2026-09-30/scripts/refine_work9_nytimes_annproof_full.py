"""Tighten coarse high-budget recall gaps, keeping exactly the same graph."""
import work9_nytimes_annproof_full as run
u=run.u;S=run.S
assert not (S/'APPLIED.json').exists()
if not (S/'REFINED.json').exists():
 r=u.read(S/'FINISHED.json');ch=r['curve_graph'];pp=ch['points'];valid=[p for p in pp if p['valid']];grid=set();refine_targets=set()
 floor=min(p['recall'] for p in valid)
 for sel in r['curve_selection']:
  target=sel['target'];p=sel['choice']
  if not p or target<floor or p['recall']<=target+.025+1e-10 and abs(target-.60)>1e-10:continue
  refine_targets.add(target)
  lower=max((x['ef'] for x in valid if x['recall']<target),default=10);upper=p['ef']
  grid.update(lower+(upper-lower)*i//16 for i in range(1,16))
 grid-=set(p['ef'] for p in pp)
 if grid:pp+=run.pilot('curve-gap-refinement',289761,ch['M'],ch['efc'],sorted(grid),ch['cache'])
 valid=[p for p in pp if p['valid']];selected=[]
 previous={p['target']:p for p in r['curve_selection']}
 for target in run.TARGETS:
  if target not in refine_targets:selected.append(previous[target]);continue
  good=[p for p in valid if p['recall']>=target-1e-12]
  selected.append(dict(target=target,choice=min(good,key=lambda p:p['ef']) if good else None))
 have={p['ef'] for p in r['curve_measurements']};needed=sorted({x['choice']['ef'] for x in selected if x['choice']}-have)
 extra=run.measure('fixed-graph-recall-refinement',289761,ch['M'],ch['efc'],ch['cache'],needed) if needed else []
 r.update(curve_graph=ch,curve_selection=selected,curve_measurements=r['curve_measurements']+extra)
 u.write(S/'FINISHED-before-refinement.json',u.read(S/'FINISHED.json'))
 u.write(S/'FINISHED.json',r);u.write(S/'curve-selection.json',selected);u.write(S/'REFINED.json',dict(extra_efs=needed,selected=selected))
 print('REFINED',[(s['target'],s['choice']) for s in selected],flush=True)

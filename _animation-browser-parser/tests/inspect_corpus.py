"""Read-only real FBX discovery gate. No model conversion or clip baking."""
import argparse, collections, json, pathlib, subprocess, time

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--parser',required=True,type=pathlib.Path)
    p.add_argument('--source',required=True,type=pathlib.Path)
    p.add_argument('--output',required=True,type=pathlib.Path)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    started=time.perf_counter();records=[]
    for ix,source in enumerate(sorted(a.source.rglob('*.fbx'))):
        req=a.output/f'{ix:04d}.request.json';res=a.output/f'{ix:04d}.response.json'
        req.write_text(json.dumps(dict(version=1,operation='inspect',source=str(source.resolve()))),encoding='utf-8')
        tick=time.perf_counter();done=subprocess.run([str(a.parser.resolve()),str(req.resolve()),str(res.resolve())],capture_output=True,timeout=60)
        data=json.loads(res.read_text(encoding='utf-8')) if res.exists() else {}
        d=data.get('data',{});clips=d.get('clips',[])
        record=dict(source=str(source),exitCode=done.returncode,ok=data.get('ok',False),seconds=time.perf_counter()-tick,meshCount=d.get('meshCount'),jointCount=sum(n['joint'] for n in d.get('nodes',[])),nodeCount=len(d.get('nodes',[])),clipCount=len(clips),trackCounts=[len(c['tracks']) for c in clips],durations=[c['duration'] for c in clips],fps=[c['fps'] for c in clips],warnings=d.get('warnings',[]))
        if not record['ok']:record['error']=data.get('error',done.stderr.decode('utf-8','replace'))
        records.append(record)
    report=dict(ok=all(r['ok'] for r in records),fileCount=len(records),seconds=time.perf_counter()-started,meshCounts=dict(collections.Counter(r['meshCount'] for r in records)),playableClipCount=sum(r['clipCount'] for r in records),records=records)
    (a.output/'summary.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='records'},ensure_ascii=False))
    return 0 if report['ok'] else 1

if __name__=='__main__':raise SystemExit(main())

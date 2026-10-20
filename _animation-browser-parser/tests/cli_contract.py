"""Process-level source safety, Unicode, coordinate selection and clip contract gates."""
import argparse, hashlib, json, pathlib, subprocess, tempfile, shutil, struct

def main():
    p=argparse.ArgumentParser();p.add_argument('--parser',required=True,type=pathlib.Path);p.add_argument('--source',required=True,type=pathlib.Path);p.add_argument('--animation',required=True,type=pathlib.Path);p.add_argument('--output',required=True,type=pathlib.Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    records=[]
    def call(label,req,response=None):
        request=a.output/(label+'.request.json');response=response or a.output/(label+'.response.json')
        request.write_text(json.dumps(req,ensure_ascii=False),encoding='utf-8')
        run=subprocess.run([str(a.parser.resolve()),str(request.resolve()),str(response.resolve())],capture_output=True,timeout=60)
        data=json.loads(response.read_text(encoding='utf-8')) if response.exists() and response.suffix=='.json' else None
        records.append(dict(label=label,exitCode=run.returncode,ok=data.get('ok') if data else None,error=data.get('error') if data else run.stderr.decode('utf-8','replace')))
        return run,data
    base=dict(version=1,operation='inspect',source=str(a.source.resolve()))
    _,auto=call('auto',base);_,maya=call('maya',dict(base,profile=dict(interpretation='maya')))
    assert auto['ok'] and maya['ok'];assert [n['rest'] for n in auto['data']['nodes']]==[n['rest'] for n in maya['data']['nodes']]
    assert auto['data']['coordinates']['selected']['sourceKind']=='detected';assert maya['data']['coordinates']['selected']['sourceKind']=='explicit'
    _,maximum=call('max',dict(base,profile=dict(interpretation='max')));_,custom=call('custom-max',dict(base,profile=dict(interpretation='custom',sourceAxes=dict(right='+X',up='+Z',front='-Y'))))
    assert maximum['ok'] and custom['ok'];assert [n['rest'] for n in maximum['data']['nodes']]==[n['rest'] for n in custom['data']['nodes']]
    assert maximum['data']['coordinates']['detected']==auto['data']['coordinates']['detected'];assert maximum['data']['coordinates']['selected']['axes']['up']=='+Z'
    for label,profile in [('invalid-unit',dict(sourceUnitMeters=0)),('invalid-axes',dict(interpretation='custom',sourceAxes=dict(right='+X',up='+X',front='+Z')))]:
        run,res=call(label,dict(base,profile=profile));assert run.returncode!=0 and res['error']['code']=='INVALID_PROFILE'
    out=(a.output/'selected-clip.clipb').resolve();run,res=call('clip',dict(version=1,operation='clip',source=str(a.animation.resolve()),clipIndex=0,output=str(out)))
    assert run.returncode==0 and res['ok'];raw=out.read_bytes();assert raw[:8]==b'BFCLIP1\0';json_bytes,data_bytes=struct.unpack_from('<II',raw,8);body=16+(json_bytes+7)//8*8;assert body+data_bytes==len(raw);clip=json.loads(raw[16:16+json_bytes]);
    for track in clip['tracks']:
        for field in ('times','values'):
            descriptor=track[field];assert descriptor['offset']>=0 and descriptor['count']>=0 and (descriptor['offset']+descriptor['count'])*8<=data_bytes;track[field]=list(struct.unpack_from('<'+str(descriptor['count'])+'d',raw,body+descriptor['offset']*8))
    assert res['data']['format']=='billfish-clip-f64-1' and res['data']['trackCount']==len(clip['tracks']);assert clip['fps']==30 and clip['samplingRate']==120
    for tr in clip['tracks']:
        assert tr['times'][0]==0 and tr['times'][-1]==clip['duration'];assert all(x<y for x,y in zip(tr['times'],tr['times'][1:]));assert len(tr['values'])==len(tr['times'])*(4 if tr['property']=='rotation' else 3)
    with tempfile.TemporaryDirectory(prefix='billfish-parser-') as td:
        source_dir=pathlib.Path(td)/'source';source_dir.mkdir();unicode_source=source_dir/'角色 模型.fbx';shutil.copyfile(a.source,unicode_source);run,res=call('unicode-valid',dict(base,source=str(unicode_source)));assert run.returncode==0 and res['data']['hasSkinnedMeshes'];bad=source_dir/'坏模型.fbx';bad.write_bytes(b'Not an FBX file\x00\xff');before=hashlib.sha256(bad.read_bytes()).hexdigest()
        run,res=call('unicode-malformed',dict(base,source=str(bad)));assert run.returncode!=0 and res['error']['code']=='FBX_PARSE_FAILED'
        run,res=call('reject-source-output',dict(base,operation='model',source=str(bad),output=str(bad)));assert run.returncode!=0 and res['error']['code']=='UNSAFE_OUTPUT_PATH'
        run,_=call('reject-source-response',dict(base,source=str(bad)),response=bad);assert run.returncode!=0;assert hashlib.sha256(bad.read_bytes()).hexdigest()==before
        assert set(source_dir.iterdir())=={bad,unicode_source}
    report=dict(ok=True,parser=str(a.parser.resolve()),cases=records,clipTrackCount=len(clip['tracks']),sourceFps=clip['fps'],samplingRate=clip['samplingRate'],coordinates=dict(auto=auto['data']['coordinates'],max=maximum['data']['coordinates']))
    (a.output/'summary.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(dict(ok=True,cases=len(records),clipTrackCount=len(clip['tracks']))));return 0
if __name__=='__main__':raise SystemExit(main())

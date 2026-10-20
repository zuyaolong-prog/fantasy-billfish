"""Independent binary glTF bind-pose gate for the supplied real FBX fixture.

Reads the actual emitted POSITION/JOINTS/WEIGHTS/IBM buffers with NumPy; it does
not call the importer implementation or reuse its in-memory mesh/skin objects.
The fixture's authored default is its bind pose. This invariant is asserted,
not assumed for arbitrary FBX files with authored non-bind default poses.
"""
import argparse, json, pathlib, struct, zlib
import numpy as np

def raw_vertices(path):
    data=path.read_bytes();version=struct.unpack_from('<I',data,23)[0]
    fmt,head=('<QQQB',25) if version>=7500 else ('<IIIB',13)
    found=[]
    def node(pos):
        end,count,_,name_length=struct.unpack_from(fmt,data,pos);pos+=head
        if not end:return pos
        name=data[pos:pos+name_length].decode();pos+=name_length
        for _ in range(count):
            kind=chr(data[pos]);pos+=1
            if kind in 'YCFDIL':pos+=struct.calcsize('<'+dict(Y='h',C='?',F='f',D='d',I='i',L='q')[kind])
            elif kind in 'SR':size=struct.unpack_from('<I',data,pos)[0];pos+=4+size
            else:
                count,encoding,size=struct.unpack_from('<III',data,pos);pos+=12
                if name=='Vertices':
                    raw=data[pos:pos+size];raw=zlib.decompress(raw) if encoding else raw
                    found.append(np.frombuffer(raw,dtype='<f8' if kind=='d' else '<f4').reshape(-1,3))
                pos+=size
        while pos<end-head:
            if not struct.unpack_from('<Q' if version>=7500 else '<I',data,pos)[0]:break
            pos=node(pos)
        return end
    pos=27
    while struct.unpack_from('<Q' if version>=7500 else '<I',data,pos)[0]:pos=node(pos)
    assert len(found)==1,'This real fixture gate expects one raw mesh'
    return found[0]

def main():
    p=argparse.ArgumentParser();p.add_argument('fbx',type=pathlib.Path);p.add_argument('glb',type=pathlib.Path);p.add_argument('report',type=pathlib.Path);a=p.parse_args()
    data=a.glb.read_bytes();length=struct.unpack_from('<I',data,12)[0];j=json.loads(data[20:20+length]);binary=data[28+length:]
    def access(index):
        ac=j['accessors'][index];view=j['bufferViews'][ac['bufferView']];lanes=dict(SCALAR=1,VEC2=2,VEC3=3,VEC4=4,MAT4=16)[ac['type']]
        return np.frombuffer(binary,dtype={5126:'<f4',5123:'<u2',5125:'<u4'}[ac['componentType']],count=ac['count']*lanes,offset=view.get('byteOffset',0)+ac.get('byteOffset',0)).reshape(ac['count'],lanes)
    def local(n):
        x,y,z,w=n['rotation'];rot=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]);m=np.eye(4);m[:3,:3]=rot@np.diag(n['scale']);m[:3,3]=n['translation'];return m
    world={}
    def visit(index,parent):
        world[index]=parent@local(j['nodes'][index])
        for child in j['nodes'][index].get('children',[]):visit(child,world[index])
    for root in j['scenes'][j['scene']]['nodes']:visit(root,np.eye(4))
    raw=raw_vertices(a.fbx);source_positions={tuple(row) for row in raw.astype(np.float32)}
    maximum_vertex_error=maximum_matrix_error=0.;vertex_count=0;weight_error=0.;unknown_positions=0
    lo=np.full(3,np.inf);hi=-lo
    for node_index,node in enumerate(j['nodes']):
        if 'mesh' not in node:continue
        skin=j['skins'][node['skin']];inverse=access(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
        matrices=np.array([world[ix]@inverse[k] for k,ix in enumerate(skin['joints'])])
        maximum_matrix_error=max(maximum_matrix_error,float(np.max(abs(matrices-world[node_index]))))
        for primitive in j['meshes'][node['mesh']]['primitives']:
            attrs=primitive['attributes'];positions=access(attrs['POSITION']);joints=access(attrs['JOINTS_0']);weights=access(attrs['WEIGHTS_0']);homogeneous=np.column_stack((positions,np.ones(len(positions))));skinned=np.zeros((len(positions),4))
            for k in range(4):skinned+=np.einsum('nij,nj->ni',matrices[joints[:,k]],homogeneous)*weights[:,k,None]
            expected=homogeneous@world[node_index].T
            maximum_vertex_error=max(maximum_vertex_error,float(np.max(abs(skinned-expected))))
            weight_error=max(weight_error,float(np.max(abs(weights.sum(1)-1))))
            unknown_positions+=sum(tuple(row) not in source_positions for row in positions)
            lo=np.minimum(lo,skinned[:,:3].min(0));hi=np.maximum(hi,skinned[:,:3].max(0));vertex_count+=len(positions)
    ok=maximum_vertex_error<0.0001 and maximum_matrix_error<0.0001 and weight_error<1e-6 and unknown_positions==0
    report=dict(ok=ok,source=str(a.fbx),glb=str(a.glb),rawVertexCount=len(raw),rawBounds=[raw.min(0).tolist(),raw.max(0).tolist()],renderVertexCount=vertex_count,renderBoundsMeters=[lo.tolist(),hi.tolist()],unknownExportedPositions=unknown_positions,maxWeightSumError=weight_error,maxBindMatrixError=maximum_matrix_error,maxSkinnedVertexErrorMeters=maximum_vertex_error,method='Independent raw FBX arrays plus emitted GLB binary accessors, NumPy hierarchy and linear skinning')
    a.report.write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report));return 0 if ok else 1
if __name__=='__main__':raise SystemExit(main())

// Independent tiny encoder for UI fixtures; production encoding belongs to backend.
export function encodeClipFixture(clip){
  let count=0;const arrays=[];const range=values=>{const result={offset:count,count:values.length};arrays.push(values);count+=values.length;return result;};
  const metadata={...clip,tracks:clip.tracks.map(track=>({...track,times:range(track.times),values:range(track.values)}))},text=new TextEncoder().encode(JSON.stringify(metadata)),start=Math.ceil((16+text.length)/8)*8,bytes=new Uint8Array(start+count*8),view=new DataView(bytes.buffer);bytes.set([66,70,67,76,73,80,49,0]);view.setUint32(8,text.length,true);view.setUint32(12,count*8,true);bytes.set(text,16);let offset=start;for(const values of arrays)for(const value of values){view.setFloat64(offset,value,true);offset+=8;}return bytes;
}

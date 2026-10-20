'use strict';
const MAGIC=Buffer.from('BFCLIP1\0'),HEADER=16;
const numeric=value=>Array.isArray(value)||value instanceof Float64Array;
function encodeClip(clip){
 const parts=[],ranges=new Map();let count=0;
 const range=value=>{if(!numeric(value))throw new Error('Clip samples must be numeric arrays');if(ranges.has(value))return ranges.get(value);const values=value instanceof Float64Array?value:Float64Array.from(value),result={offset:count,count:values.length};count+=values.length;ranges.set(value,result);parts.push(Buffer.from(values.buffer,values.byteOffset,values.byteLength));return result;};
 const metadata={...clip,tracks:clip.tracks.map(track=>({...track,times:range(track.times),values:range(track.values)}))},json=Buffer.from(JSON.stringify(metadata)),padding=(8-json.length%8)%8,bytes=count*8;
 if(json.length>0xffffffff||bytes>0xffffffff)throw new Error('Binary clip exceeds its bounded representation');
 const header=Buffer.alloc(HEADER);MAGIC.copy(header);header.writeUInt32LE(json.length,8);header.writeUInt32LE(bytes,12);return Buffer.concat([header,json,Buffer.alloc(padding),...parts]);
}
function decodeClip(input){
 let bytes=Buffer.isBuffer(input)?input:Buffer.from(input.buffer,input.byteOffset,input.byteLength);
 if(bytes.length<HEADER||!bytes.subarray(0,8).equals(MAGIC))throw new Error('Invalid binary clip header');
 const jsonLength=bytes.readUInt32LE(8),dataLength=bytes.readUInt32LE(12),body=HEADER+Math.ceil(jsonLength/8)*8;
 if(body>bytes.length||dataLength%8||body+dataLength!==bytes.length)throw new Error('Invalid binary clip lengths');
 const metadata=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes.subarray(HEADER,HEADER+jsonLength)));if(metadata?.version!==1||!Array.isArray(metadata.tracks))throw new Error('Invalid binary clip metadata');
 // Files normally have aligned Buffer storage. Keep arbitrary Buffer slices valid too.
 if((bytes.byteOffset+body)%8)bytes=Buffer.from(Uint8Array.from(bytes).buffer);
 const views=new Map();const range=value=>{if(!value||!Number.isSafeInteger(value.offset)||!Number.isSafeInteger(value.count)||value.offset<0||value.count<0||value.offset>dataLength/8||value.count>dataLength/8-value.offset)throw new Error('Invalid binary clip sample range');const key=value.offset+':'+value.count;if(!views.has(key))views.set(key,new Float64Array(bytes.buffer,bytes.byteOffset+body+value.offset*8,value.count));return views.get(key);};
 return {...metadata,tracks:metadata.tracks.map(track=>({...track,times:range(track.times),values:range(track.values)}))};
}
module.exports={encodeClip,decodeClip};

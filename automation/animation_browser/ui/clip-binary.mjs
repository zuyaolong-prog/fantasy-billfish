const magic=[66,70,67,76,73,80,49,0];
const littleEndian=new Uint8Array(new Uint16Array([1]).buffer)[0]===1;

export function decodeClipBinary(bytes){
  if(!(bytes instanceof Uint8Array)||bytes.byteLength<16)throw new Error('动画二进制数据缺失或长度无效');
  if(magic.some((value,index)=>bytes[index]!==value))throw new Error('动画二进制版本无效');
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),jsonLength=view.getUint32(8,true),dataLength=view.getUint32(12,true),start=Math.ceil((16+jsonLength)/8)*8;
  if(dataLength%8||start>bytes.byteLength||start+dataLength!==bytes.byteLength)throw new Error('动画二进制数据边界无效');
  let clip;try{clip=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes.subarray(16,16+jsonLength)));}catch{throw new Error('动画二进制元数据无效');}
  if(!clip||clip.version!==1||!Array.isArray(clip.tracks))throw new Error('动画二进制轨道信息无效');
  let body=bytes.subarray(start);if(body.byteOffset%8)body=body.slice();
  const count=dataLength/8;
  function numbers(range){
    if(!range||typeof range!=='object'||Array.isArray(range)||!Number.isSafeInteger(range.offset)||!Number.isSafeInteger(range.count)||range.offset<0||range.count<0||range.offset>count||range.count>count-range.offset)throw new Error('动画二进制轨道范围无效');
    if(littleEndian)return new Float64Array(body.buffer,body.byteOffset+range.offset*8,range.count);
    const result=new Float64Array(range.count),source=new DataView(body.buffer,body.byteOffset,body.byteLength);for(let index=0;index<result.length;index++)result[index]=source.getFloat64((range.offset+index)*8,true);return result;
  }
  return {...clip,tracks:clip.tracks.map(track=>{if(!track||typeof track!=='object'||Array.isArray(track))throw new Error('动画二进制轨道无效');return {...track,times:numbers(track.times),values:numbers(track.values)};})};
}

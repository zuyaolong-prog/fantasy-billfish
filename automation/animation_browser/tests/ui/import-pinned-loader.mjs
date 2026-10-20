// Node has no browser import maps. Only rewrite module specifiers in-memory;
// the shipped GLTFLoader/BufferGeometryUtils implementation stays unchanged.
import fs from 'node:fs/promises';
const dataURL=source=>'data:text/javascript;base64,'+Buffer.from(source).toString('base64');
export async function pinnedLoader(){
 const three=new URL('../../vendor/three/build/three.module.js',import.meta.url).href;
 const utils=await fs.readFile(new URL('../../vendor/three/addons/utils/BufferGeometryUtils.js',import.meta.url),'utf8');
 const loader=await fs.readFile(new URL('../../vendor/three/addons/loaders/GLTFLoader.js',import.meta.url),'utf8');
 const utilsURL=dataURL(utils.replace("from 'three'",`from '${three}'`));
 return import(dataURL(loader.replace("from 'three'",`from '${three}'`).replace("from '../utils/BufferGeometryUtils.js'",`from '${utilsURL}'`)));
}

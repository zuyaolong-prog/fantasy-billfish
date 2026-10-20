'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const root = __dirname;
const source = process.argv[2] || 'C:/Users/Administrator/AppData/Local/Microsoft/Edge/User Data/Default/Extensions/kjjmbhmmbfgbaannpbpoaabponeogaop/4.0.5_1';
if(path.resolve(source).toLowerCase()===root.toLowerCase()) throw new Error('Source must be the original extension, not the maintained output.');
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const identity = JSON.parse(fs.readFileSync(path.join(root, 'identity.json'), 'utf8'));
const hashes = {};
function copyTree(from, to, relative = '') {
  fs.mkdirSync(to, {recursive:true});
  for (const entry of fs.readdirSync(from, {withFileTypes:true})) {
    if (!relative && entry.name === '_metadata') continue;
    const rel = path.posix.join(relative, entry.name);
    if (entry.isDirectory()) copyTree(path.join(from,entry.name), path.join(to,entry.name), rel);
    else {
      const bytes = fs.readFileSync(path.join(from,entry.name));
      hashes[rel] = sha(bytes);
      fs.writeFileSync(path.join(to,entry.name), bytes);
    }
  }
}
copyTree(source,root);
const patches = [];
function patch(file, before, after, expected = 1) {
  const name = path.join(root,file);
  const text = fs.readFileSync(name,'utf8');
  const count = text.split(before).length-1;
  if (count !== expected) throw new Error(`${file}: expected ${expected} anchors, found ${count}: ${before}`);
  fs.writeFileSync(name,text.split(before).join(after));
  patches.push({file,before,after,count});
}
// Preserve vendor layout, folder selection, image extraction, HTTP transport and image feedback.
patch('content.js','"dragstart","img, a"','"dragstart","img, a, video"',2);
patch('content.js','this.onDragStart=async e=>{var t;if(!(await this.backCaller.call(Z.GetHostIncludes,{host:location.host})).output&&(',
  'this.onDragStart=async e=>{var t;const v=globalThis.BillfishCaptureVideo.fromElement(e.target,location.href,document.title),p=this.captureDrag,d={candidate:v,policy:null,finalized:!1};this.captureDrag=d,this.captureVideoCandidate=null,this.canDrag=!1,this.collectImgSrc="",this.collectTitle="",this.screenshotArea=!1,this.chose=void 0,this.otherFile=!1;p&&this.removeDom(p.frame);d.policy=v?globalThis.BillfishCaptureVideo.checkPolicy(this.backCaller.call(Z.GetHostIncludes,{host:location.host})):null;if(!(v?false:(await this.backCaller.call(Z.GetHostIncludes,{host:location.host})).output)&&(');
patch('content.js','try{if(this.settings.dropType===E.disableDrop)return!1;',
  'try{if(this.captureDrag!==d)return;if(this.settings.dropType===E.disableDrop)return!1;');
patch('content.js','if(document.getElementById("show-scoll-mouse-top")){Math.abs',
  'if(this.captureDrag===d&&document.getElementById("show-scoll-mouse-top")){Math.abs');
patch('content.js','const n=this.dragTransfer.transfer(e);let i=null==n?void 0:n.src;',
  'this.captureVideoCandidate=v;v&&(this.chose=void 0,this.otherFile=!1,this.screenshotArea=!1);const n=this.captureVideoCandidate||this.dragTransfer.transfer(e);let i=null==n?void 0:n.src;');
patch('content.js','this.canDrag=!0,this.scollShow(),this.timers=',
  'this.canDrag=!0,this.scollShow(),d.frame=document.getElementById("newSocllMouse"),this.captureVideoCandidate&&globalThis.BillfishCaptureVideoUi.decorate(d.frame),this.timers=');
patch('content.js','this.timers=setTimeout((()=>{const e=document.getElementById("maskBillfishDrag");',
  'this.timers=setTimeout((()=>{if(this.captureDrag!==d)return;const e=document.getElementById("maskBillfishDrag");');
patch('content.js','await this.getFile()}}),100);','this.captureVideoCandidate||await this.getFile()}}),100);');
patch('content.js','this.collectTitle=null==n?void 0:n.alt',
  'this.collectTitle=this.captureVideoCandidate?this.captureVideoCandidate.title:null==n?void 0:n.alt');
patch('content.js','async setMessage(e){try{const t=this.collectImgSrc,',
  'async setMessage(e){try{const d=this.captureDrag;if(d&&d.candidate)return globalThis.BillfishCaptureVideo.finalizeDrag(d,this.screenshotArea,{collectReffer:this.collectReffer},()=>{if(this.captureDrag===d){this.captureDrag=null,this.captureVideoCandidate=null,this.canDrag=!1,this.collectImgSrc="",this.collectTitle="",this.screenshotArea=!1,this.chose=void 0,this.otherFile=!1,this.removeDom(d.frame),document.documentElement.style.overflow=""}},e=>this.backCaller.call(Z.SendSingleCollect,e),globalThis.BillfishCaptureVideoNotice);const t=this.collectImgSrc,');
patch('content.js','const{output:i}=await this.backCaller.call(Z.GetBillfishStatus);i||this.onShowOPenBillfish(),ie(i)?',
  'const{output:i}=await this.backCaller.call(Z.GetBillfishStatus);if(this.captureDrag!==d)return;i||this.onShowOPenBillfish(),ie(i)?');
patch('content.js','getBackgroundImage(e){var t;',
  'getBackgroundImage(e){const v=globalThis.BillfishCaptureVideo.fromElement(e,location.href,document.title);if(v)return v;var t;');
patch('background.js','async directSendImage(e,t){if("image"===e.contentType)',
  'async directSendImage(e,t){if("video"===e.contentType)return globalThis.BillfishCaptureNative.capture(e);if("image"===e.contentType)');
patch('background.js','files:["content.js"]','files:["maintained/video-common.js","maintained/video-content.js","content.js"]');
patch('background.js','vr.listenNotify(_),wn.init(),',
  'vr.listenNotify(_),globalThis.BillfishCaptureNative.setPolicyReader(async e=>{const t=(await zr.get(Vr.DragHostList)||"").includes(e),r=wn.getSitePolicies.disableDarg;if(!Array.isArray(r))return{allowed:!1,code:"site_policy_unavailable",message:"无法确认当前网站的采集设置, 请稍后重试."};const n=r.find(t=>e.includes(t));return t?{allowed:!1,code:"user_site_disabled",message:"你已禁用当前网站的采集."}:n?{allowed:!1,code:"vendor_site_disabled",message:"当前网站在采集器的网站禁用列表中."}:{allowed:!0,code:"allowed",message:""}}),wn.init(),');
const backgroundPath=path.join(root,'background.js');
fs.writeFileSync(backgroundPath,'importScripts("maintained/video-background.js");\n'+fs.readFileSync(backgroundPath,'utf8'));
patches.push({file:'background.js',scope:'prefix',after:'importScripts("maintained/video-background.js");',count:1});
const manifest=JSON.parse(fs.readFileSync(path.join(root,'manifest.json'),'utf8'));
manifest.name=identity.name;
manifest.version=identity.version;
manifest.key=identity.key;
delete manifest.update_url;
manifest.permissions=[...new Set([...manifest.permissions,'nativeMessaging'])];
manifest.content_scripts[0].js=['./maintained/video-common.js','./maintained/video-content.js',...manifest.content_scripts[0].js];
fs.writeFileSync(path.join(root,'manifest.json'),JSON.stringify(manifest,null,2)+'\n');
const provenance={source_path:source,source_extension_id:identity.source_extension_id,source_version:identity.source_version,maintained_version:identity.version,source_hashes:hashes,patches,notes:['Signed _metadata excluded from unpacked fork.','Only public RSA key retained; extension ID is stable.','Video uses nativeMessaging; existing image HTTP path retained.']};
fs.writeFileSync(path.join(root,'provenance.json'),JSON.stringify(provenance,null,2)+'\n');
console.log(JSON.stringify({path:root,extension_id:identity.extension_id,version:identity.version,files:Object.keys(hashes).length,patches:patches.length}));

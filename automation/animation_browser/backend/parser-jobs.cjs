'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),{spawn}=require('node:child_process');
class ParserJobs {
 constructor(executable,root,concurrency=2){this.executable=executable;this.root=root;this.concurrency=Math.max(1,Math.min(4,concurrency));this.active=0;this.pending=[];this.waiters=[];this.stats={inspect:0,model:0,clip:0,failures:0};}
 run(request,signal){return new Promise((resolve,reject)=>{const task={request,signal,resolve,reject};this.pending.push(task);this.pump();});}
 pump(){while(this.active<this.concurrency&&this.pending.length){const t=this.pending.shift();if(t.signal?.aborted){t.reject(aborted());continue;}this.active++;this.execute(t).then(t.resolve,t.reject).finally(()=>{this.active--;this.pump();});}if(!this.active&&!this.pending.length)for(const done of this.waiters.splice(0))done();}
 idle(){if(!this.active&&!this.pending.length)return Promise.resolve();return new Promise(resolve=>this.waiters.push(resolve));}
 async execute({request,signal}) {
  await fs.mkdir(this.root,{recursive:true});const id=crypto.randomUUID(),req=path.join(this.root,id+'.request.json'),res=path.join(this.root,id+'.response.json');
  await fs.writeFile(req,JSON.stringify({version:1,...request}));this.stats[request.operation]++;
  try{return await new Promise((resolve,reject)=>{
   if(signal?.aborted){reject(aborted());return;}
   const child=spawn(this.executable,[req,res],{windowsHide:true,stdio:['ignore','ignore','pipe']});let diagnostic='';child.stderr.on('data',b=>{diagnostic=(diagnostic+b).slice(-8192);});
   const cancel=()=>child.kill();signal?.addEventListener('abort',cancel,{once:true});
   child.once('error',reject);child.once('close',async code=>{signal?.removeEventListener('abort',cancel);if(signal?.aborted){reject(aborted());return;}try{const result=JSON.parse(await fs.readFile(res,'utf8'));if(code!==0||!result.ok){const e=new Error(result.error?.message||diagnostic||'Parser failed');e.code=result.error?.code||'PARSER_FAILED';e.details=result.error?.details;throw e;}resolve({...result.data,parserVersion:result.parserVersion});}catch(e){this.stats.failures++;reject(e);}});
  });}finally{await Promise.all([req,res].map(p=>fs.unlink(p).catch(()=>{})));}
 }
}
function aborted(){const e=new Error('Operation cancelled');e.code='ABORT_ERR';return e;}
module.exports={ParserJobs,aborted};

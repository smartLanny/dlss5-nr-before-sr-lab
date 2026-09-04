'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {appError}=require('./errors');
function safePath(root,rel){
 if(typeof rel!=='string'||!rel||path.isAbsolute(rel)||rel.includes(':')||rel.includes('\0')||rel.split(/[\\/]/).some(x=>x==='..'))throw appError('ERR_BACKUP_INVALID');
 const base=path.resolve(root),target=path.resolve(base,rel),r=path.relative(base,target);
 if(!r||r==='..'||r.startsWith('..'+path.sep)||path.isAbsolute(r))throw appError('ERR_BACKUP_INVALID');
 // Check the complete ancestor chain, including ancestors of the chosen root.
 for(let p=target;;p=path.dirname(p)){try{if(fs.lstatSync(p).isSymbolicLink())throw appError('ERR_BACKUP_INVALID');}catch(e){if(e.code!=='ENOENT')throw e;}if(path.dirname(p)===p)break;}
 return target;
}
function hash(bytes){return crypto.createHash('sha256').update(bytes).digest('hex');}
function readBounded(file,max=2*1024*1024){const s=fs.statSync(file);if(!s.isFile()||s.size>max)throw appError('ERR_BAD_REQUEST');return fs.readFileSync(file);}
async function atomicWrite(file,bytes){
 const tmp=file+'.zjz-'+crypto.randomUUID()+'.tmp';await fs.promises.mkdir(path.dirname(file),{recursive:true});
 try{await fs.promises.writeFile(tmp,bytes,{flag:'wx'});await fs.promises.rename(tmp,file);}finally{await fs.promises.unlink(tmp).catch(e=>{if(e.code!=='ENOENT')throw e;});}
}
module.exports={safePath,hash,readBounded,atomicWrite};

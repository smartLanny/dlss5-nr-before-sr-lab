'use strict';
// Product-owned write-ahead journal; inspired by the MIT DLSS5-Swapper journal.
// Does not share its pending-switch.json or perform recursive backup deletion.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {safePath,hash,readBounded,atomicWrite}=require('./safe-files');const {appError}=require('./errors');
const PENDING='_DLSS5_Backup/xiaofeng-pending.json';const busy=new Set();
const json=(file,value)=>atomicWrite(file,JSON.stringify(value,null,2));
function validate(root,j){
 if(!j||j.version!==1||!/^[a-f0-9-]{36}$/.test(j.id)||!Array.isArray(j.files)||j.files.length>32)throw appError('ERR_BACKUP_INVALID');
 for(let i=0;i<j.files.length;i++){const f=j.files[i];safePath(root,f.rel);if(typeof f.existed!=='boolean'||f.snapshot!==`_DLSS5_Backup/xiaofeng-tx/${j.id}/${i}.bin`)throw appError('ERR_BACKUP_INVALID');
  if(f.existed){const b=readBounded(safePath(root,f.snapshot),1024*1024*1024);if(hash(b)!==f.sha256)throw appError('ERR_BACKUP_INVALID');}}
}
async function recover(root){
 const pending=safePath(root,PENDING);if(!fs.existsSync(pending))return false;const j=JSON.parse(readBounded(pending).toString('utf8'));validate(root,j);
 for(const f of [...j.files].reverse()){const target=safePath(root,f.rel);if(f.existed)await atomicWrite(target,readBounded(safePath(root,f.snapshot),1024*1024*1024));else if(fs.existsSync(target))await fs.promises.unlink(target);}
 await fs.promises.unlink(pending);return true;
}
async function transaction(root,work){
 const key=path.resolve(root).toLowerCase();if(busy.has(key))throw appError('ERR_JOB_BUSY');busy.add(key);
 const pending=safePath(root,PENDING);
 try{
  if(fs.existsSync(pending))throw appError('ERR_RECOVERY_REQUIRED');
  const j={version:1,id:crypto.randomUUID(),files:[]};await json(pending,j);
  const tx={async capture(target){const rel=path.relative(root,target);safePath(root,rel);if(j.files.some(f=>f.rel.toLowerCase()===rel.toLowerCase()))return;
   const existed=fs.existsSync(target),snapshot=`_DLSS5_Backup/xiaofeng-tx/${j.id}/${j.files.length}.bin`;let sha256=null;
   if(existed){const b=readBounded(target,1024*1024*1024);sha256=hash(b);await atomicWrite(safePath(root,snapshot),b);}
   j.files.push({rel,existed,snapshot,sha256});await json(pending,j);
  }};
  try{const result=await work(tx);await fs.promises.unlink(pending);return result;}
  catch(cause){try{await recover(root);}catch(recoveryError){throw appError('ERR_RECOVERY_REQUIRED',{cause:cause.message,recoveryError:recoveryError.message});}throw cause;}
 }finally{busy.delete(key);}
}
module.exports={transaction,recover,PENDING};

'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {PAYLOAD_FILES}=require('./constants');const {appError}=require('./errors');const {safePath,readBounded}=require('./safe-files');
const required=['reshade','addon','bridge','runtime','config'];
function sha256(file){const fd=fs.openSync(file,'r');try{const s=fs.fstatSync(fd);if(!s.isFile()||s.size>1024**3)throw appError('ERR_PAYLOAD_HASH');const h=crypto.createHash('sha256'),b=Buffer.alloc(1024*1024);let n;while((n=fs.readSync(fd,b,0,b.length,null))>0)h.update(b.subarray(0,n));return h.digest('hex');}finally{fs.closeSync(fd);}}
function payloadRoot(base){return path.join(base,'payload','nr-before-sr');}
function readBundle(dir){const f=safePath(dir,'bundle.json');if(!fs.existsSync(f))throw appError('ERR_PAYLOAD_MISSING');let b;try{b=JSON.parse(readBounded(f,65536));}catch{throw appError('ERR_PAYLOAD_HASH');}
 if(!b||![1,2].includes(b.version)||!b.files||Array.isArray(b.files))throw appError('ERR_PAYLOAD_HASH');
 const valid=new Set(Object.values(PAYLOAD_FILES));for(const[k,v]of Object.entries(b.files))if(!valid.has(k)||typeof v!=='string'||! /^[a-f0-9]{64}$/i.test(v))throw appError('ERR_PAYLOAD_HASH');
 if(b.files[PAYLOAD_FILES.quickbar]&&b.coreControlApi!==1)throw appError('ERR_PAYLOAD_HASH',{reason:'Quickbar requires a control-API core'});return b;}
function inspectPayload(dir,options={}){let bundle=null,error=null;try{bundle=readBundle(dir);}catch(e){if(!options.allowMissingBundle)throw e;error=e.code;}
 const files=Object.entries(PAYLOAD_FILES).filter(([k,n])=>required.includes(k)||bundle?.files[n]).map(([kind,name])=>{
 const file=safePath(dir,name),exists=fs.existsSync(file)&&fs.statSync(file).isFile(),expected=bundle?.files[name]?.toLowerCase()||null,actual=exists?sha256(file):null;
 return{kind,name,file,exists,expected,actual,valid:exists&&Boolean(expected)&&actual===expected};});
 return{dir,bundle,error,files,ready:Boolean(bundle)&&files.every(f=>f.valid),missing:files.filter(f=>!f.exists).map(f=>f.name),invalid:files.filter(f=>f.exists&&!f.valid).map(f=>f.name)};}
function requirePayload(dir){const s=inspectPayload(dir);if(s.missing.length)throw appError('ERR_PAYLOAD_MISSING',{files:s.missing});if(!s.ready)throw appError('ERR_PAYLOAD_HASH',{files:s.invalid});return Object.fromEntries(s.files.map(r=>[r.kind,r]));}
function createBundle(dir){const files={};for(const[k,name]of Object.entries(PAYLOAD_FILES)){const f=safePath(dir,name);if(!fs.existsSync(f)){if(required.includes(k))throw appError('ERR_PAYLOAD_MISSING',{file:name});continue;}files[name]=sha256(f);}return{version:2,coreControlApi:files[PAYLOAD_FILES.quickbar]?1:0,generatedAt:new Date().toISOString(),files};}
module.exports={sha256,payloadRoot,readBundle,inspectPayload,requirePayload,createBundle};

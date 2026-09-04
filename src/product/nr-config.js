'use strict';
const fs=require('node:fs');
const {appError}=require('./errors');
const {hash,readBounded,atomicWrite}=require('./safe-files');
const SECTION='NRBeforeSR';
const ranges={Enabled:[0,1,true],Mode:[1,2,true],Intensity:[0,2,false],TransferStrength:[0,4,false],PostTransferStrength:[0,4,false],Style:[0,2,true],AutoMask:[0,1,true],WorkMode:[0,5,true],CustomWorkScale:[.25,1,false]};
const canonical=new Map([...Object.keys(ranges),'ConfigVersion','StrengthConfigVersion'].map(k=>[k.toLowerCase(),k]));
function fail(message){throw appError('ERR_BAD_REQUEST',{reason:message});}
function normalizeValue(key,value){const r=ranges[key];if(!r||typeof value!=='number'||!Number.isFinite(value)||value<r[0]||value>r[1]||(r[2]&&!Number.isInteger(value)))fail('Invalid '+key);return value;}
function decode(bytes){
 const utf16=bytes[0]===255&&bytes[1]===254,bom=utf16||(bytes[0]===239&&bytes[1]===187&&bytes[2]===191);
 const encoding=utf16?'utf16le':'utf8',body=bytes.subarray(utf16?2:bom?3:0);
 if(utf16&&body.length%2)fail('Truncated UTF-16 config');
 let text;try{text=new TextDecoder(utf16?'utf-16le':'utf-8',{fatal:true}).decode(body);}catch{fail('Unsupported config text encoding');}
 return {text,encoding,bom};
}
function encode(text,d){const b=Buffer.from(text,d.encoding);return d.bom?Buffer.concat([d.encoding==='utf16le'?Buffer.from([255,254]):Buffer.from([239,187,191]),b]):b;}
function parseSection(text){
 const result={};let active=false,sections=0;const seen=new Set();
 for(const line of String(text).split(/\r?\n/)){
  const s=line.match(/^\s*\[([^\]]+)\]\s*$/);if(s){active=s[1].trim().toLowerCase()===SECTION.toLowerCase();if(active&&++sections>1)fail('Duplicate NR section');continue;}
  if(!active||/^\s*[;#]/.test(line))continue;const p=line.match(/^\s*([^=]+?)\s*=\s*([^;#]*)(.*)$/);if(!p)continue;
  const key=canonical.get(p[1].trim().toLowerCase());if(!key)continue;if(seen.has(key))fail('Duplicate key '+key);seen.add(key);
  if(!p[2].trim()||!Number.isFinite(Number(p[2])))fail('Invalid key '+key);result[key]=Number(p[2]);
 }
 if(sections!==1)fail('Missing NRBeforeSR section');return result;
}
function schemaFor(v){if(v.ConfigVersion===4&&v.StrengthConfigVersion===1)return 'residual-v4';if((v.ConfigVersion===undefined||v.ConfigVersion<4)&&v.Intensity!==undefined)return 'legacy';return 'unknown';}
function allowed(schema){return schema==='residual-v4'?['Enabled','Mode','TransferStrength','Style','AutoMask','WorkMode']:schema==='legacy'?['Enabled','Mode','Intensity','Style','AutoMask']:[];}
function updateSection(text,patch){
 const current=parseSection(text),schema=schemaFor(current),keys=allowed(schema);if(schema==='unknown')fail('Unknown core schema');
 if(!patch||typeof patch!=='object'||Array.isArray(patch)||Object.keys(patch).length===0)fail('Empty patch');
 const edits={};for(const[k,v]of Object.entries(patch)){if(!keys.includes(k))fail('Unsupported setting '+k);edits[k]=normalizeValue(k,v);}
 if('TransferStrength'in edits)edits.PostTransferStrength=edits.TransferStrength;
 const nl=text.includes('\r\n')?'\r\n':'\n',lines=text.split(/\r?\n/);let active=false,start=-1,end=lines.length;const seen=new Set();
 for(let i=0;i<lines.length;i++){const s=lines[i].match(/^\s*\[([^\]]+)\]\s*$/);if(s){if(active){end=i;break;}active=s[1].trim().toLowerCase()===SECTION.toLowerCase();if(active)start=i;continue;}
  if(!active)continue;const p=lines[i].match(/^(\s*)([^=;#]+?)(\s*=\s*)([^;#]*)(.*)$/);if(!p)continue;
  const k=canonical.get(p[2].trim().toLowerCase());if(Object.hasOwn(edits,k)){lines[i]=`${p[1]}${p[2]}${p[3]}${edits[k]}${p[5]?' '+p[5]:''}`;seen.add(k);}}
 let pos=end;while(pos>start+1&&!lines[pos-1].trim())pos--;lines.splice(pos,0,...Object.keys(edits).filter(k=>!seen.has(k)).map(k=>`${k}=${edits[k]}`));return lines.join(nl);
}
function readConfig(file){const bytes=readBounded(file),d=decode(bytes),values=parseSection(d.text);return {schema:schemaFor(values),revision:hash(bytes),values};}
async function writeConfig(file,patch,expectedRevision){const bytes=readBounded(file);if(!expectedRevision||hash(bytes)!==expectedRevision)throw appError('ERR_CONFIG_CHANGED');const d=decode(bytes);const next=updateSection(d.text,patch);if(hash(readBounded(file))!==expectedRevision)throw appError('ERR_CONFIG_CHANGED');await atomicWrite(file,encode(next,d));return readConfig(file);}
function recommendedPatch(){throw appError('ERR_BAD_REQUEST',{reason:'No universal recommended preset; use the matching payload defaults.'});}
module.exports={SECTION,parseSection,schemaFor,allowed,normalizeValue,updateSection,readConfig,writeConfig,recommendedPatch,decode,encode};

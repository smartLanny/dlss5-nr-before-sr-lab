'use strict';
const fs=require('fs'),path=require('path');
const file=path.join(__dirname,'../payload/nr-before-sr/bundle.json');
const bundle=JSON.parse(fs.readFileSync(file,'utf8'));
if(bundle.version!==4||!bundle.versions)throw Error('需要已校验的 v4 成品目录。');
if(Object.keys(bundle.versions).some(id=>/^0\.4\.6(?:$|-)/.test(id)))delete bundle.versions['0.4.5-ota'];
for(const [id,entry] of Object.entries(bundle.versions)){
  if(/^0\.4\./.test(id))entry.label=`${id} · Beta`;
  if(/^0\.3\./.test(id)){entry.label=`${id} · 稳定基线`;entry.notes='0.3 稳定基线，用于日常使用与回退对照；0.4 系列仍为 Beta。';}
}
fs.writeFileSync(file,JSON.stringify(bundle,null,2)+'\n');
console.log('核心目录：0.3 稳定基线；0.4 Beta；0.4.6 已替代 0.4.5 的常规分发。');

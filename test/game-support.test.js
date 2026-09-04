'use strict';
const test=require('node:test'),assert=require('node:assert/strict');const {assess}=require('../src/product/game-support');
const base={chosen:{bitness:64,api:'dxgi',apiLabel:'DirectX 12'},primaryDlss:{name:'nvngx_dlss.dll'}};
test('D3D12 core is not advertised for DX11 just because both use DXGI',()=>{assert.equal(assess(base).supported,true);for(const label of ['DirectX 11','DirectX 11/12','DirectX 10','Vulkan'])assert.equal(assess({...base,chosen:{...base.chosen,apiLabel:label}}).supported,false);});
test('missing native DLSS, 32-bit and emulator inputs are refused',()=>{assert.equal(assess({...base,primaryDlss:null}).supported,false);assert.equal(assess({...base,chosen:{...base.chosen,bitness:32}}).supported,false);assert.equal(assess({...base,emulator:{name:'test'}}).supported,false);});

'use strict';
// Serves only four static preview files on loopback. No file-system APIs exposed to the page.
const http=require('node:http'),fs=require('node:fs'),path=require('node:path');
const root=path.join(__dirname,'..','preview'),types={'index.html':'text/html; charset=utf-8','style.css':'text/css; charset=utf-8','renderer.js':'text/javascript; charset=utf-8','demo.js':'text/javascript; charset=utf-8'};
http.createServer((req,res)=>{const name=req.url==='/'?'index.html':req.url.slice(1);if(req.method!=='GET'||!Object.hasOwn(types,name)){res.writeHead(404).end();return;}res.setHeader('Content-Type',types[name]);res.end(fs.readFileSync(path.join(root,name)));}).listen(8790,'127.0.0.1',()=>console.log('Sample-data preview only: http://127.0.0.1:8790 (Ctrl+C stops)'));

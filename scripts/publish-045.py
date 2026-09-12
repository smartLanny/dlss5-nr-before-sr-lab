"""Publish only the exact tested 0.4.5 source; never overwrite historical releases."""
from __future__ import annotations
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import urllib.error
import urllib.parse
import urllib.request

REPO='smartLanny/dlss5-nr-before-sr-lab'
BRANCH='release/0.4.5-test'
VERSION='beta0.4.5'
ROOT=Path(__file__).resolve().parents[1]
API='https://api.github.com/repos/'+REPO
UPLOAD='https://uploads.github.com/repos/'+REPO

class NoRedirect(urllib.request.HTTPRedirectHandler):
 def redirect_request(self,req,fp,code,msg,headers,newurl):
  raise RuntimeError('Unexpected redirect; credentials will not be forwarded')

def main():
 if (os.environ.get('GITHUB_REPOSITORY')!=REPO or os.environ.get('GITHUB_REF')!='refs/heads/'+BRANCH or
     os.environ.get('GITHUB_EVENT_NAME')!='push'):
  raise RuntimeError('Publication requires an explicit push on the isolated 0.4.5 branch')
 sha=os.environ.get('GITHUB_SHA','');run=os.environ.get('GITHUB_RUN_ID','')
 if not re.fullmatch('[0-9a-f]{40}',sha) or not run.isdigit():raise RuntimeError('Invalid workflow source identity')
 token=os.environ['GH_TOKEN']
 spec=importlib.util.spec_from_file_location('ota045',ROOT/'scripts/package-045.py')
 package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)
 if package.source_sha()!=sha:raise RuntimeError('Checkout is not the tested source')
 expected={f'DLSS5-{VERSION}-{lang}-OTA.zip' for lang in package.ADDONS}
 if {p.name for p in (ROOT/'ota').iterdir()}!=expected:raise RuntimeError('Expected exactly two OTA files')
 assets=[package.verify(ROOT/'ota'/f'DLSS5-{VERSION}-{lang}-OTA.zip',sha,lang) for lang in package.ADDONS]
 opener=urllib.request.build_opener(NoRedirect())
 def request(url,method='GET',data=None,binary=None,missing=False):
  if not (url.startswith(API+'/') or url.startswith(UPLOAD+'/')):raise RuntimeError('Unexpected repository destination')
  payload=binary if binary is not None else (json.dumps(data).encode() if data is not None else None)
  headers={'Authorization':'Bearer '+token,'Accept':'application/vnd.github+json','X-GitHub-Api-Version':'2022-11-28','User-Agent':'dlss5-045-release'}
  if payload is not None:headers['Content-Type']='application/zip' if binary is not None else 'application/json'
  try:
   with opener.open(urllib.request.Request(url,data=payload,method=method,headers=headers),timeout=90) as r:
    raw=r.read();return json.loads(raw) if raw else None
  except urllib.error.HTTPError as e:
   if missing and e.code==404:return None
   raise RuntimeError(f'GitHub {method} failed: HTTP {e.code}') from None
 def check_ref():
  if request(API+'/git/ref/heads/'+urllib.parse.quote(BRANCH,safe='/'))['object']['sha']!=sha:
   raise RuntimeError('Newer branch source exists; refusing stale publication')
  tag=request(API+'/git/ref/tags/'+VERSION,missing=True)
  if tag and (tag['object']['type']!='commit' or tag['object']['sha']!=sha):
   raise RuntimeError('Historical tag differs; no rewrite permitted')
 check_ref()
 body=(ROOT/'docs/releases/beta0.4.5.md').read_text(encoding='utf-8')
 body+=f'\n\n## 固定发布身份\nPR: #117\n\nSource: `{sha}`\n\n全量验证/发布运行：https://github.com/{REPO}/actions/runs/{run}\n\n'
 body+='本 Release 只包含两个 OTA。每个包含一个语言 addon、同提交构建的自有 nrchain_nvngx.dll、说明/校验清单和已有采集脚本；不含默认 INI、NVIDIA 运行库或 DX11。\n\n'
 body+='| OTA | 字节 | SHA256 |\n|---|---:|---|\n'
 for a in assets:body+=f"| {a['name']} | {a['bytes']} | `{a['sha256']}` |\n"
 release=request(API+'/releases/tags/'+VERSION,missing=True)
 if release:
  if not release['draft'] or release['target_commitish']!=sha:raise RuntimeError('Existing release cannot be overwritten')
 else:
  release=request(API+'/releases','POST',{'tag_name':VERSION,'target_commitish':sha,'name':'DLSS5 NR beta0.4.5｜0.4 集中修复测试版（非DX11）',
    'body':body,'draft':True,'prerelease':True,'make_latest':'false'})
 rid=release['id']
 if not isinstance(rid,int):raise RuntimeError('Invalid release identity')
 existing=request(API+f'/releases/{rid}/assets?per_page=100')
 if any(a['name'] not in expected for a in existing):raise RuntimeError('Unexpected draft assets')
 for asset in assets:
  matches=[x for x in existing if x['name']==asset['name']]
  if len(matches)>1:raise RuntimeError('Duplicate draft asset')
  if not matches:
   data=(ROOT/'ota'/asset['name']).read_bytes()
   if hashlib.sha256(data).hexdigest()!=asset['sha256']:raise RuntimeError('Asset changed after verification')
   request(UPLOAD+f'/releases/{rid}/assets?name='+urllib.parse.quote(asset['name']),'POST',binary=data)
 verified=request(API+f'/releases/{rid}/assets?per_page=100')
 if len(verified)!=2 or {a['name'] for a in verified}!=expected:raise RuntimeError('Two-language asset set incomplete')
 for asset in assets:
  server=next(a for a in verified if a['name']==asset['name'])
  if server['state']!='uploaded' or server['size']!=asset['bytes'] or server.get('digest')!='sha256:'+asset['sha256']:
   raise RuntimeError('Server bytes or SHA256 mismatch')
 check_ref()  # A commit during upload must not silently publish an obsolete candidate.
 published=request(API+f'/releases/{rid}','PATCH',{'body':body,'draft':False,'prerelease':True,'make_latest':'false'})
 if published['draft'] or not published['prerelease']:raise RuntimeError('Unexpected public state')
 tag=request(API+'/git/ref/tags/'+VERSION)
 if tag['object']['type']!='commit' or tag['object']['sha']!=sha:raise RuntimeError('Published tag is not the tested source')
 print(json.dumps({'release':published['html_url'],'sourceCommit':sha,'assets':assets},ensure_ascii=False,indent=2))
if __name__=='__main__':main()

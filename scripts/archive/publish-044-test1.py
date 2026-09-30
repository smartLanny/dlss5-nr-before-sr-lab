"""Publish the owner's branch prerelease after both reusable CI workflows succeed.
No existing published release/tag is overwritten. Uploads go only to this repo.
"""
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
BRANCH='fix/044-d3d12-compat-test1'
VERSION='beta0.4.4-test1'
ROOT=Path(__file__).resolve().parents[1]
API='https://api.github.com/repos/'+REPO
UPLOAD='https://uploads.github.com/repos/'+REPO

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,req,fp,code,msg,headers,newurl):
        raise RuntimeError('Unexpected redirect; credentials will not be forwarded')


def main()->None:
    if os.environ.get('GITHUB_REPOSITORY')!=REPO or os.environ.get('GITHUB_REF')!='refs/heads/'+BRANCH:
        raise RuntimeError('Publication restricted to the isolated owner branch')
    if os.environ.get('GITHUB_EVENT_NAME')!='push':raise RuntimeError('Publication requires an explicit branch push')
    sha=os.environ.get('GITHUB_SHA','');run=os.environ.get('GITHUB_RUN_ID','')
    if not re.fullmatch('[0-9a-f]{40}',sha) or not run.isdigit():raise RuntimeError('Missing workflow identity')
    token=os.environ['GH_TOKEN']
    spec=importlib.util.spec_from_file_location('branch_ota',ROOT/'scripts/package-044-test1.py')
    package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)
    if package.source_sha()!=sha:raise RuntimeError('Checkout does not match the tested source')
    ota=ROOT/'ota'
    expected={f'DLSS5-{VERSION}-{language}-OTA.zip' for language in package.ADDONS}
    if {p.name for p in ota.iterdir()}!=expected:raise RuntimeError('Publication requires exactly two OTA files')
    assets=[package.verify(ota/f'DLSS5-{VERSION}-{language}-OTA.zip',sha,language) for language in package.ADDONS]
    opener=urllib.request.build_opener(NoRedirect())
    def request(url:str,method:str='GET',data=None,binary:bytes|None=None,missing_ok:bool=False):
        if not (url.startswith(API+'/') or url.startswith(UPLOAD+'/')):raise RuntimeError('Unexpected destination')
        payload=binary if binary is not None else (json.dumps(data).encode() if data is not None else None)
        headers={'Authorization':'Bearer '+token,'Accept':'application/vnd.github+json',
                 'X-GitHub-Api-Version':'2022-11-28','User-Agent':'dlss5-branch-release'}
        if payload is not None:headers['Content-Type']='application/zip' if binary is not None else 'application/json'
        try:
            with opener.open(urllib.request.Request(url,data=payload,headers=headers,method=method),timeout=90) as response:
                raw=response.read()
                return json.loads(raw) if raw else None
        except urllib.error.HTTPError as error:
            if missing_ok and error.code==404:return None
            raise RuntimeError(f'GitHub {method} failed with HTTP {error.code}') from None

    current=request(API+'/git/ref/heads/'+urllib.parse.quote(BRANCH,safe='/'))
    if current['object']['sha']!=sha:raise RuntimeError('A newer branch revision exists; do not publish the stale run')
    tag=request(API+'/git/ref/tags/'+VERSION,missing_ok=True)
    if tag and (tag['object']['type']!='commit' or tag['object']['sha']!=sha):
        raise RuntimeError('Existing tag is not this exact source; refusing any rewrite')
    body='''独立分支 **beta0.4.4-test1**，仅供已有 R3 安装的 D3D12 问题电脑定向测试，不替换 R3 主发布线。

本包尝试修复：NR关闭/透传时嵌套NGX句柄误判；漏登记请求被其它载体取消；完整观察后仅含视口/裁剪框的空PSO状态恢复；后置对照遗留诊断原图模式；自定义前置比例百分比显示与后置失败阶段说明。

优先样本：异环5060Ti、街霸6的5060Ti/5080、NBA2K27的5070；沉没之城2/地铁增强版/Elden+ERSS的5080作为相邻合同候选。每台电脑的推送范围以对应Issue回复为准。

**没有宣称游戏已经实测修好。不含DX11；不含0.5算法；不提供新的MFG解锁/倍率控制；AW2+RTX40MFG热补丁冲突、007/地平线崩溃根因仍未解决。**

安装：完全退出游戏，把旧同类addon备份到游戏目录外，只用一个语言。保留现有INI、ReShade、nrchain_nvngx.dll和显卡对应运行库。包内没有默认INI，不会自动安装/上传。

玩家只需进旧问题场景，F6看是否有变化，回复是否有效/闪烁/崩溃；能运行时F8收日志，画面异常方便时F7。出现持续异常就回退，不要求换驱动或做技术矩阵。

发布门禁：同一源码提交的完整0.4验证与跨平台反馈矩阵成功后才公开；源码/编译/CI/WARP不等于NVIDIA模型、游戏或FG节奏实测。
'''
    body+=f'\nPR: #92\n\nSource: `{sha}`\n\n验证运行：https://github.com/{REPO}/actions/runs/{run}\n\n'
    body+='| OTA | 字节 | SHA256 |\n|---|---:|---|\n'
    for a in assets:body+=f"| {a['name']} | {a['bytes']} | `{a['sha256']}` |\n"
    release=request(API+'/releases/tags/'+VERSION,missing_ok=True)
    if release:
        if not release['draft'] or release['target_commitish']!=sha:
            raise RuntimeError('Refusing to overwrite an existing release')
    else:
        release=request(API+'/releases','POST',{'tag_name':VERSION,'target_commitish':sha,
            'name':'DLSS5 NR beta0.4.4-test1｜D3D12 定向修复测试（非DX11）',
            'body':body,'draft':True,'prerelease':True,'make_latest':'false'})
    rid=release['id']
    if not isinstance(rid,int):raise RuntimeError('Invalid release id')
    existing=request(API+f'/releases/{rid}/assets?per_page=100')
    if any(a['name'] not in expected for a in existing):raise RuntimeError('Draft contains unexpected assets')
    for a in assets:
        matched=[e for e in existing if e['name']==a['name']]
        if len(matched)>1:raise RuntimeError('Duplicate draft asset')
        if not matched:
            blob=(ota/a['name']).read_bytes()
            if hashlib.sha256(blob).hexdigest()!=a['sha256']:raise RuntimeError('OTA changed after verification')
            request(UPLOAD+f'/releases/{rid}/assets?name='+urllib.parse.quote(a['name']),
                    'POST',binary=blob)
    verified=request(API+f'/releases/{rid}/assets?per_page=100')
    if len(verified)!=2 or {a['name'] for a in verified}!=expected:raise RuntimeError('Release assets are incomplete')
    for a in assets:
        server=next(e for e in verified if e['name']==a['name'])
        if server['state']!='uploaded' or server['size']!=a['bytes'] or server.get('digest')!='sha256:'+a['sha256']:
            raise RuntimeError('Server asset digest/size does not match verified bytes')
    published=request(API+f'/releases/{rid}','PATCH',{'body':body,'draft':False,'prerelease':True,'make_latest':'false'})
    if published['draft'] or not published['prerelease']:raise RuntimeError('Publication state not as requested')
    tag=request(API+'/git/ref/tags/'+VERSION)
    if tag['object']['type']!='commit' or tag['object']['sha']!=sha:raise RuntimeError('Published tag does not resolve to tested source')
    print(json.dumps({'release':published['html_url'],'sourceCommit':sha,'assets':assets},ensure_ascii=False,indent=2))

if __name__=='__main__':main()

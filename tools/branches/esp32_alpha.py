#!/usr/bin/env python3
"""Prepare or publish real alpha firmware. A manual main workflow is the authority.

Local `stage` never publishes. `release` is only callable by the trusted manual
main workflow, after both builds and all bundle checks. It never replaces tags
or assets. Hardware qualification is pending and does not disable alpha installs.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile
ROOT=Path(__file__).resolve().parents[2]
SPEC=importlib.util.spec_from_file_location('alpha_site',ROOT/'tools/branches/prepare_esp32_site.py')
s=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(s)
REPO='Pepeuch/universal-gnss'
VARIANTS=('s3-gateway','c3-rover-usb')
ENV={**os.environ,'GIT_NO_REPLACE_OBJECTS':'1','GIT_NO_LAZY_FETCH':'1','GIT_TERMINAL_PROMPT':'0'}
def git(repo,*args):return subprocess.check_output(['git','-C',str(repo),*args],env=ENV,text=True).strip()
def source(repo,sha,tag='',main_ref='origin/main'):
    if not isinstance(sha,str) or not re.fullmatch('[a-f0-9]{40}',sha):raise ValueError('exact lowercase main commit required')
    if git(repo,'cat-file','-t',sha)!='commit':raise ValueError('source must be a commit')
    tree=git(repo,'rev-parse',sha+'^{tree}');s.require_main(repo,sha,tree,main_ref)
    for path in ('embedded/esp32/native/ci.py','embedded/esp32/migration/dependencies.json','embedded/esp32/native/main/gateway.cpp','embedded/esp32/native/main/rover.c'):
        subprocess.run(['git','-C',str(repo),'cat-file','-e',sha+':'+path],env=ENV,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    derived='esp32-alpha-'+sha[:12];tag=tag or derived
    if tag not in (derived,'0.7.5-a01'):raise ValueError('alpha tag must identify this exact source; v* publication excluded')
    exists=subprocess.run(['git','-C',str(repo),'show-ref','--verify','--quiet','refs/tags/'+tag],env=ENV).returncode==0
    if exists and git(repo,'rev-parse','refs/tags/'+tag+'^{commit}')!=sha:raise ValueError('existing tag targets another commit; never move it')
    if tag=='0.7.5-a01' and not exists:raise ValueError('0.7.5-a01 must already exist at the selected source')
    return {'source_sha':sha,'source_tree':tree,'release_tag':tag,'release_url':'https://github.com/'+REPO+'/releases/tag/'+tag}
def require_s3_memory(entry,directory):
    if entry['profile']['variant']=='s3-gateway':
        memory=s.p.load(directory/'qualification.json').get('binary',{}).get('boot_memory',{})
        if memory.get('main_stack_bytes',0)<32768 or memory.get('tls_external') is not True:
            raise ValueError('S3 alpha requires the boot-stack and TLS memory corrections')
def stage(repo,sha,bundles,output,tag='',review='manual workflow_dispatch'):
    identity=source(repo,sha,tag)
    if len(bundles)!=2:raise ValueError('both real S3 and C3 bundles are required')
    approvals={'schema':1,'sources':[{'source_sha':sha,'source_tree':identity['source_tree'],'review':review,'publication':'main_approved'}]}
    seen=set()
    for root in bundles:
        entry,directory,_=s.inspect_bundle(root,approvals)
        variant=entry['profile']['variant']
        if entry['source_sha']!=sha or variant not in VARIANTS or variant in seen:raise ValueError('wrong/duplicate alpha source or profile')
        require_s3_memory(entry,directory)
        seen.add(variant)
    if seen!=set(VARIANTS):raise ValueError('incomplete alpha build matrix')
    result=s.prepare(output,approvals,bundles,repo)
    (result/'index.html').write_text('<!doctype html><meta charset="utf-8"><meta http-equiv="refresh" content="0;url=flasher.html"><a href="flasher.html">UG ESP32 ALPHA Web Flasher</a>\n')
    (result/'alpha-publication.json').write_bytes(s.p.json_bytes({**identity,'channel':'ALPHA','hardware':'qualification_pending','publisher_source':git(repo,'rev-parse','HEAD')}))
    return identity,result

def deterministic_zip(directory,path):
    with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_STORED) as archive:
        for file in sorted(Path(directory).rglob('*')):
            if file.is_symlink():raise ValueError('site links are not publishable')
            if not file.is_file():continue
            info=zipfile.ZipInfo(file.relative_to(directory).as_posix(),date_time=(1980,1,1,0,0,0));info.external_attr=0o100644<<16;info.create_system=3
            archive.writestr(info,file.read_bytes())

def remote_tag(repo,tag):
    rows=git(repo,'ls-remote','origin','refs/tags/'+tag,'refs/tags/'+tag+'^{}').splitlines()
    refs={line.split()[1]:line.split()[0] for line in rows if line}
    return refs.get('refs/tags/'+tag+'^{}',refs.get('refs/tags/'+tag))

def release(repo,site,assets,env=os.environ):
    if env.get('GITHUB_EVENT_NAME') not in ('workflow_dispatch','workflow_run') or env.get('GITHUB_REF')!='refs/heads/main' or env.get('GITHUB_REPOSITORY')!=REPO:
        raise ValueError('publication requires the maintainer manual workflow on main')
    if env.get('GITHUB_EVENT_NAME') in ('workflow_run','workflow_dispatch'):
        spec=importlib.util.spec_from_file_location('release_authority',ROOT/'tools/branches/esp32_publication_gate.py')
        gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
        gate.authorize(s.p.load(Path(site)/'alpha-publication.json')['source_sha'],repo)
    site=Path(site);meta=s.p.load(site/'alpha-publication.json');identity=source(repo,meta['source_sha'],meta['release_tag'])
    if meta['source_tree']!=identity['source_tree']:raise ValueError('publication tree changed')
    catalog=s.p.load(site/'flasher/catalog.json')
    if len([v for v in catalog['images'] if v['source_sha']==identity['source_sha']])!=2 or {v['profile']['variant'] for v in catalog['images']}!=set(VARIANTS):raise ValueError('nonempty complete catalogue required')
    # Reconstruct and revalidate the review bundles from the exact prepared site.
    import tempfile,shutil
    with tempfile.TemporaryDirectory(prefix='ug-alpha-check-') as temp:
        for i,entry in enumerate(catalog['images']):
            root=Path(temp)/str(i);folder=root/'published'/entry['build_key'];folder.parent.mkdir(parents=True)
            shutil.copytree(site/'flasher/published'/entry['build_key'],folder)
            (root/'catalog-entry.json').write_bytes(s.p.json_bytes(entry))
            checked,directory,_=s.inspect_bundle(root,s.p.load(site/'flasher/approved-sources.json'))
            require_s3_memory(checked,directory)
    assets=Path(assets);assets.mkdir(parents=True,exist_ok=False)
    archive=assets/'esp32-alpha-site.zip';deterministic_zip(site,archive)
    # Include an exact corresponding-source archive, not just expiring CI artifacts.
    subprocess.run(['git','-C',str(repo),'archive','--format=tar.gz','--output='+str((assets/'firmware-source.tar.gz').resolve()),identity['source_sha']],check=True,env=ENV)
    for name in ('catalog.json','approved-sources.json'):shutil.copyfile(site/'flasher'/name,assets/name)
    for entry in catalog['images']:
        if entry['source_sha']!=identity['source_sha']:continue
        shutil.copyfile(site/entry['bundle'],assets/(entry['profile']['variant']+'.zip'))
    sums={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(assets.iterdir())}
    (assets/'SHA256SUMS').write_text(''.join(value+'  '+key+'\n' for key,value in sums.items()))
    notes=assets/'release-notes.md';notes.write_text('ALPHA — qualification matérielle en cours.\n\nSource: '+identity['source_sha']+'\n\nS3 Gateway W5500/GNSS/NTRIP/ESP-NOW; C3 ESP-NOW RTCM USB/UART runtime.\nInstallation complète; sauvegarder Flash/NVS. Aucun secret ni configuration utilisateur inclus.\nLes essais logiciels ne prouvent pas RF, RTK FIX ou le câblage de chaque installation.\n')
    command_env={**env,'GH_TOKEN':env.get('GITHUB_TOKEN','')}
    tag=identity['release_tag']
    actual=remote_tag(repo,tag)
    if actual is None:
        subprocess.run(['gh','api','--method','POST','repos/'+REPO+'/git/refs','-f','ref=refs/tags/'+tag,'-f','sha='+identity['source_sha']],check=True,env=command_env,stdout=subprocess.DEVNULL)
        actual=remote_tag(repo,tag)
    if actual!=identity['source_sha']:raise ValueError('remote tag conflict; never retarget it')
    # All assets are immutable. A conflicting rerun stops without clobbering them.
    view=subprocess.run(['gh','release','view',tag,'--repo',REPO,'--json','tagName,isPrerelease'],capture_output=True,text=True,env=command_env)
    if view.returncode==0:
        data=json.loads(view.stdout)
        if not data.get('isPrerelease'):raise ValueError('existing release is not an alpha prerelease')
        old=assets/'existing';old.mkdir()
        subprocess.run(['gh','release','download',tag,'--repo',REPO,'--dir',str(old)],check=True,env=command_env)
        if any(not (old/file.name).is_file() or (old/file.name).read_bytes()!=file.read_bytes() for file in assets.iterdir() if file.is_file()):raise ValueError('immutable release asset conflict; no overwrite')
    else:
        # An existing tag was already checked above; gh --target creates only the
        # derived esp32-alpha-* tag when absent, never retargets another tag.
        subprocess.run(['gh','release','create',tag,'--repo',REPO,'--verify-tag','--prerelease','--latest=false','--title','ESP32 ALPHA '+tag,'--notes-file',str(notes),*[str(p) for p in assets.iterdir() if p.is_file()]],check=True,env=command_env)
    # Verify durable downloaded bytes before allowing Pages upload/deployment.
    verify=assets/'verified';verify.mkdir()
    subprocess.run(['gh','release','download',tag,'--repo',REPO,'--dir',str(verify)],check=True,env=command_env)
    for name,digest in {**sums,'SHA256SUMS':hashlib.sha256((assets/'SHA256SUMS').read_bytes()).hexdigest()}.items():
        if hashlib.sha256((verify/name).read_bytes()).hexdigest()!=digest:raise ValueError('durable release verification failed')
    return identity

def verify_online(site,url):
    import urllib.request,time
    if not isinstance(url,str) or not url.startswith('https://pepeuch.github.io/universal-gnss/'):
        raise ValueError('unexpected public Pages URL')
    base=url.rstrip('/')+'/'
    site=Path(site);paths=['flasher.html','flasher/catalog.json','flasher/approved-sources.json','flasher/app.js','flasher/portable.js','flasher/provision.js']
    catalog=s.p.load(site/'flasher/catalog.json')
    if not catalog['images']:raise ValueError('public alpha catalogue must not be empty')
    if (site/'flasher/publications.json').exists():paths.append('flasher/publications.json')
    for entry in catalog['images']:
        prefix='flasher/published/'+entry['build_key']+'/'
        paths.extend(prefix+name for name in ('manifest.json','provenance.json','qualification.json','bundle.zip'))
        paths.extend(prefix+part['path'] for part in s.p.load(site/entry['manifest'])['builds'][0]['parts'])
    for name in paths:
        expected=(site/name).read_bytes()
        for attempt in range(6):
            try:
                with urllib.request.urlopen(base+name,timeout=20) as response:data=response.read(len(expected)+1)
                if data!=expected:raise ValueError('public file differs: '+name)
                break
            except (OSError,ValueError):
                if attempt==5:raise
                time.sleep(5)
    return {'url':base+'flasher.html','verified_public_files':len(paths)}

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=('validate','stage','release','verify-online'))
    p.add_argument('--source-sha');p.add_argument('--release-tag',default='');p.add_argument('--bundle',type=Path,action='append',default=[])
    p.add_argument('--output',type=Path);p.add_argument('--site',type=Path);p.add_argument('--assets',type=Path);p.add_argument('--review',default='manual workflow_dispatch');p.add_argument('--url')
    a=p.parse_args(argv)
    if os.getuid()==0:p.error('run as normal project user')
    try:
        if a.command=='validate':result=source(ROOT,a.source_sha,a.release_tag)
        elif a.command=='stage':
            result=stage(ROOT,a.source_sha,a.bundle,a.output,a.release_tag,a.review)[0]
            spec=importlib.util.spec_from_file_location('catalog_history',ROOT/'tools/branches/esp32_catalog_history.py')
            history=importlib.util.module_from_spec(spec);spec.loader.exec_module(history)
            history.merge(a.output)
        elif a.command=='release':result=release(ROOT,a.site,a.assets)
        else:result=verify_online(a.site,a.url)
    except (ValueError,KeyError,TypeError,OSError,subprocess.SubprocessError,zipfile.BadZipFile) as e:p.error(str(e))
    print(json.dumps(result,sort_keys=True));return 0
if __name__=='__main__':raise SystemExit(main())

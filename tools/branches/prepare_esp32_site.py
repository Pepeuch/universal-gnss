#!/usr/bin/env python3
"""Assemble a local Pages review directory. Never deploy, release, tag or push.

Published firmware must have durable release/storage backing before a separately
authorized Pages deployment. Actions artifacts are review evidence, not archives.
"""
import argparse
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import zipfile
ROOT=Path(__file__).resolve().parents[2]
SPEC=importlib.util.spec_from_file_location('site_requests',ROOT/'tools/branches/firmware_request.py')
r=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(r)
p=r.p

def inspect_bundle(root,approvals):
    root=Path(root).resolve(strict=True);entry=p.load(root/'catalog-entry.json')
    profile=p.profiles.normalize(entry['profile'])
    request=r.validate(entry['source_sha'],profile['variant'],profile.get('uart_tx',4),profile.get('uart_baud',115200),approvals)
    if request['publication'] not in ('main_approved','ci_preview') or entry.get('build_key')!=request['build_key']:
        raise ValueError('publication approval required; review artifacts are not a catalogue')
    key=request['build_key'];directory=root/'published'/key
    if entry.get('manifest')!=f'flasher/published/{key}/manifest.json' or entry.get('bundle')!=f'flasher/published/{key}/bundle.zip':
        raise ValueError('noncanonical bundle paths')
    provenance=p.load(directory/'provenance.json');qualification=p.load(directory/'qualification.json')
    pinned=p.load(ROOT/'embedded/esp32/migration/dependencies.json')['sdk']['image']
    output={'transport':'uart1-tx' if profile['output']=='uart' else 'usb-serial-jtag',
            'tx_pin':profile.get('uart_tx'),'baud':profile.get('uart_baud')}
    if (entry.get('sdk_image')!=pinned or entry.get('qualification')!='software_only' or
        p.profiles.normalize(provenance.get('profile'))!=profile or
        qualification.get('sdk_image')!=pinned or qualification.get('variant')!=profile['variant'] or qualification.get('source_object')!=entry['source_sha'] or
        qualification.get('network_during_build_and_tests')!='disconnected_no_interfaces' or qualification.get('source_mounts') is not False or
        qualification.get('flash_or_publication') is not False or qualification.get('binary',{}).get('radio_configuration')!='runtime_nvs_encrypted_unicast' or qualification.get('binary',{}).get('portable_configuration_schema')!=1 or
        qualification.get('binary',{}).get('sha256')!=entry['sha256'] or
        (profile['variant']=='s3-gateway' and qualification.get('host_tests')!='pass') or
        any(provenance.get(k)!=entry.get(k) for k in ('sdk_image','sha256','manifest','manifest_sha256','partition_sha256','qualification','build_key','nvs_preservation')) or
        qualification.get('binary',{}).get('profile')!=profile['profile'] or
        qualification.get('binary',{}).get('local_gnss') is not (profile['profile']=='gateway') or
        (profile['profile']=='rover' and any(qualification.get('binary',{}).get('rover_output',{}).get(k)!=v for k,v in output.items())) or
        provenance.get('source_sha')!=entry['source_sha'] or qualification.get('source_tree')!=request['source_tree'] or
        qualification.get('source_commit')!=entry['source_sha'] or qualification.get('source_type')!='commit' or
        qualification.get('build_and_verify')!='pass' or qualification.get('reproducibility')!='pass' or
        provenance.get('install_mode')!='full' or entry.get('install_mode')!='full' or
        entry.get('nvs_preservation')!='not_qualified'):
        raise ValueError('bundle qualification/source mismatch')
    manifest_bytes=(directory/'manifest.json').read_bytes()
    if p.digest(manifest_bytes)!=entry['manifest_sha256']:raise ValueError('manifest checksum mismatch')
    manifest=p.load(directory/'manifest.json')
    if (set(manifest)!={'name','version','new_install_prompt_erase','builds'} or manifest['new_install_prompt_erase'] is not True or
        len(manifest['builds'])!=1 or set(manifest['builds'][0])!={'chipFamily','parts'} or
        manifest['builds'][0]['chipFamily']!=profile['chip'] or len(manifest['builds'][0]['parts'])!=4):
        raise ValueError('bundle chip/parts mismatch')
    expected={'manifest.json','provenance.json','qualification.json','LICENSE.txt','LICENSE-UG-LGPL.txt','DEPENDENCIES.json','SOURCE.txt'}
    for part in manifest['builds'][0]['parts']:
        name=part['path']
        if (set(part)!={'path','offset'} or type(part['offset']) is not int or part['offset']<0 or
            name[:6]!=f"{part['offset']:06x}"):
            raise ValueError('invalid bundle offset')
        if not isinstance(name,str) or not re.fullmatch(r'[a-f0-9]{6}-(bootloader|partition-table|ota_data_initial|universal-gnss-esp32)\.bin',name):raise ValueError('unsafe bundle member')
        file=(directory/name).resolve(strict=True)
        if (file.parent!=directory or p.digest(file.read_bytes())!=provenance['part_sha256'].get(name) or
            file.stat().st_size!=provenance.get('part_sizes',{}).get(name) or
            part['offset']+file.stat().st_size>profile['flash_mb']*1024*1024):raise ValueError('bundle part checksum/capacity mismatch')
        basename=name[7:]
        original='bootloader/'+basename if basename=='bootloader.bin' else 'partition_table/'+basename if basename=='partition-table.bin' else basename
        if qualification.get('flash_offsets',{}).get(original)!=part['offset'] or qualification.get('part_sha256',{}).get(original)!=provenance['part_sha256'][name]:raise ValueError('unqualified part/offset')
        if basename=='partition-table.bin' and p.digest(file.read_bytes())!=entry['partition_sha256']:raise ValueError('unqualified partition')
        if basename=='universal-gnss-esp32.bin' and p.digest(file.read_bytes())!=entry['sha256']:raise ValueError('unqualified app')
        expected.add(name)
    if len(expected)!=11:raise ValueError('duplicate/missing bundle members')
    archive=directory/'bundle.zip'
    if p.digest(archive.read_bytes())!=entry['bundle_sha256']:raise ValueError('archive checksum mismatch')
    with zipfile.ZipFile(archive) as bundle:
        if set(bundle.namelist())!=expected or len(bundle.infolist())!=len(expected):raise ValueError('unexpected archive payload')
        for info in bundle.infolist():
            if info.compress_type!=zipfile.ZIP_STORED or info.file_size!=(directory/info.filename).stat().st_size:
                raise ValueError('unsupported/oversized archive member')
            if bundle.read(info)!=(directory/info.filename).read_bytes():raise ValueError('archive member mismatch')
    return entry,directory,expected|{'bundle.zip'}

def require_main(repo,source,tree,main_ref='origin/main'):
    env={**os.environ,'GIT_NO_REPLACE_OBJECTS':'1','GIT_NO_LAZY_FETCH':'1','GIT_TERMINAL_PROMPT':'0'}
    actual=subprocess.check_output(['git','-C',str(repo),'rev-parse',source+'^{tree}'],env=env,text=True).strip()
    if actual!=tree:raise ValueError('approved Git tree mismatch')
    if subprocess.run(['git','-C',str(repo),'merge-base','--is-ancestor',source,main_ref],env=env).returncode:
        raise ValueError('official catalogue source must be contained in main')

def require_source(repo,source,tree,publication):
    if publication=='main_approved':
        return require_main(repo,source,tree)
    if publication!='ci_preview':raise ValueError('source not authorized for staging')
    env={**os.environ,'GIT_NO_REPLACE_OBJECTS':'1','GIT_NO_LAZY_FETCH':'1','GIT_TERMINAL_PROMPT':'0'}
    if not re.fullmatch('[a-f0-9]{40}',source) or not re.fullmatch('[a-f0-9]{40}',tree):
        raise ValueError('exact development source/tree required')
    available=subprocess.run(['git','-C',str(repo),'cat-file','-e',source+'^{commit}'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    if available.returncode:
        subprocess.run(['git','-C',str(repo),'fetch','--no-tags','origin',source],env=env,check=True,timeout=90)
    if subprocess.check_output(['git','-C',str(repo),'cat-file','-t',source],env=env,text=True).strip()!='commit':raise ValueError('development source must be a commit')
    actual=subprocess.check_output(['git','-C',str(repo),'rev-parse',source+'^{tree}'],env=env,text=True).strip()
    if actual!=tree:raise ValueError('development source/tree mismatch')

def prepare(output,approvals,bundles=(),repo=ROOT):
    # Validate everything before creating the destination; no partial public catalogue.
    output=Path(output).resolve()
    if output.exists() or output.is_relative_to(Path(repo).resolve()):raise ValueError('new staging output outside source required')
    # Register validation also applies to an empty catalogue (no fake source approval).
    r.approval_register(approvals)
    if len(bundles)>512:raise ValueError('site catalogue capacity exceeded')
    entries=[];records=[];keys=set();total=0
    for root in bundles:
        entry,directory,files=inspect_bundle(root,approvals)
        row=next(row for row in approvals['sources'] if row['source_sha']==entry['source_sha'])
        require_source(repo,entry['source_sha'],row['source_tree'],row['publication'])
        if entry['build_key'] in keys:raise ValueError('duplicate firmware identity')
        total+=sum((directory/name).stat().st_size for name in files)
        if total>512*1024*1024:raise ValueError('local Pages mirror exceeds 512 MiB budget')
        keys.add(entry['build_key']);entries.append(entry);records.append((directory,files))
    output.mkdir(parents=True)
    for name in ('flasher.html','UG_logo.png'):shutil.copyfile(ROOT/'docs'/name,output/name)
    (output/'flasher').mkdir()
    for name in ('app.js','portable.js','provision.js','README.md'):shutil.copyfile(ROOT/'docs/flasher'/name,output/'flasher'/name)
    (output/'.nojekyll').write_text('')
    (output/'flasher/approved-sources.json').write_bytes(p.json_bytes(approvals))
    (output/'flasher/catalog.json').write_bytes(p.json_bytes({'schema':2,'images':sorted(entries,key=lambda e:e['build_key'])}))
    for directory,files in records:
        dest=output/'flasher/published'/directory.name;dest.mkdir(parents=True)
        for name in sorted(files):shutil.copyfile(directory/name,dest/name)
    cache_bust(output)
    return output

def cache_bust(site):
    """Validate local HTML resource references and address them by content hash."""
    import hashlib
    from html.parser import HTMLParser
    site=Path(site)
    class Resources(HTMLParser):
        def handle_starttag(self,tag,attrs):
            for name,value in attrs:
                if name not in ('src','href') or not value or ':' in value or value.startswith(('#','/')):
                    continue
                path=value.split('?')[0]
                if Path(path).suffix not in ('.js','.css','.png','.svg','.webp'):
                    continue
                file=(site/path).resolve(strict=True)
                if not file.is_relative_to(site.resolve()):raise ValueError('unsafe HTML resource')
                digest=hashlib.sha256(file.read_bytes()).hexdigest()
                self.text=self.text.replace('"'+value+'"','"'+path+'?v='+digest+'"')
    parser=Resources();parser.text=(site/'flasher.html').read_text()
    parser.feed(parser.text)
    (site/'flasher.html').write_text(parser.text)

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--approvals',type=Path,default=ROOT/'docs/flasher/approved-sources.json')
    parser.add_argument('--bundle',type=Path,action='append',default=[])
    args=parser.parse_args(argv)
    if os.getuid()==0:parser.error('run as the normal project user')
    try:prepare(args.output,p.load(args.approvals),args.bundle)
    except (ValueError,KeyError,TypeError,OSError,subprocess.SubprocessError,zipfile.BadZipFile) as error:parser.error(str(error))
    print('Local Pages staging prepared; no deployment or publication performed.');return 0
if __name__=='__main__':raise SystemExit(main())

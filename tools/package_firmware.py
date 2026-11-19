#!/usr/bin/env python3
"""Package locally qualified generic firmware. Never build, publish, deploy or flash."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import tempfile
import zipfile

ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location('ug_firmware_profiles',ROOT/'embedded/esp32/native/profiles.py')
profiles=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(profiles)
PARTS=('bootloader/bootloader.bin','partition_table/partition-table.bin','ota_data_initial.bin','universal-gnss-esp32.bin')

def unique(pairs):
    result={}
    for key,value in pairs:
        if key in result: raise ValueError('duplicate JSON key')
        result[key]=value
    return result

def load(path):
    return json.loads(Path(path).read_text(),object_pairs_hook=unique,
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError('non-finite JSON')))

def digest(data):return hashlib.sha256(data).hexdigest()
def json_bytes(value):return (json.dumps(value,sort_keys=True,indent=2)+'\n').encode()

def partitions(data):
    if not data or len(data)%32: raise ValueError('invalid partition table size')
    rows=[];checked=False
    for pos in range(0,len(data),32):
        row=data[pos:pos+32]
        if row[:2]==b'\xeb\xeb':
            if row[:16]!=b'\xeb\xeb'+b'\xff'*14 or row[16:]!=hashlib.md5(data[:pos],usedforsecurity=False).digest():
                raise ValueError('partition checksum mismatch')
            if data[pos+32:]!=b'\xff'*(len(data)-pos-32): raise ValueError('unexpected partition trailer')
            checked=True;break
        magic,kind,subtype,offset,size,label,flags=struct.unpack('<2sBBII16sI',row)
        if magic!=b'\xaa\x50' or flags or not size or offset%4096 or size%4096:
            raise ValueError('unsupported partition entry')
        label=label.split(b'\0',1)[0].decode('ascii')
        if any(p['label']==label for p in rows):raise ValueError('duplicate partition label')
        rows.append(dict(label=label,type=kind,subtype=subtype,offset=offset,size=size))
    if not rows or not checked:raise ValueError('partition table checksum required')
    ordered=sorted(rows,key=lambda p:p['offset'])
    if any(a['offset']+a['size']>b['offset'] for a,b in zip(ordered,ordered[1:])):
        raise ValueError('overlapping partitions')
    return rows

def package(build, verified, profile, source_sha, output, qualification):
    if not isinstance(verified,dict) or not isinstance(qualification,dict):raise ValueError('qualification objects required')
    profile=profiles.normalize(profile);key=profiles.build_key(source_sha,profile)
    pinned=load(ROOT/'embedded/esp32/migration/dependencies.json')['sdk']['image']
    if (qualification.get('source_type')!='commit' or qualification.get('source_commit')!=source_sha or
        qualification.get('source_object')!=source_sha or not isinstance(qualification.get('source_tree'),str) or not re.fullmatch('[a-f0-9]{40}',qualification.get('source_tree','')) or
        qualification.get('variant')!=profile['variant'] or qualification.get('sdk_image')!=pinned or
        qualification.get('build_and_verify')!='pass' or qualification.get('reproducibility')!='pass' or
        qualification.get('network_during_build_and_tests')!='disconnected_no_interfaces' or
        qualification.get('source_mounts') is not False or qualification.get('flash_or_publication') is not False or
        qualification.get('binary')!=verified or
        (profile['variant'].startswith('s3-gateway') and qualification.get('host_tests')!='pass')):
        raise ValueError('matching immutable isolated CI qualification required')
    if (verified.get('profile')!=profile['profile'] or verified.get('local_gnss') is not (profile['profile']=='gateway') or
        verified.get('radio_configuration')!='runtime_nvs_encrypted_unicast' or verified.get('portable_configuration_schema')!=1):
        raise ValueError('verified generic role/radio configuration mismatch')
    expected_output={'transport':'uart1-tx' if profile['output']=='uart' else 'usb-serial-jtag',
                     'tx_pin':profile.get('uart_tx'),'baud':profile.get('uart_baud')}
    if profile['profile']=='rover' and any(verified.get('rover_output',{}).get(k)!=v for k,v in expected_output.items()):
        raise ValueError('verified rover output mismatch')
    expected_transport = 'http' if profile['variant']=='s3-gateway-http' else 'https'
    if profile['profile']=='gateway' and verified.get('web_transport')!=expected_transport:
        raise ValueError('Web transport does not match the selected firmware variant')
    build=Path(build).resolve(strict=True)
    flash=load(build/'flasher_args.json');description=load(build/'project_description.json')
    if not isinstance(flash,dict) or not isinstance(description,dict):raise ValueError('IDF metadata objects required')
    chip='esp32s3' if profile['chip']=='ESP32-S3' else 'esp32c3'
    if description.get('target')!=chip or description.get('git_revision')!='v6.1' or description.get('project_name')!='universal-gnss-esp32':
        raise ValueError('unexpected IDF project/target/version')
    config_file=(build.parent/'sdkconfig').resolve(strict=True)
    if Path(description.get('config_file','')).resolve()!=config_file:raise ValueError('unexpected sdkconfig path')
    config=dict(line.split('=',1) for line in config_file.read_text().splitlines() if line.startswith('CONFIG_') and '=' in line)
    if (config.get('CONFIG_IDF_TARGET')!='"'+chip+'"' or
        config.get('CONFIG_ESPTOOLPY_FLASHSIZE')!='"'+str(profile['flash_mb'])+'MB"' or
        any(k in config for k in ('CONFIG_ESPNOW_RTCM_PEER_MAC','CONFIG_ESPNOW_RTCM_CHANNEL')) or
        (profile['psram_mb']==8 and any(config.get(k)!='y' for k in ('CONFIG_SPIRAM','CONFIG_SPIRAM_MODE_OCT','CONFIG_SPIRAM_TYPE_ESPPSRAM64'))) or
        (profile['psram_mb']==0 and config.get('CONFIG_SPIRAM')=='y')):
        raise ValueError('actual SDK hardware/RF configuration mismatch')
    files=flash.get('flash_files')
    if not isinstance(flash.get('extra_esptool_args'),dict) or not isinstance(flash.get('flash_settings'),dict):raise ValueError('IDF flash metadata required')
    if not isinstance(files,dict) or len(files)!=4 or not all(isinstance(v,str) for v in files.values()) or set(files.values())!=set(PARTS) or flash['extra_esptool_args'].get('chip')!=chip:
        raise ValueError('only the four actual IDF installation segments are supported')
    if flash.get('flash_settings',{}).get('flash_size')!=str(profile['flash_mb'])+'MB':raise ValueError('flash capacity mismatch')
    binaries={};offsets={}
    hashes=qualification.get('part_sha256')
    if not isinstance(hashes,dict) or set(hashes)!=set(PARTS):raise ValueError('independent part qualification missing')
    for offset,path in files.items():
        if not isinstance(offset,str) or not re.fullmatch('0x[0-9a-fA-F]+|0|[1-9][0-9]*',offset):raise ValueError('invalid offset')
        source=(build/path).resolve(strict=True)
        if not source.is_relative_to(build):raise ValueError('flash path escapes build')
        data=source.read_bytes();number=int(offset,0)
        if not data or number+len(data)>profile['flash_mb']*1024*1024 or digest(data)!=hashes[path]:
            raise ValueError('part integrity/capacity mismatch')
        binaries[path]=data;offsets[path]=number
    if offsets!=qualification.get('flash_offsets'):raise ValueError('qualified installation offsets mismatch')
    app=binaries[PARTS[3]]
    if digest(app)!=verified.get('sha256') or len(app)!=verified.get('app_bytes'):raise ValueError('application verification mismatch')
    if app[0]!=0xe9 or binaries[PARTS[0]][0]!=0xe9:raise ValueError('ESP image magic missing')
    table=partitions(binaries[PARTS[1]]); slots=[p for p in table if p['type']==0]
    actual_slots=[{k:p[k] for k in ('label','offset','size')} for p in slots]
    if actual_slots!=verified.get('app_slots') or len(slots)!=2 or any(len(app)>p['size'] for p in slots):
        raise ValueError('qualified app partitions mismatch')
    ota=[p for p in table if p['type']==1 and p['subtype']==0]
    if (len(ota)!=1 or offsets[PARTS[3]]!=slots[0]['offset'] or offsets[PARTS[2]]!=ota[0]['offset'] or
        len(binaries[PARTS[2]])>ota[0]['size'] or offsets[PARTS[0]]!=0 or
        offsets[PARTS[1]]!=int(config.get('CONFIG_PARTITION_TABLE_OFFSET','-1'),0) or
        any(p['offset']+p['size']>profile['flash_mb']*1024*1024 for p in table)):
        raise ValueError('actual installation offsets/layout mismatch')
    spans=sorted((offsets[p],offsets[p]+len(binaries[p])) for p in PARTS)
    if any(a[1]>b[0] for a,b in zip(spans,spans[1:])):raise ValueError('overlapping flash segments')
    # No segment may overwrite another partition, particularly NVS.
    for path in PARTS:
        start,end=offsets[path],offsets[path]+len(binaries[path])
        permitted=slots[0]['label'] if path==PARTS[3] else ota[0]['label'] if path==PARTS[2] else None
        if any(start<p['offset']+p['size'] and p['offset']<end and p['label']!=permitted for p in table):
            raise ValueError('installation segment overlaps protected partition')
    output=Path(output).resolve();dest=output/'published'/key
    if dest.exists() or dest.is_symlink():raise ValueError('refuse bundle overwrite')
    dest.parent.mkdir(parents=True,exist_ok=True)
    names={p:f'{offsets[p]:06x}-{Path(p).name}' for p in PARTS}
    manifest={'name':'Universal GNSS ALPHA '+profile['variant'],'version':'alpha-'+source_sha[:12]+'-'+key,
              'new_install_prompt_erase':True,'builds':[{'chipFamily':profile['chip'],
              'parts':[{'path':names[p],'offset':offsets[p]} for p in sorted(PARTS,key=lambda x:offsets[x])]}]}
    entry={'profile':profile,'build_key':key,'manifest':f'flasher/published/{key}/manifest.json',
           'sha256':digest(app),'source_sha':source_sha,'sdk_image':pinned,
           'partition_sha256':digest(binaries[PARTS[1]]),'manifest_sha256':digest(json_bytes(manifest)),
           'qualification':'software_only','install_mode':'full','nvs_preservation':'not_qualified'}
    if 'firmware_identity' in verified:
        identity=verified['firmware_identity']
        if (identity.get('source_sha')!=source_sha or identity.get('source_tree')!=qualification['source_tree'] or
                identity.get('modified') is not False or identity.get('esp_idf')!='6.1' or
                identity.get('profile')!=profile['profile'] or identity.get('hardware_variant')!=profile['board'] or
                identity.get('web_transport')!=(expected_transport if profile['profile']=='gateway' else 'none')):
            raise ValueError('modified/mismatched firmware provenance cannot be published')
        expected_boot='automatic_unique_nvs' if profile['profile']=='gateway' and expected_transport=='https' else 'not_applicable'
        if verified.get('first_boot_tls')!=expected_boot:
            raise ValueError('matching first-boot TLS qualification required')
        entry['firmware_identity']=identity
        entry['first_boot_tls']=verified.get('first_boot_tls','not_applicable')
    provenance={**entry,'part_sha256':{names[p]:hashes[p] for p in PARTS},'part_sizes':{names[p]:len(binaries[p]) for p in PARTS}}
    with tempfile.TemporaryDirectory(dir=dest.parent,prefix='.package-') as temp:
        staging=Path(temp)/key;staging.mkdir()
        for path,data in binaries.items():(staging/names[path]).write_bytes(data)
        (staging/'manifest.json').write_bytes(json_bytes(manifest))
        (staging/'provenance.json').write_bytes(json_bytes(provenance))
        (staging/'qualification.json').write_bytes(json_bytes(qualification))
        shutil.copyfile(ROOT/'embedded/esp32/licenses/legacy-GPL-3.0.txt',staging/'LICENSE.txt')
        shutil.copyfile(ROOT/'embedded/esp32/migration/dependencies.json',staging/'DEPENDENCIES.json')
        shutil.copyfile(ROOT/'LICENSE',staging/'LICENSE-UG-LGPL.txt')
        (staging/'SOURCE.txt').write_text('Combined firmware: GPLv3; portable UG libraries retain LGPLv3.\nSource: https://github.com/Pepeuch/universal-gnss/tree/'+source_sha+'\nHardware qualification pending; full installation only; NVS preservation unqualified.\n')
        with zipfile.ZipFile(staging/'bundle.zip','w',compression=zipfile.ZIP_STORED) as archive:
            for file in sorted(staging.iterdir()):
                if file.name=='bundle.zip':continue
                info=zipfile.ZipInfo(file.name,date_time=(1980,1,1,0,0,0));info.external_attr=0o100644<<16;info.create_system=3
                archive.writestr(info,file.read_bytes())
        entry['bundle']=f'flasher/published/{key}/bundle.zip';entry['bundle_sha256']=digest((staging/'bundle.zip').read_bytes())
        os.rename(staging,dest)
    return entry

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','verified','profile-json','output','qualification'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--source-sha',required=True);args=p.parse_args(argv)
    try:entry=package(args.build,load(args.verified),load(args.profile_json),args.source_sha,args.output,load(args.qualification))
    except (ValueError,KeyError,TypeError,OSError,struct.error) as error:p.error(str(error))
    print(json.dumps({'catalog_entry':entry},sort_keys=True));return 0
if __name__=='__main__':raise SystemExit(main())

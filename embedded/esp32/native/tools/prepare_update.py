#!/usr/bin/env python3
"""Offline S3 application-update plan. No device access, flashing or publication.

Requires a verified full backup and the exact previously installed application.
The plan is review evidence, NOT an ESP Web Tools manifest or a qualification.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location('update_package', ROOT/'tools/package_firmware.py')
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)


def plan(backup, backup_sha256, current_app, new_app, table, bootloader):
    if len(backup)!=16*1024*1024 or p.digest(backup)!=backup_sha256:
        raise ValueError('verified 16 MiB S3 backup required')
    if backup[0x8000:0x8000+len(table)]!=table or backup[:len(bootloader)]!=bootloader:
        raise ValueError('bootloader/partition change is not an application update')
    rows=p.partitions(table)
    slots=sorted((row for row in rows if row['type']==0),key=lambda row:row['subtype'])
    nvs=[row for row in rows if row['label']=='nvs' and row['type']==1 and row['subtype']==2]
    ota=[row for row in rows if row['type']==1 and row['subtype']==0]
    if (len(slots)!=2 or [row['subtype'] for row in slots]!=[16,17] or len(nvs)!=1 or
            len(ota)!=1 or ota[0]['size']!=8192):
        raise ValueError('unsupported S3 layout')
    entries=[]
    for copy in (0,4096):
        raw=backup[ota[0]['offset']+copy:ota[0]['offset']+copy+32]
        if raw==b'\xff'*32:
            continue
        seq,=struct.unpack_from('<I',raw)
        state,crc=struct.unpack_from('<II',raw,24)
        # IDF6.1 bootloader_common_ota_select_crc uses ROM CRC32_LE(UINT32_MAX).
        expected=zlib.crc32(raw[:4], 0xffffffff)
        if seq in (0,0xffffffff) or crc!=expected or state not in (2,0xffffffff):
            raise ValueError('pending/invalid OTA state; inspect boot selection before update')
        entries.append(seq)
    selected=slots[(max(entries)-1)%2] if entries else slots[0]
    offset=selected['offset']
    if any(not image or image[0]!=0xe9 or len(image)>selected['size'] for image in (current_app,new_app)):
        raise ValueError('application image/slot mismatch')
    # ESP image chip ID and app description (not a full image-signature verifier).
    for image in (current_app,new_app):
        if (len(image)<64 or struct.unpack_from('<H',image,12)[0]!=9 or
                struct.unpack_from('<I',image,32)[0]!=0xabcd5432):
            raise ValueError('recognized ESP32-S3 application required')
    if backup[offset:offset+len(current_app)]!=current_app:
        raise ValueError('installed application does not match the archived reference')
    if offset < nvs[0]['offset']+nvs[0]['size'] and offset+len(new_app)>nvs[0]['offset']:
        raise ValueError('update overlaps NVS')
    return {'schema':1,'mode':'application_only','hardware':'waveshare-s3-eth',
            'backup_sha256':backup_sha256,'current_app_sha256':p.digest(current_app),
            'app_sha256':p.digest(new_app),'partition_sha256':p.digest(table),
            'write':{'offset':offset,'size':len(new_app)},
            'nvs_preservation':'software_plan_only_hardware_required',
            'browser_update_enabled':False,'power_loss_atomic':False,
            'requires':'approved bundle, compatible settings schema, private backup, hardware acceptance'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for argument in ('backup','current-app','new-app','partition-table','bootloader','output'):
        parser.add_argument('--'+argument,type=Path,required=True)
    parser.add_argument('--backup-sha256',required=True)
    args=parser.parse_args()
    result=plan(args.backup.read_bytes(),args.backup_sha256,args.current_app.read_bytes(),
                args.new_app.read_bytes(),args.partition_table.read_bytes(),args.bootloader.read_bytes())
    # Exclusive creation; never overwrite existing review evidence.
    with args.output.open('x') as output:
        json.dump(result,output,sort_keys=True,indent=2)
        output.write('\n')
    print('Application-only plan prepared; hardware qualification required; no device written.')


if __name__=='__main__':
    main()

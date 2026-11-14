#!/usr/bin/env python3
"""Explicit USB maintenance client. No port discovery, reset, flash or radio action.

Reset the identified rover manually then invoke within its eight-second boot
window. Commands are local USB only, before RTCM starts. Pair secrets never go to
stdout; an optional private S3 ticket is created with O_EXCL and mode 0600.
"""
import argparse
import json
import os
from pathlib import Path
import re
import secrets
import select
import termios
import time
import tty
import importlib.util
_spec=importlib.util.spec_from_file_location('ug_portable_config',Path(__file__).with_name('portable_config.py'))
portable=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(portable)


def exchange(fd, command, timeout=3):
    data=(command+'\n').encode('ascii'); end=time.monotonic()+timeout
    while data:
        if time.monotonic()>=end: raise TimeoutError('USB command write timeout')
        if select.select([], [fd], [], max(0,end-time.monotonic()))[1]:
            try: count=os.write(fd,data)
            except BlockingIOError: continue
            if count<=0: raise OSError('USB write made no progress')
            data=data[count:]
    line=bytearray(); skipped=0
    while time.monotonic()<end:
        if not select.select([fd],[],[],max(0,end-time.monotonic()))[0]: break
        try: byte=os.read(fd,1)
        except BlockingIOError: continue
        if not byte: raise OSError('USB disconnected')
        if byte==b'\n':
            if line.startswith(b'UGCFG/1 '):
                reply=json.loads(line[8:])
                if not isinstance(reply,dict): raise ValueError('invalid control reply')
                if 'error' in reply: raise ValueError('rover rejected command: '+str(reply['error']))
                return reply
            skipped+=len(line)+1; line.clear()
        else:
            line.extend(byte)
            if len(line)>512: skipped+=len(line); line.clear()
        if skipped>65536: raise ValueError('control response budget exceeded')
    raise TimeoutError('No UGCFG/1 reply; reset the identified C3 and retry within 8 seconds')


def private_write(path, data):
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(fd,'w') as stream:
        stream.write(data); stream.flush(); os.fsync(stream.fileno())


def key_text(path):
    info=path.stat()
    if info.st_mode & 0o077: raise ValueError('key file must have mode 0600')
    key=path.read_text().strip()
    if not re.fullmatch('[0-9a-fA-F]{32}',key) or not int(key,16): raise ValueError('invalid private key file')
    return key


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port',help='explicit identified USB port, preferably by-id')
    parser.add_argument('--set-peer',help='base Wi-Fi STA MAC, never the USB serial identifier')
    parser.add_argument('--channel',type=int,default=1)
    parser.add_argument('--key-file',type=Path)
    parser.add_argument('--new-key-file',type=Path)
    parser.add_argument('--s3-ticket',type=Path,help='create private import JSON for the S3 WebUI')
    parser.add_argument('--clear',action='store_true')
    parser.add_argument('--run',action='store_true',help='explicitly close control mode and allow RTCM output')
    parser.add_argument('--portable-file',type=Path,help='check private-free JSON locally then validate in rover maintenance')
    parser.add_argument('--apply-portable',action='store_true',help='explicitly persist the checked portable configuration')
    parser.add_argument('--export-portable',type=Path,help='new portable JSON file; no pairing/identity fields')
    args=parser.parse_args(argv)
    if args.apply_portable and not args.portable_file:parser.error('apply requires portable-file')
    try:config=portable.load(args.portable_file,'supermini-c3') if args.portable_file else None
    except (ValueError,OSError) as error:parser.error(str(error))
    if args.clear and args.set_peer: parser.error('clear and set-peer conflict')
    if args.new_key_file and args.key_file: parser.error('select an existing or a new key file')
    if args.set_peer:
        if not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}',args.set_peer): parser.error('invalid peer MAC')
        peer=bytes.fromhex(args.set_peer.replace(':',''))
        if peer[0]&1 or not any(peer) or not 1<=args.channel<=11: parser.error('invalid unicast peer or channel')
        if not (args.key_file or args.new_key_file): parser.error('pairing requires a private key file')
    elif args.key_file or args.new_key_file or args.s3_ticket or args.channel!=1:
        parser.error('key and ticket options require set-peer')
    if args.new_key_file: private_write(args.new_key_file,secrets.token_hex(16)+'\n')
    key=key_text(args.key_file or args.new_key_file) if args.set_peer else None
    fd=os.open(args.port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
    try:
        tty.setraw(fd)
        attrs=termios.tcgetattr(fd); attrs[2]&=~termios.HUPCL; termios.tcsetattr(fd,termios.TCSANOW,attrs)
        nonce=secrets.token_hex(8)
        report=exchange(fd,'UGCFG/1 OPEN '+nonce)
        if report.get('nonce')!=nonce: raise ValueError('untrusted control handshake')
        if report.get('mode')!='control': raise ValueError('not a rover maintenance session')
        if args.set_peer:
            report=exchange(fd,f'UGCFG/1 SET {args.set_peer} {args.channel} {key} CONFIRM_PAIR')
            if args.s3_ticket:
                ticket={'confirm':'PAIR_ESPNOW_ROVER','peer':report['sta_mac'],'channel':args.channel,'key':key}
                private_write(args.s3_ticket,json.dumps(ticket,indent=2)+'\n')
        if args.clear: report=exchange(fd,'UGCFG/1 CLEAR CONFIRM_UNPAIR')
        if config is not None:
            encoded=json.dumps(config,separators=(',',':'))
            report=exchange(fd,'UGCFG/1 CONFIG_CHECK '+encoded)
            portable.validate(report.get('portable'),'supermini-c3')
            if args.apply_portable:report=exchange(fd,'UGCFG/1 CONFIG_APPLY CONFIRM '+encoded)
        if args.export_portable:
            report=exchange(fd,'UGCFG/1 CONFIG_GET')
            value=portable.validate(report.get('portable'),'supermini-c3')
            private_write(args.export_portable,json.dumps(value,indent=2)+'\n')
        if args.run: report=exchange(fd,'UGCFG/1 RUN CONFIRM_RTCM')
        print(json.dumps(report,sort_keys=True))
    finally: os.close(fd)

if __name__=='__main__': main()

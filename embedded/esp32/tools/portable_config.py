#!/usr/bin/env python3
"""Strict private-free portable configuration v1. Offline validation; no network/storage."""
import argparse
import json
from pathlib import Path
PINS={'waveshare-s3-eth':(1,2,15,16,17,18,38,39,40,41,42,43,44,47,48),'supermini-c3':(4,5,6,7,10)}
MODELS=('none','auto','NMEA','UM980','UM981','UM982','ZED-F9P')
def defaults(board):
    if board not in PINS:raise ValueError('unsupported board')
    s3=board=='waveshare-s3-eth'
    return dict(format='ug-esp32-portable',schema=1,board=board,profile='gateway' if s3 else 'rover',ethernet='w5500-onboard' if s3 else 'none',output='gnss' if s3 else 'usb',uart_tx=-1,uart_rx=-1,uart_baud=115200,gnss_model='auto' if s3 else 'none',radio_channel=1)
def validate(value,board=None):
    if not isinstance(value,dict) or type(value.get('board')) is not str:raise ValueError('portable object required')
    d=defaults(value['board']);s3=d['profile']=='gateway'
    if set(value)!=set(d) or any(type(value[k]) is not type(d[k]) for k in d):raise ValueError('unknown/missing/invalid portable field')
    if board is not None and value['board']!=board:raise ValueError('incompatible board')
    if any(value[k]!=d[k] for k in ('format','schema','profile','ethernet')) or not 1<=value['radio_channel']<=11:raise ValueError('incompatible schema/profile')
    tx,rx,baud=value['uart_tx'],value['uart_rx'],value['uart_baud']
    if s3:
        if value['output']!='gnss' or value['gnss_model'] not in MODELS[1:] or not 9600<=baud<=921600 or not ((tx==rx==-1) or (tx in PINS[value['board']] and rx in PINS[value['board']] and tx!=rx)):raise ValueError('GNSS GPIO conflict or incompatible baud')
    elif value['gnss_model']!='none' or rx!=-1 or not (value['output']=='usb' and tx==-1 and baud==115200 or value['output']=='uart' and tx in PINS[value['board']] and baud in (115200,230400,460800,921600)):raise ValueError('incompatible rover output')
    return {k:value[k] for k in d}
def parse(text,board=None):
    if len(text.encode('utf-8'))>512:raise ValueError('portable byte limit')
    # Firmware intentionally accepts ASCII enum strings without JSON escapes only.
    if '\\' in text or any(ord(c)>127 for c in text):raise ValueError('portable enums require plain ASCII')
    def unique(pairs):
        out={}
        for key,value in pairs:
            if key in out:raise ValueError('duplicate portable key')
            out[key]=value
        return out
    def integer(s):
        if s=='-0':raise ValueError('noncanonical integer')
        return int(s)
    return validate(json.loads(text,object_pairs_hook=unique,parse_int=integer,parse_float=lambda _:None,parse_constant=lambda _:None),board)
def load(path,board=None):
    with Path(path).open('rb') as stream:data=stream.read(513)
    return parse(data.decode('ascii'),board)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('file',type=Path);args=p.parse_args()
    try:value=load(args.file)
    except (OSError,ValueError) as error:p.error(str(error))
    print(json.dumps(value,sort_keys=True))

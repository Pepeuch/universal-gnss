#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-only
"""Controlled LAN RTCM fixture; synthetic payloads, never connect it to a GNSS input.

Config and control files are operator-private. Only the configured S3 address is
accepted. Control defaults to zero frames; no firmware, radio or NVS operations.
Run on the Linux host used for USB capture: payload clocks are CLOCK_MONOTONIC_NS
from that host, not ESP timestamps. Requires the existing check_rtcm_usb.py module.
"""
import argparse
import asyncio
import base64
import importlib.util
import json
from pathlib import Path
import struct
import time

MARKER=b'UGRB'


def next_deadline(previous, now, period):
    """Advance the fixed schedule, skipping missed slots without a catch-up burst."""
    following=previous+period
    missed=0 if following>now else int((now-following)/period)+1
    return following+missed*period,missed


async def send_source(writers, frame, stats):
    for writer in list(writers):
        writer.write(frame)
        try:
            await asyncio.wait_for(writer.drain(),1)
        except TimeoutError:
            stats['source_write_timeouts']+=1
            raise
    stats['source_frames']+=1
    stats['source_bytes']+=len(frame)


def make_frame(crc, message, sequence, payload_size):
    if message not in (1005,1077,1087) or not 22<=payload_size<=1023:
        raise ValueError('unsupported synthetic frame')
    body=bytearray(payload_size)
    body[:2]=bytes((message>>4,(message&15)<<4))
    body[2:22]=MARKER+struct.pack('>QQ',sequence,time.monotonic_ns())
    raw=bytes((0xd3,payload_size>>8,payload_size&255))+body
    return raw+crc(raw).to_bytes(3,'big')


def identity(frame):
    if len(frame)<28 or frame[5:9]!=MARKER:raise ValueError('not a resource fixture frame')
    sequence,sent=struct.unpack('>QQ',frame[9:25])
    return ((frame[3]<<4)|(frame[4]>>4)),sequence,sent


async def serve(config_path,control_path,output,checker):
    config=json.loads(config_path.read_text())
    token=config['test_token']
    if len(token)<12:raise ValueError('unique test-only credential required')
    peer=config['peer'];bind=config['bind'];base=config['port']
    if not 1024<=base<=65530:raise ValueError('unprivileged consecutive ports required')
    stats={'source_connections':0,'source_frames':0,'source_bytes':0,'rejected':0,
           'source_write_timeouts':0,'source_schedule_misses':0,'failure':None,
           'first_source_ns':None,'last_source_ns':None,
           'publishers':[{'connections':0,'frames':0,'bytes':0,'crc_errors':0,'types':{},'first_sequence':None,'last_sequence':None,'latency_us':[]} for _ in range(5)]}
    control={};source_writers=set();sequence=0

    async def connection(reader,writer,slot):
        nonlocal stats
        try:
            if writer.get_extra_info('peername')[0]!=peer:
                stats['rejected']+=1;return
            header=await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'),6)
            if len(header)>4096:raise ValueError('header budget')
            if slot<0:
                authorization=base64.b64encode(('ug-resource-test:'+token).encode())
                if not header.startswith(b'GET /UGRESOURCE ') or b'Authorization: Basic '+authorization not in header:
                    raise ValueError('source credentials')
                writer.write(b'ICY 200 OK\r\n\r\n');await writer.drain()
                stats['source_connections']+=1;source_writers.add(writer)
                while await reader.read(4096):
                    pass
            else:
                expected=('SOURCE '+token+' /UGRESOURCE'+str(slot)+'\r\n').encode()
                if not header.startswith(expected):raise ValueError('publisher credentials')
                writer.write(b'ICY 200 OK\r\n');await writer.drain()
                sample=stats['publishers'][slot];sample['connections']+=1
                parser=checker.RtcmReader()
                while True:
                    data=await reader.read(4096)
                    if not data:break
                    try:frames=parser.feed(data)
                    except ValueError:sample['crc_errors']+=1;break
                    for frame in frames:
                        message,seq,sent=identity(frame)
                        sample['frames']+=1;sample['bytes']+=len(frame)
                        sample['types'][str(message)]=sample['types'].get(str(message),0)+1
                        if sample['first_sequence'] is None:sample['first_sequence']=seq
                        sample['last_sequence']=seq
                        if len(sample['latency_us'])<4096:sample['latency_us'].append((time.monotonic_ns()-sent)/1000)
        except (ValueError,TimeoutError,asyncio.IncompleteReadError,ConnectionError,OSError):
            stats['rejected']+=1
        finally:
            source_writers.discard(writer);writer.close()
            try:await writer.wait_closed()
            except (OSError,ConnectionError):pass

    listeners=[]
    for slot in range(-1,5):
        listeners.append(await asyncio.start_server(lambda r,w,s=slot:connection(r,w,s),bind,base+slot+1,limit=4096))
    until=time.monotonic()+900
    next_frame=time.monotonic();next_snapshot=0
    try:
        while time.monotonic()<until:
            if control_path.exists():control=json.loads(control_path.read_text())
            if control.get('stop'):break
            rate=control.get('frames_per_second',0);size=control.get('payload_size',32)
            if not isinstance(rate,(int,float)) or not 0<=rate<=100:raise ValueError('rate budget')
            if rate and source_writers and time.monotonic()>=next_frame:
                message=(1005,1077,1087)[sequence%3]
                frame=make_frame(checker.crc24q,message,sequence,size);sequence+=1
                await send_source(source_writers,frame,stats)
                sent=time.monotonic_ns()
                if stats['first_source_ns'] is None:stats['first_source_ns']=sent
                stats['last_source_ns']=sent
                next_frame,missed=next_deadline(next_frame,time.monotonic(),1/rate)
                stats['source_schedule_misses']+=missed
            elif not rate or not source_writers:
                next_frame=time.monotonic()
            if time.monotonic()>=next_snapshot:
                temporary=output.with_suffix('.tmp');temporary.write_text(json.dumps(stats));temporary.replace(output)
                next_snapshot=time.monotonic()+.5
            # Wake at the next frame deadline, rather than quantizing e.g. 45 Hz
            # to a 10 ms polling grid. Do not claim the requested rate as achieved.
            await asyncio.sleep(min(.01,max(0,next_frame-time.monotonic())) if rate and source_writers else .01)
    except Exception as error:
        stats['failure']=type(error).__name__
        raise
    finally:
        for server in listeners:server.close()
        for writer in source_writers:writer.close()
        output.write_text(json.dumps(stats,indent=2)+'\n')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',type=Path,required=True)
    parser.add_argument('--control',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--checker',type=Path,required=True)
    args=parser.parse_args()
    spec=importlib.util.spec_from_file_location('rtcm_checker',args.checker)
    checker=importlib.util.module_from_spec(spec);spec.loader.exec_module(checker)
    asyncio.run(serve(args.config,args.control,args.output,checker))

if __name__=='__main__':main()

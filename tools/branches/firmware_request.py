#!/usr/bin/env python3
"""Validate a maintainer-approved request; --execute invokes the real isolated build.

No issue text is executed. No public event triggers this program, no publication
is performed and no token is embedded in the portal. Approval data belongs to the
trusted default-branch checkout, not the requested source or request payload.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
SPEC=importlib.util.spec_from_file_location('firmware_packager',ROOT/'tools/package_firmware.py')
p=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(p)

def validate(source,variant,tx,baud,approvals):
    if not isinstance(source,str) or not re.fullmatch('[a-f0-9]{40}',source):raise ValueError('full source commit required')
    if type(variant) is not str or variant not in p.profiles.VARIANTS:raise ValueError('unsupported catalogue variant')
    if variant!='c3-rover-uart' and (tx!=4 or baud!=115200):raise ValueError('UART overrides require UART rover')
    profile=p.profiles.from_variant(variant,tx,baud)
    sources=approval_register(approvals)
    if source not in sources:raise ValueError('source is not approved; request maintainer review first')
    return {'source_sha':source,'source_tree':sources[source]['source_tree'],
            'profile':profile,'build_key':p.profiles.build_key(source,profile),'publication':sources[source]['publication']}

def approval_register(approvals):
    if not isinstance(approvals,dict) or set(approvals)!={'schema','sources'} or type(approvals['schema']) is not int or approvals['schema']!=1 or not isinstance(approvals['sources'],list) or len(approvals['sources'])>128:
        raise ValueError('invalid trusted approval register')
    sources={}
    for row in approvals['sources']:
        if (not isinstance(row,dict) or set(row)!={'source_sha','source_tree','review','publication'} or
            not isinstance(row['source_sha'],str) or not re.fullmatch('[a-f0-9]{40}',row['source_sha']) or
            not isinstance(row['source_tree'],str) or not re.fullmatch('[a-f0-9]{40}',row['source_tree']) or
            not isinstance(row['review'],str) or not 1<=len(row['review'])<=256 or
            row['publication'] not in ('not_authorized','main_approved','ci_preview') or row['source_sha'] in sources):
            raise ValueError('invalid or duplicate approval entry')
        sources[row['source_sha']]=row
    return sources

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-sha',required=True)
    parser.add_argument('--variant',choices=p.profiles.VARIANTS,required=True)
    parser.add_argument('--uart-tx',type=int,choices=(4,5,6,7,10),default=4)
    parser.add_argument('--uart-baud',type=int,choices=(115200,230400,460800,921600),default=115200)
    parser.add_argument('--approvals',type=Path,default=ROOT/'docs/flasher/approved-sources.json')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--execute',action='store_true')
    args=parser.parse_args(argv)
    if os.getuid()==0:parser.error('run as the normal project user')
    try:request=validate(args.source_sha,args.variant,args.uart_tx,args.uart_baud,p.load(args.approvals))
    except (ValueError,KeyError,TypeError,OSError) as error:parser.error(str(error))
    if not args.execute:
        print(json.dumps({**request,'build_started':False},sort_keys=True));return 0
    # SHA is validated/approved before a fetch. No branch is updated or pushed.
    env={**os.environ,'GIT_NO_REPLACE_OBJECTS':'1','GIT_NO_LAZY_FETCH':'1','GIT_TERMINAL_PROMPT':'0'}
    try:
        subprocess.run(['git','-C',str(ROOT),'fetch','--no-tags','origin',args.source_sha],env=env,check=True,timeout=120)
        kind=subprocess.check_output(['git','-C',str(ROOT),'cat-file','-t',args.source_sha],env=env,text=True).strip()
        tree=subprocess.check_output(['git','-C',str(ROOT),'rev-parse',args.source_sha+'^{tree}'],env=env,text=True).strip()
        if kind!='commit' or tree!=request['source_tree']:raise ValueError('approved source/tree mismatch')
        command=[sys.executable,str(ROOT/'embedded/esp32/native/ci.py'),'--source-ref',args.source_sha,
                 '--variant',args.variant,'--bundle-output',str(args.output)]
        if args.variant=='c3-rover-uart':command+=['--uart-tx',str(args.uart_tx),'--uart-baud',str(args.uart_baud)]
        subprocess.run(command,env=env,check=True)  # CI owns command timeouts and graceful cleanup.
    except (ValueError,OSError,subprocess.SubprocessError) as error:parser.error(str(error))
    return 0
if __name__=='__main__':raise SystemExit(main())

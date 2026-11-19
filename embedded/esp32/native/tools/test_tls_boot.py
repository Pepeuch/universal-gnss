#!/usr/bin/env python3
"""Run real SDK TLS/NVS boot code in QEMU, never on a physical serial port."""
import argparse
import json
import os
from pathlib import Path
import selectors
import re
import socket
import subprocess
import tempfile
import time


def boot(flash, phase, build, timeout=45):
    with socket.socket() as address:
        address.bind(('127.0.0.1',0));port=address.getsockname()[1]
    command=['qemu-system-xtensa','-nographic','-monitor','none','-serial','stdio','-nic','none',
             '-machine','esp32s3','-m','8M','-global','driver=ssi_psram,property=is_octal,value=true',
             '-drive','file='+str(flash)+',if=mtd,format=raw', '-gdb','tcp:127.0.0.1:'+str(port)]
    process=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    output=bytearray()
    expected=0x55475400 | phase
    elf=build/'ug-tls-boot-test.elf'
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout,selectors.EVENT_READ)
            until=time.monotonic()+timeout
            while time.monotonic()<until:
                for key,_ in selector.select(min(1,max(0,until-time.monotonic()))):
                    part=os.read(key.fileobj.fileno(),65536)
                    output.extend(part)
                    if len(output)>2*1024*1024:raise RuntimeError('QEMU output limit exceeded')
                # The S3 UART backend is not usable with this pinned QEMU.
                # Read one explicit completion word, never arbitrary memory.
                probe=subprocess.run(['xtensa-esp32s3-elf-gdb','-batch','-nx',str(elf),
                    '-ex','target remote 127.0.0.1:'+str(port),
                    '-ex','printf "UG_QEMU_RESULT=%u\\n", ug_tls_test_result',
                    '-ex','detach'],capture_output=True,text=True,timeout=10)
                found=re.search(r'UG_QEMU_RESULT=([0-9]+)',probe.stdout)
                if probe.returncode==0 and found and int(found[1])==expected:
                    return 'UG_TLS_BOOT_TEST '+('INTERRUPTION_READY' if phase==8 else 'PASS')+' phase='+str(phase)+' via isolated GDB result word'
                if process.poll() is not None:break
        raise RuntimeError('TLS QEMU boot failed/timeout phase='+str(phase)+'\n'+output.decode(errors='replace'))
    finally:
        if process.poll() is None:process.terminate()
        try:process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill();process.wait(timeout=5)
        process.stdout.close()


def run(build, output):
    build=Path(build).resolve(strict=True)
    args=json.loads((build/'flasher_args.json').read_text())
    start=time.monotonic()
    summaries=[]
    # Ephemeral emulated NVS contains test-only generated keys. It is never
    # retained as an Actions artifact or written into the repository.
    with tempfile.TemporaryDirectory(prefix='ug-tls-qemu-') as temp:
        directory=Path(temp)
        directory.chmod(0o700)
        flash=directory/'flash.bin'
        with flash.open('xb') as stream:
            stream.write(b'\xff'*(16*1024*1024))
        flash.chmod(0o600)
        with flash.open('r+b') as stream:
            for offset,name in args['flash_files'].items():
                stream.seek(int(offset,0));stream.write((build/name).read_bytes())
        for phase in range(10):
            logs=boot(flash,phase,build)
            lines=[line for line in logs.splitlines() if 'UG_TLS_BOOT_TEST' in line or 'UG_TLS:' in line]
            summaries.append({'phase':phase,'result':'interrupted_before_commit' if phase==8 else 'pass','emulator_diagnostics':lines})
    result={'sdk':'6.1','target':'esp32s3','phases':summaries,'elapsed_host_s':time.monotonic()-start,
            'hardware_qualified':False,'external_network_or_device_access':False,'debugger_transport':'loopback_only_in_isolated_sdk'}
    Path(output).write_text(json.dumps(result,sort_keys=True,indent=2)+'\n')
    print('PASS real SDK TLS/NVS in QEMU: 10 starts including interruption; hardware qualification remains required.')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    run(args.build,args.output)


if __name__=='__main__':main()

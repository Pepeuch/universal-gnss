#!/usr/bin/env python3
"""Compare an immutable baseline and current GNSS buffers; no flash/Git mutations.

Linux/glibc GCC/Clang host-only allocation benchmark. Output must be outside the
repository and not already exist. Both engines use the same current harness and
baseline fixtures. Memory uses operator new counts/requested bytes and glibc
malloc_usable_size; timing runs disable this instrumentation and canonical traces.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import shutil
import statistics
import subprocess

ROOT=Path(__file__).resolve().parents[3]
PARTS=('core/gnss_core','protocols/gnss_protocols','core/gnss_transport','core/gnss_driver','testdata')
def run(args,log):
    before=resource.getrusage(resource.RUSAGE_CHILDREN)
    with log.open('w') as stream:subprocess.run(args,stdout=stream,stderr=subprocess.STDOUT,check=True)
    after=resource.getrusage(resource.RUSAGE_CHILDREN)
    return {'user_s':after.ru_utime-before.ru_utime,'system_s':after.ru_stime-before.ru_stime}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',required=True);p.add_argument('--output',required=True,type=Path);p.add_argument('--runs',type=int,default=7);a=p.parse_args()
    if os.getuid()==0:raise ValueError('run as the normal project user')
    if a.runs<3:raise ValueError('at least three independent paired runs required')
    output=a.output.resolve()
    if output==ROOT or ROOT in output.parents or output.exists():raise ValueError('output must be a new directory outside the repository')
    output.mkdir();baseline=output/'baseline';baseline.mkdir()
    base=subprocess.check_output(['git','rev-parse','--verify',a.base+'^{commit}'],cwd=ROOT,text=True).strip()
    archive=subprocess.Popen(['git','archive',base,'--',*PARTS],cwd=ROOT,stdout=subprocess.PIPE)
    subprocess.run(['tar','-xf','-','-C',str(baseline)],stdin=archive.stdout,check=True);archive.stdout.close()
    if archive.wait():raise RuntimeError('baseline archive failed')
    harness=Path(__file__).with_name('benchmark_session_buffers.cpp');shutil.copyfile(harness,output/harness.name)
    for name,source in (('baseline',baseline),('candidate',ROOT)):
        project=output/(name+'-project');project.mkdir()
        cmake=['cmake_minimum_required(VERSION 3.22)','project(buffer_comparison LANGUAGES CXX)','set(BUILD_TESTING OFF)']
        for part in PARTS[:-1]:cmake.append('add_subdirectory("'+str(source/part)+'" "'+part.split('/')[-1]+'")')
        cmake+=['add_executable(buffer_benchmark "'+str(output/harness.name)+'")','target_link_libraries(buffer_benchmark PRIVATE universal_gnss_driver)','target_compile_options(buffer_benchmark PRIVATE -fno-access-control)','target_compile_definitions(buffer_benchmark PRIVATE UG_BUFFER_TESTDATA="'+str(baseline/'testdata')+'")']
        (project/'CMakeLists.txt').write_text('\n'.join(cmake)+'\n')
        run(['cmake','-S',str(project),'-B',str(output/(name+'-build')),'-DCMAKE_BUILD_TYPE=Release'],output/(name+'-configure.log'))
        run(['cmake','--build',str(output/(name+'-build')),'-j4'],output/(name+'-build.log'))
    trace_sha=None
    timing_cpu={name:[] for name in ('baseline','candidate')}
    for phase in ('memory','timing'):
        for i in range(a.runs):
            order=('baseline','candidate') if i%2==0 else ('candidate','baseline')
            for name in order:
                prefix=output/(name+'-'+phase+'-'+str(i));trace=prefix.with_suffix('.trace')
                args=[str(output/(name+'-build/buffer_benchmark')),str(trace)]
                if phase=='timing':args.append('--timing-only')
                cpu=run(args,prefix.with_suffix('.csv'))
                if phase=='timing':timing_cpu[name].append(cpu)
                # Diagnostic legacy sizeof is on stderr too; keep CSV rows only.
                rows=prefix.with_suffix('.csv').read_text().splitlines();prefix.with_suffix('.csv').write_text('\n'.join(x for x in rows if not x.startswith('legacy element'))+'\n')
                if phase=='memory':
                    digest=hashlib.sha256(trace.read_bytes()).hexdigest()
                    if trace_sha is None:trace_sha=digest
                    if digest!=trace_sha:raise RuntimeError('observable state/metrics/timestamp mismatch: '+name)
                trace.unlink() # Keep canonical digest, not large duplicate traces.
            print(phase,i+1,'PASS',flush=True)
    summary={'base':base,'candidate_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'host':platform.platform(),'compiler':subprocess.check_output(['c++','--version'],text=True).splitlines()[0],'harness_sha256':hashlib.sha256(harness.read_bytes()).hexdigest(),'runs':a.runs,'trace_sha256':trace_sha,'timing_process_cpu_s':timing_cpu,'scenarios':[]}
    for phase in ('memory','timing'):
        for name in ('baseline','candidate'):
            rows=[list(csv.DictReader((output/(name+'-'+phase+'-'+str(i)+'.csv')).open())) for i in range(a.runs)]
            if not summary['scenarios']:summary['scenarios']=[{'scenario':x['scenario'],'chunk':int(x['chunk']),'stamp':int(x['stamp'])} for x in rows[0]]
            for index,scenario in enumerate(summary['scenarios']):
                entry={}
                for key in rows[0][index]:
                    if key in ('scenario','chunk','stamp'):continue
                    values=[int(row[index][key]) for row in rows]
                    entry[key]={'median':statistics.median(values),'min':min(values),'max':max(values)}
                scenario[name+'_'+phase]=entry
    (output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print('Compared scenarios:',len(summary['scenarios']),'; exact canonical traces PASS')
if __name__=='__main__':main()

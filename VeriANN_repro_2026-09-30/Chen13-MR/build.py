"""Reproducible native build; also record environment without rebuilding."""
import os, platform, subprocess, sys
import numpy, openpyxl
from run import HERE, STUDY, write, sha

def capture(cmd):return subprocess.check_output(cmd,text=True).strip()
command=['g++','-O3','-DNDEBUG','-std=c++17',str(HERE/'mr.cpp'),'-lcrypto','-o',str(HERE/'mr')]
STUDY.mkdir(parents=True,exist_ok=True)
if '--record-only' not in sys.argv:
 with (STUDY/'compile.log').open('w') as f:subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,check=True)
write(STUDY/'build-environment.json',dict(command=command,compiler=capture(['g++','--version']),
 openssl=capture(['openssl','version']),kernel=platform.platform(),python=platform.python_version(),
 numpy=numpy.__version__,openpyxl=openpyxl.__version__,cpu=capture(['lscpu']),
 memory=dict(line.split(':',1) for line in open('/proc/meminfo').read().splitlines()),
 source_sha256=sha(HERE/'mr.cpp'),binary_sha256=sha(HERE/'mr'),threads=1,
 note='record-only uses existing binary; source/binary checksums in every completed measurement'))
print('Build and environment record saved')

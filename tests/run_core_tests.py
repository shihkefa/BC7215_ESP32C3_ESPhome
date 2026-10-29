"""Compile the complete production climate component; mock only framework/vendor I/O."""
from pathlib import Path
import argparse
import subprocess
import shutil
import re
p=argparse.ArgumentParser()
p.add_argument('--cxx',default='g++');p.add_argument('--cxx-arg',action='append',default=[])
p.add_argument('--build-dir',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[1];c=root/'components/bc7215a'
b=a.build_dir.resolve();b.mkdir(parents=True,exist_ok=True)
header=(c/'bc7215a_climate.h').read_text(encoding='utf-8')
header=re.sub(r'#include "esphome/[^\"]+"', '#include "host_framework.h"', header)
header=header.replace('#include "bc7215ac.h"','#include "host_vendor.h"').replace(' protected:',' public:')
(b/'bc7215a_climate.h').write_text(header,encoding='utf-8')
for n in ['bc7215a_climate.cpp']:
    s=(c/n).read_text(encoding='utf-8').replace('#include "esphome/core/log.h"','#include "host_framework.h"')
    (b/n).write_text(s,encoding='utf-8')
for n in ['host_framework.h','host_vendor.h','core_test.cpp']:
    shutil.copyfile(root/'tests'/n,b/n)
exe=b/'core_test.exe'
subprocess.run([a.cxx,*a.cxx_arg,'-std=c++17','-Wall','-Wextra','-I'+str(c),
               str(b/'bc7215a_climate.cpp'),str(b/'core_test.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)

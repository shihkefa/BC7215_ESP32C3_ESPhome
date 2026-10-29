"""Link the complete component and unmodified vendor sources with desktop platform stubs."""
from pathlib import Path
import argparse
import subprocess
import shutil
import re
p=argparse.ArgumentParser();p.add_argument('--cxx',default='g++')
p.add_argument('--cxx-arg',action='append',default=[]);p.add_argument('--build-dir',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[1];c=root/'components/bc7215a'
b=a.build_dir.resolve();b.mkdir(parents=True,exist_ok=True)
(b/'esphome/core').mkdir(parents=True,exist_ok=True)
(b/'esphome/core/defines.h').write_text('#pragma once\n#define USE_ARDUINO\n')
header=(c/'bc7215a_climate.h').read_text(encoding='utf-8')
header=re.sub(r'#include "esphome/[^\"]+"','#include "host_framework.h"',header).replace(' protected:',' public:')
(b/'bc7215a_climate.h').write_text(header,encoding='utf-8')
for n in ['bc7215a_climate.cpp']:
    (b/n).write_text((c/n).read_text(encoding='utf-8').replace('#include "esphome/core/log.h"','#include "host_framework.h"'),encoding='utf-8')
shutil.copyfile(root/'tests/host_framework.h',b/'host_framework.h')
shutil.copyfile(root/'tests/vendor_core_test.cpp',b/'vendor_core_test.cpp')
(b/'Arduino.h').write_text('#pragma once\n#include "host_framework.h"\n#define HIGH 1\n#define INPUT 0\n#define OUTPUT 1\ninline void pinMode(int,int){}\ninline void digitalWrite(int,int){}\n',encoding='utf-8')
(b/'Stream.h').write_text('#pragma once\n#include "host_framework.h"\n',encoding='utf-8')
obj=b/'vendor.o';exe=b/'vendor_core_test.exe'
# Keep the obfuscated .c compiled as C, as it is on the target.
if Path(a.cxx).name.lower() in ('zig','zig.exe'):
    cc=[a.cxx,'cc']
else:
    cc=[a.cxx,'-x','c']
subprocess.run([*cc,'-std=c99','-c',str(c/'bc7215_ac_lib.c'),'-o',str(obj)],check=True)
subprocess.run([a.cxx,*a.cxx_arg,'-std=c++17','-I'+str(b),'-I'+str(c),
               str(b/'bc7215a_climate.cpp'),
               str(c/'bc7215.cpp'),str(c/'bc7215ac.cpp'),str(obj),
               str(b/'vendor_core_test.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)

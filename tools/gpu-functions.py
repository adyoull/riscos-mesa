#!/usr/bin/env python3
"""gpu-functions.py NM ARCHIVE OUT.c: the GPU library's list of gl entry
points (riscos_mesa_gpu_functions, v3d/include/riscos_mesa_gpu.h), made
from the gl* functions Mesa's glapi archive defines. Each is declared
without its real prototype: only its address is taken.

Copyright (c) 2026 Andrew Youll. MIT licence."""
import re, subprocess, sys

nm, archive, out = sys.argv[1:4]
names = sorted({m.group(1) for m in re.finditer(r' T (gl[A-Z]\w*)$',
                subprocess.run([nm, archive], check=True, capture_output=True, text=True).stdout, re.M)})
if len(names) < 500:
    sys.exit('gpu-functions.py: only %d gl functions in %s' % (len(names), archive))
with open(out, 'w') as f:
    f.write('/* made by tools/gpu-functions.py from %s: %d functions */\n' % (archive.split('/')[-1], len(names)))
    f.write('#include <stddef.h>\n#include "riscos_mesa_gpu.h"\n\n')
    for n in names:
        f.write('void %s(void);\n' % n)
    f.write('\nconst struct riscos_mesa_gpu_function riscos_mesa_gpu_functions[] = {\n')
    for n in names:
        f.write('    { "%s", %s },\n' % (n, n))
    f.write('    { NULL, NULL }\n};\n')

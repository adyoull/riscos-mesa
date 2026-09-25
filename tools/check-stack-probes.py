#!/usr/bin/env python3
"""check-stack-probes.py ELF - list functions with >= 4 KB stack frames that
do not probe the stack page by page. On RISC OS (GCCSDK GCC 10) such frames
can jump past the ELF stack guard page and crash; build with
-fstack-clash-protection until this prints 0. Needs arm-riscos-gnueabihf-objdump."""
import re, subprocess, sys
out=subprocess.run(['arm-riscos-gnueabihf-objdump','-d','--no-show-raw-insn',sys.argv[1]],capture_output=True,text=True).stdout
fn=None; body={}
for line in out.splitlines():
    m=re.match(r'^[0-9a-f]+ <(.+)>:$',line)
    if m: fn=m.group(1); body[fn]=[]; continue
    if fn: body[fn].append(line)
bad=[]
for f,lines in body.items():
    tot=sum(int(m.group(1)) for l in lines for m in [re.search(r'\tsub\tsp, sp, #(\d+)',l)] if m)
    if tot>=4096:
        probed=any(re.search(r'\tsub\tip, sp, ip|\tstr\tr0, \[ip|\[ip, #-\d+\]',l) for l in lines[:40])
        if not probed: bad.append((tot,f))
print(len(bad),"functions with >= 4 KB frames and NO probing:")
for t,f in sorted(bad,reverse=True)[:15]: print("  %6d %s"%(t,f))

#!/usr/bin/env python3
"""Runs dEQP-EGL (built by build.sh), a process per test group, surviving
crashes and hangs: each run goes on from the case after the one that
crashed or hung. Writes RESULTS (one line per case: name, status, details) and
prints a summary by group, and the difference from EXPECTED if given.

    run.py DEQP_EGL RESULTS [--filter PREFIX...] [--exclude PREFIX...] [--expected FILE] [--timeout S]

By default the performance and stress groups are left out, and so are the
1254 threaded sharing cases (--exclude with nothing after it runs them).
"""
import argparse
import os
import re
import select
import subprocess
import sys
import tempfile
import time
from collections import Counter, defaultdict

STATUS = re.compile(r"^  (Pass|Fail|NotSupported|QualityWarning|CompatibilityWarning|InternalError|"
                    r"ResourceError|Crash|Timeout|Waiver|DeviceLost)( \((.*)\))?$")


def all_cases(deqp):
    d = os.path.dirname(deqp)
    subprocess.run([deqp, "--deqp-runmode=txt-caselist"], cwd=d, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, check=True)
    with open(os.path.join(d, "dEQP-EGL-cases.txt")) as f:
        return [l[6:].strip() for l in f if l.startswith("TEST: ")]


def run_batch(deqp, cases, timeout, results):
    """Runs cases in one process; returns the index of the first case not run."""
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n".join(cases) + "\n")
        listfile = f.name
    p = subprocess.Popen([deqp, "--deqp-caselist-file=" + listfile, "--deqp-log-images=disable",
                          "--deqp-log-shader-sources=disable", "--deqp-log-filename=/dev/null"],
                         cwd=os.path.dirname(deqp), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    fd = p.stdout.fileno()
    current, done, buf, last = None, 0, "", time.time()
    while True:
        ready, _, _ = select.select([fd], [], [], 0.2)
        chunk = os.read(fd, 65536).decode("utf-8", "replace") if ready else ""
        if chunk:
            buf += chunk
            last = time.time()
            *lines, buf = buf.split("\n")
            for line in lines:
                m = re.match(r"^Test case '(.*)'\.\.$", line)
                if m:
                    current = m.group(1)
                    continue
                m = STATUS.match(line)
                if m and current:
                    results[current] = (m.group(1), m.group(3) or "")
                    current = None
                    done += 1
        elif ready or p.poll() is not None:   # EOF
            p.wait()
            break
        elif time.time() - last > timeout:
            p.kill()
            p.wait()
            if current:
                results[current] = ("Timeout", "no progress for %d s" % timeout)
                done += 1
            break
    os.unlink(listfile)
    if p.returncode not in (0, None) and current and current not in results:
        results[current] = ("Crash", "exit status %d" % p.returncode)
        done += 1
    # cases actually finished, in order
    n = 0
    for c in cases:
        if c in results:
            n += 1
        else:
            break
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("deqp")
    ap.add_argument("results")
    ap.add_argument("--filter", nargs="*", default=["dEQP-EGL.info.", "dEQP-EGL.functional."])
    ap.add_argument("--exclude", nargs="*", default=["dEQP-EGL.functional.sharing.gles2.multithread."])
    ap.add_argument("--expected")
    ap.add_argument("--timeout", type=int, default=60)
    a = ap.parse_args()
    deqp = os.path.abspath(a.deqp)
    cases = [c for c in all_cases(deqp) if any(c.startswith(p) for p in a.filter)
             and not any(c.startswith(p) for p in a.exclude)]
    results = {}
    # A new process for each group (dEQP-EGL.functional.<group>): all memory
    # must stay below 2 GB, and a whole run in one process fragments the
    # brk heap until it reaches the thread stacks.
    groups = defaultdict(list)
    for c in cases:
        groups[".".join(c.split(".")[:3])].append(c)
    done = 0
    for g in groups.values():
        i = 0
        while i < len(g):
            n = run_batch(deqp, g[i:], a.timeout, results)
            if n == 0:          # the first case produced nothing at all
                results.setdefault(g[i], ("Crash", "no output"))
                n = 1
            i += n
            sys.stderr.write("\r%d/%d cases" % (done + i, len(cases)))
        done += len(g)
    sys.stderr.write("\n")
    with open(a.results, "w") as f:
        for c in cases:
            st, detail = results.get(c, ("Missing", ""))
            f.write("%s %s %s\n" % (c, st, detail))

    by_group = defaultdict(Counter)
    total = Counter()
    for c in cases:
        st = results.get(c, ("Missing", ""))[0]
        by_group[".".join(c.split(".")[1:3])][st] += 1
        total[st] += 1
    for g in sorted(by_group):
        cnt = by_group[g]
        print("%-40s %s" % (g, "  ".join("%s %d" % kv for kv in sorted(cnt.items()))))
    print("TOTAL: " + "  ".join("%s %d" % kv for kv in sorted(total.items())))

    if a.expected:
        want = {}
        with open(a.expected) as f:
            for line in f:
                parts = line.rstrip("\n").split(" ", 2)
                if len(parts) >= 2:
                    want[parts[0]] = parts[1]
        changed = [(c, want.get(c), results.get(c, ("Missing",))[0]) for c in cases
                   if want.get(c) != results.get(c, ("Missing",))[0]]
        for c, w, g in changed:
            print("CHANGED %s: %s -> %s" % (c, w, g))
        worse = [x for x in changed if x[1] == "Pass"]
        print("%d changed, %d no longer pass" % (len(changed), len(worse)))
        return 1 if worse else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())

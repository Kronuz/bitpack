#!/usr/bin/env python3
"""Count, per width, the input loads and stack traffic of the unpack kernels in an asm_check.s.

usage: asm_count.py FILE.s {x86|arm64}   (FILE.s from: c++ -std=c++20 -O3 -I. -S bench/asm_check.cc)

Per function (unpack `up<B>`, delta `ud<B>`): vector loads whose base is the input pointer, vector
stores whose base is the output pointer, and stack accesses (spills and reloads). A kernel that
reads each of its B input words once has loads == B; the aliasing reloads show up as loads > B.
"""
import re
import sys

path, arch = sys.argv[1], sys.argv[2]
text = open(path).read().splitlines()

if arch == "x86":
    # AT&T: movdqu 16(%rdi), %xmm1 / movups %xmm0, 32(%rsi) / movaps %xmm2, 16(%rsp)
    ld = re.compile(r"\s(?:movdqu|movdqa|movups|movaps|vmovdqu|vmovdqa)\s+-?\d*\(%rdi\),")
    st = re.compile(r"\s(?:movdqu|movdqa|movups|movaps|vmovdqu|vmovdqa)\s+%[xy]mm\d+,\s*-?\d*\(%rsi\)")
    stack = re.compile(r"\(%rsp\)|\(%rbp\)")
    # the delta kernel takes (in, base, out): out is rdx
    st_delta = re.compile(r"\s(?:movdqu|movdqa|movups|movaps|vmovdqu|vmovdqa)\s+%[xy]mm\d+,\s*-?\d*\(%rdx\)")
    label = re.compile(r"^_?(_Z(?:2(?:up|ud)|N7bitpack6detail(?:11unpack_simd|17unpack_delta_simd))ILj(\d+)E[A-Za-z0-9_]*):")  # Mach-O adds a leading underscore
else:
    # Mach-O/ELF arm64: ldr q0, [x0, #16] / ldp q0, q1, [x0, #32] / str q0, [x1, #16] / stp q0, q1, [sp, #32]
    ld = re.compile(r"\s(ldr|ldp)\s+q\d+(?:,\s*q\d+)?,\s*\[x0[,\]]")
    st = re.compile(r"\s(str|stp)\s+q\d+(?:,\s*q\d+)?,\s*\[x1[,\]]")
    st_delta = re.compile(r"\s(str|stp)\s+q\d+(?:,\s*q\d+)?,\s*\[x2[,\]]")
    stack = re.compile(r"\[sp[,\]]")
    label = re.compile(r"^_?(_Z(?:2(?:up|ud)|N7bitpack6detail(?:11unpack_simd|17unpack_delta_simd))ILj(\d+)E[A-Za-z0-9_]*):")

def weight(line):
    return 2 if re.search(r"\sldp\s|\sstp\s", line) else 1

funcs = {}
cur = None
for line in text:
    m = label.match(line)
    if m:
        sym = m.group(1)
        kind = "ud" if ("2ud" in sym[:6] or "unpack_delta_simd" in sym) else "up"
        cur = (kind, int(m.group(2)), sym)
        cand = {"ld": 0, "st": 0, "stack": 0, "n": 0}
        funcs[cur] = cand
        continue
    if cur is None or not line.startswith("\t") or line.startswith("\t."):
        if line.startswith(".cfi_endproc") or line.startswith("\t.cfi_endproc"):
            cur = None
        continue
    f = funcs[cur]
    f["n"] += 1
    stre = st_delta if cur[0] == "ud" else st
    if ld.search(line):
        f["ld"] += weight(line)
    elif stre.search(line):
        f["st"] += weight(line)
    elif stack.search(line):
        f["stack"] += weight(line)
    if re.match(r"\s*\.?cfi_endproc|\s*\.size", line):
        cur = None

def best(kind, b):
    c = [f for (k, bb, _), f in funcs.items() if k == kind and bb == b]
    return max(c, key=lambda f: f["n"]) if c else None

print(f"{'B':>3} | {'unpack ld/B':>11} {'st':>3} {'stack':>5} {'insns':>5} | {'delta ld/B':>10} {'st':>3} {'stack':>5} {'insns':>5}")
for b in range(1, 33):
    u = best("up", b); d = best("ud", b)
    def cell(f):
        return ("   -       -     -     -") if not f else f"{f['ld']:>6}/{b:<3} {f['st']:>3} {f['stack']:>5} {f['n']:>5}"
    print(f"{b:>3} | {cell(u)} | {cell(d)}")

#!/usr/bin/env python3
"""Random GLSL 1.20 fragment shaders for riscos-mesa's JIT check (glsl-jit).

   tools/gen-jit-shaders.py SEED COUNT > tests/host-harness/mesa/jit-shaders.txt

One shader per line, newlines written as \\n. The shaders mix arithmetic
(everything the JIT compiles) with what it leaves to the interpreter
(loops, IF, discard, texturing, derivatives, indexed arrays), so blocks
start and end in every kind of place and run with some lanes inactive.
Part of riscos-mesa, MIT licence.
"""
import random
import sys

seed = int(sys.argv[1])
n = int(sys.argv[2])
random.seed(seed)

LEAVES = ['c.r', 'c.g', 'c.b', 'tc.x', 'tc.y', 'tc.z', 'tc.w', 's', 't.x', 't.y',
          't.z', 'arr[ix].x', 'arr[ix].w', 'u.y', 'u.w', 'pal[pi].z',
          'texture2D(smp, t.xy).x', 'texture2DProj(smp, tc).z',
          'dFdx(tc.x)*10.0', 'gl_FragCoord.x*0.02', 'gl_FragCoord.y*0.03']
UNARY = ['abs', 'fract', 'floor', 'sin', 'cos', 'sign', '-', 'sqrt', 'exp2',
         'log2', 'inversesqrt', 'exp', 'ceil']
BINARY = ['min', 'max', 'step', 'mod', 'pow', 'atan', 'distance']


def const():
    return str(round(random.uniform(-2, 2), 3))


def expr(d=0):
    r = random.random()
    if d > 2 or r < 0.3:
        return random.choice(LEAVES + [const()])
    if r < 0.5:
        return '(%s %s %s)' % (expr(d + 1), random.choice('+-*'), expr(d + 1))
    if r < 0.58:
        return '(%s / (abs(%s) + 0.5))' % (expr(d + 1), expr(d + 1))
    if r < 0.66:
        return 'mix(%s, %s, clamp(%s, 0.0, 1.0))' % (expr(d + 1), expr(d + 1), expr(d + 1))
    if r < 0.72:
        return 'smoothstep(-0.5, 0.75, %s)' % expr(d + 1)
    if r < 0.8:
        return random.choice(['dot(normalize(vec3(%s, %s, 0.7)), vec3(0.3, 0.6, 0.74))',
                              'length(vec2(%s, %s))',
                              'cross(vec3(%s, 0.2, 0.9), vec3(0.4, %s, -0.3)).x'
                              ]) % (expr(d + 1), expr(d + 1))
    f = random.choice(UNARY)
    if f in ('sqrt', 'inversesqrt'):
        return '%s(abs(%s) + 0.01)' % (f, expr(d + 1))
    if f == 'log2':
        return 'log2(abs(%s) + 0.01)' % expr(d + 1)
    if f in ('exp2', 'exp'):
        return '%s(clamp(%s, -4.0, 4.0))' % (f, expr(d + 1))
    if random.random() < 0.3:
        b = random.choice(BINARY)
        if b == 'pow':
            return 'pow(abs(%s) + 0.01, clamp(%s, -3.0, 3.0))' % (expr(d + 1), expr(d + 1))
        if b == 'distance':
            return 'distance(vec2(%s, 0.5), vec2(0.1, %s))' % (expr(d + 1), expr(d + 1))
        return '%s(%s, %s)' % (b, expr(d + 1), expr(d + 1))
    return '%s(%s)' % (f, expr(d + 1))


def cond():
    return '%s %s %s' % (expr(1), random.choice(['<', '>', '<=', '>=']), expr(2))


def stmt(d, inloop):
    r = random.random()
    if d > 2 or r < 0.45:
        k = random.random()
        if k < 0.3:
            return 's = %s;' % expr()
        if k < 0.5:
            return 't.%s = %s;' % (random.choice('xyzw'), expr())
        if k < 0.58:
            return 'ix = int(clamp(%s, 0.0, 3.0));' % expr()
        if k < 0.7:
            return 'arr[ix] = vec4(%s, %s, s, 1.0);' % (expr(), expr())
        if k < 0.78:
            return 't = clamp(t, -1.0, 1.0);'
        if k < 0.88:
            return 't.xyz = t.zxy * %s + vec3(%s);' % (expr(), expr())
        return 's += %s;' % expr()
    if r < 0.62:
        e = '' if random.random() < 0.4 else ' else { %s }' % block(d + 1, inloop)
        return 'if (%s) { %s }%s' % (cond(), block(d + 1, inloop), e)
    if r < 0.74:
        v = 'i%d' % d
        return 'for (int %s=0; %s<%d; %s++) { %s }' % (v, v, random.randint(1, 3), v,
                                                       block(d + 1, True))
    if r < 0.8 and inloop:
        return 'if (%s) %s;' % (cond(), random.choice(['break', 'continue']))
    if r < 0.86:
        return 'if (%s) discard;' % cond()
    if r < 0.9:
        return 'if (%s) { gl_FragColor = vec4(s, t.x, t.y, 1.0); return; }' % cond()
    return 's = %s;' % expr()


def block(d, inloop):
    return ' '.join(stmt(d, inloop) for _ in range(random.randint(1, 4)))


for i in range(n):
    body = block(0, False)
    fs = ("#version 120\\nvarying vec4 c; varying vec4 tc; uniform vec4 u; "
          "uniform vec4 pal[4]; uniform sampler2D smp;\\n"
          "void main(){ float s=0.25; vec4 t=vec4(c.b, tc.y, 0.5, 1.0); vec4 arr[4]; "
          "for(int q=0;q<4;q++) arr[q]=vec4(float(q)*0.3, c.g, tc.x, 1.0); int ix=0; "
          "int pi=int(clamp(tc.x,0.0,3.0)); "
          + body + " gl_FragColor = vec4(s, t.x, t.y+arr[ix].z, clamp(t.w,0.0,1.0)); }\\n")
    print(fs)

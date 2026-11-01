"""Generic CXDEC (KiriKiri "Cx" XP3 filter) reference decoder.

Python port of GARbro's CxEncryption (ArcFormats/KiriKiri/KiriKiriCx.cs) with the
exact Emit/GetRandom ordering of the original generator. Used to verify the C++
runtime filter; it carries no game data. A scheme is a small text file:

  mask=0x1a7 / offset=0x369 / prolog=0,1,2 / odd=<6> / even=<8> / control_block=<file>.cb
  (one key=value per line; .cxscheme)

Usage: python tools/cxdec_ref.py SCHEME.cxscheme ADLR_HEX IN OUT  (decrypt one file)
See docs/cxdec.md.
"""
import os
import struct
M=0xffffffff
class Prog:
    LIMIT=0x80
    def __init__(s, seed, cb): s.seed=seed; s.cb=cb; s.code=[]; s.len=0
    def rnd(s):
        o=s.seed; s.seed=(1103515245*o+12345)&M
        return (s.seed ^ (o<<16) ^ (o>>16)) & M
    def emit(s, op, n=1, imm=None):
        # exact GARbro order: Emit(opcode) is checked first; a callable imm is drawn
        # from the RNG only after the opcode fitted, then EmitUInt32 is checked.
        if s.len+n>s.LIMIT: return False
        s.len+=n
        if imm is not None:
            if callable(imm): imm=imm()
            if s.len+4>s.LIMIT: return False
            s.len+=4
        s.code.append((op,imm)); return True
    def nop(s,n):
        if s.len+n>s.LIMIT: return False
        s.len+=n; return True
    def run(s, h):
        eax=ebx=ecx=edi=0; st=[]
        for op,imm in s.code:
            if op=='edi': edi=h
            elif op=='pushb': st.append(ebx)
            elif op=='popb': ebx=st.pop()
            elif op=='pushc': st.append(ecx)
            elif op=='popc': ecx=st.pop()
            elif op=='ebx=eax': ebx=eax
            elif op=='eax=edi': eax=edi
            elif op=='ecx=ebx': ecx=ebx
            elif op=='ecx&=f': ecx&=15
            elif op=='ebx>>=1': ebx>>=1
            elif op=='eax<<=1': eax=(eax<<1)&M
            elif op=='shr': eax>>=ecx
            elif op=='shl': eax=(eax<<ecx)&M
            elif op=='or': eax|=ebx
            elif op=='not': eax=~eax&M
            elif op=='neg': eax=(-eax)&M
            elif op=='dec': eax=(eax-1)&M
            elif op=='inc': eax=(eax+1)&M
            elif op=='add': eax=(eax+ebx)&M
            elif op=='sub': eax=(eax-ebx)&M
            elif op=='imul': eax=(eax*ebx)&M
            elif op=='addi': eax=(eax+imm)&M
            elif op=='subi': eax=(eax-imm)&M
            elif op=='andbi': ebx&=imm
            elif op=='andi': eax&=imm
            elif op=='xori': eax^=imm
            elif op=='movi': eax=imm
            elif op=='ind': eax=s.cb[eax]
            elif op=='ret': assert not st; return eax
        raise Exception('no ret')

class Cx:
    def __init__(s, mask, offset, prolog, odd, even, cb_bytes):
        s.mask=mask; s.offset=offset; s.prolog=prolog; s.odd=odd; s.even=even
        s.cb=struct.unpack('<1024I', cb_bytes); s.progs={}
    def prog(s, seed):
        if seed not in s.progs:
            p=Prog(seed, s.cb)
            for stage in range(5,0,-1):
                if s.emit_code(p, stage): break
                p.code=[]; p.len=0
            else: raise Exception('too large')
            s.progs[seed]=p
        return s.progs[seed]
    def emit_code(s,p,stage):
        return p.nop(5) and p.emit('edi',4) and s.body(p,stage) and p.nop(5) and p.emit('ret',1)
    def body(s,p,stage):
        if stage==1: return s.prol(p)
        if not p.emit('pushb'): return False
        if not ((s.body if p.rnd()&1 else s.body2)(p,stage-1)): return False
        if not p.emit('ebx=eax',2): return False
        if not ((s.body if p.rnd()&1 else s.body2)(p,stage-1)): return False
        return s.oddb(p) and p.emit('popb')
    def body2(s,p,stage):
        if stage==1: return s.prol(p)
        if not ((s.body if p.rnd()&1 else s.body2)(p,stage-1)): return False
        return s.evenb(p)
    def prol(s,p):
        c=s.prolog[p.rnd()%3]
        if c==2: return p.nop(5) and p.emit('movi',2,lambda: p.rnd()&0x3ff) and p.emit('ind',0)
        if c==1: return p.emit('eax=edi',2)
        return p.emit('movi',1,p.rnd)
    def evenb(s,p):
        c=s.even[p.rnd()&7]
        if c==0: return p.emit('not',2)
        if c==1: return p.emit('dec')
        if c==2: return p.emit('neg',2)
        if c==3: return p.emit('inc')
        if c==4: return p.nop(5) and p.emit('andi',1,0x3ff) and p.emit('ind',3)
        if c==5: return (p.emit('pushb') and p.emit('ebx=eax',2) and p.emit('andbi',2,0xaaaaaaaa) and p.emit('andi',1,0x55555555)
                         and p.emit('ebx>>=1',2) and p.emit('eax<<=1',2) and p.emit('or',2) and p.emit('popb'))
        if c==6: return p.emit('xori',1,p.rnd)
        op='addi' if p.rnd()&1 else 'subi'
        return p.emit(op,1,p.rnd)
    def oddb(s,p):
        c=s.odd[p.rnd()%6]
        if c in (0,1): return p.emit('pushc') and p.emit('ecx=ebx',2) and p.emit('ecx&=f',3) and p.emit('shr' if c==0 else 'shl',2) and p.emit('popc')
        if c==2: return p.emit('add',2)
        if c==3: return p.emit('neg',2) and p.emit('add',2)
        if c==4: return p.emit('imul',3)
        return p.emit('sub',2)
    def xcode(s,h):
        p=s.prog(h&0x7f); h>>=7
        return p.run(h), p.run(~h&M)
    def decode(s,key,offset,buf,pos,count):
        r1,r2=s.xcode(key)
        k1=r2>>16; k2=r2&0xffff; k3=r1&0xff
        if k1==k2: k2+=1
        if k3==0: k3=1
        if offset<=k2<offset+count: buf[pos+k2-offset]^=(r1>>16)&0xff
        if offset<=k1<offset+count: buf[pos+k1-offset]^=(r1>>8)&0xff
        for i in range(count): buf[pos+i]^=k3
    def decrypt(s,h,offset,buf,pos,count):
        base=(h&s.mask)+s.offset
        if offset<base:
            n=min(base-offset,count); s.decode(h,offset,buf,pos,n); offset+=n; pos+=n; count-=n
        if count>0: s.decode((h>>16)^h,offset,buf,pos,count)



def load_scheme(path):
    """Reads the .cxscheme text form (key=value; same parser rules as the runtime)."""
    kv = {}
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            kv[k.strip()] = v.strip()
    lst = lambda k: [int(x, 0) for x in kv[k].split(",")]
    cb = open(os.path.join(os.path.dirname(os.path.abspath(path)), kv["control_block"]), "rb").read()
    return Cx(int(kv["mask"], 0), int(kv["offset"], 0), lst("prolog"), lst("odd"), lst("even"), cb)


def write_scheme(path, mask, offset, prolog, odd, even, cb, source=""):
    cb_name = os.path.splitext(os.path.basename(path))[0] + ".cb"
    open(os.path.join(os.path.dirname(os.path.abspath(path)), cb_name), "wb").write(cb)
    lines = ["# kk4s CXDEC scheme" + ((" (from %s)" % source) if source else ""),
             "mask=%#x" % mask, "offset=%#x" % offset]
    for k, v in (("prolog", prolog), ("odd", odd), ("even", even)):
        lines.append("%s=%s" % (k, ",".join(str(x) for x in v)))
    lines.append("control_block=%s" % cb_name)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

if __name__ == "__main__":
    import sys, os
    cx = load_scheme(sys.argv[1]); h = int(sys.argv[2], 16)
    buf = bytearray(open(sys.argv[3], "rb").read())
    cx.decrypt(h, 0, buf, 0, len(buf))
    open(sys.argv[4], "wb").write(buf)

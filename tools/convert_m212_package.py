"""Rewrites a Metallicafan212 HP2 package (version 104/106) as a retail
HP2 package (version 79) that the original engine can load.

  python3 -I convert.py <input.unr|utx> <output>

What changes:
  - FPoly / FBspSurf: 64-bit PolyFlags -> 32-bit; LightMapScale, DWORD and
    compact index tail dropped.
  - FBspNode / FZoneProperties / FLeaf: 256-bit zone masks -> 64-bit.
  - ULevel: the 64 KB table after TravelInfo is dropped.
  - Textures in Format 11 (BC7) are decoded to RGBA8 (Format 5).
  - Lazy-array seek positions are recomputed for the new layout.
  - Imports of Metallicafan's M212Share package point at retail HGame
    classes (M212Bean/M212SpawnBeany -> Jellybean, M212CauldronMixing ->
    CauldronMixing).
Every other byte is copied as is.
"""
import struct, sys
from PIL import Image

RF_HasStack = 0x02000000
REDIRECT = {'M212Bean': 'Jellybean', 'M212SpawnBeany': 'Jellybean', 'M212CauldronMixing': 'CauldronMixing', 'M212Share': 'HGame'}


def read_ci(d, p):
    b = d[p]; p += 1
    neg = b & 0x80; v = b & 0x3f
    if b & 0x40:
        sh = 6
        while True:
            b = d[p]; p += 1
            v |= (b & 0x7f) << sh; sh += 7
            if not b & 0x80:
                break
    return (-v if neg else v), p


def write_ci(v):
    out = bytearray()
    a = abs(v)
    b0 = (0x80 if v < 0 else 0) | (a & 0x3f)
    a >>= 6
    if a:
        b0 |= 0x40
    out.append(b0)
    while a:
        b = a & 0x7f; a >>= 7
        if a:
            b |= 0x80
        out.append(b)
    return bytes(out)


class Pkg:
    def __init__(s, fn):
        s.d = d = open(fn, 'rb').read()
        (s.tag, s.filever, s.flags, nc, no, ec, eo, ic, io) = struct.unpack_from('<IIIIIIIII', d, 0)
        s.guid = d[36:52]
        s.names = []
        p = no
        for _ in range(nc):
            ln, p = read_ci(d, p)
            st = d[p:p + ln - 1].decode('latin1') if ln > 0 else ''
            p += max(ln, 0)
            fl = struct.unpack_from('<I', d, p)[0]; p += 4
            s.names.append([st, fl])
        s.imps = []
        p = io
        for _ in range(ic):
            a, p = read_ci(d, p); b, p = read_ci(d, p)
            pk = struct.unpack_from('<i', d, p)[0]; p += 4
            n, p = read_ci(d, p)
            s.imps.append([a, b, pk, n])
        s.exps = []
        p = eo
        for _ in range(ec):
            c, p = read_ci(d, p); sp, p = read_ci(d, p)
            pk = struct.unpack_from('<i', d, p)[0]; p += 4
            n, p = read_ci(d, p)
            fl = struct.unpack_from('<I', d, p)[0]; p += 4
            sz, p = read_ci(d, p); off = 0
            if sz > 0:
                off, p = read_ci(d, p)
            s.exps.append(dict(cls=c, sup=sp, pkg=pk, name=n, flags=fl, size=sz, off=off))

    def cname(s, e):
        c = e['cls']
        if c < 0:
            return s.names[s.imps[-c - 1][3]][0]
        if c > 0:
            return s.names[s.exps[c - 1]['name']][0]
        return 'Class'

    def name_index(s, nm):
        for i, (n, _) in enumerate(s.names):
            if n.lower() == nm.lower():
                return i
        s.names.append([nm, 0x00070010])
        return len(s.names) - 1


class T:
    """Copies an export from src to out, transforming fields on the way."""

    def __init__(s, pk, e, base):
        s.pk = pk; s.d = pk.d; s.p = e['off']; s.end = e['off'] + e['size']
        s.o = bytearray(); s.base = base; s.e = e

    def copy(s, n):
        s.o += s.d[s.p:s.p + n]; s.p += n

    def ci(s, keep=True):
        start = s.p
        v, s.p = read_ci(s.d, s.p)
        if keep:
            s.o += s.d[start:s.p]
        return v

    def i32(s, keep=True):
        v = struct.unpack_from('<i', s.d, s.p)[0]
        if keep:
            s.o += s.d[s.p:s.p + 4]
        s.p += 4
        return v

    def skip(s, n):
        s.p += n

    def mask(s, n_in, n_out=8):
        s.o += s.d[s.p:s.p + n_out]; s.p += n_in

    def header(s):
        if s.e['flags'] & RF_HasStack:
            nd = s.ci(); s.ci(); s.copy(12)
            if nd:
                s.ci()

    def props(s, patch=None):
        """Copies tagged properties; patch(name, info, raw) may return new raw."""
        pk = s.pk
        while True:
            n = s.ci()
            nm = pk.names[n][0]
            if nm == 'None':
                return
            info = s.d[s.p]; s.copy(1)
            t = info & 0xf; arr = info & 0x80; sz = (info >> 4) & 7
            if t == 10:
                s.ci()
            if sz < 5:
                size = [1, 2, 4, 12, 16][sz]
            elif sz == 5:
                size = s.d[s.p]; s.copy(1)
            elif sz == 6:
                size = struct.unpack_from('<H', s.d, s.p)[0]; s.copy(2)
            else:
                size = struct.unpack_from('<I', s.d, s.p)[0]; s.copy(4)
            if arr and t != 3:
                b = s.d[s.p]
                s.copy(1 if b < 128 else (2 if b & 0xc0 == 0x80 else 4))
            if t == 3:
                size = 0
            raw = s.d[s.p:s.p + size]
            if patch:
                new = patch(nm, t, raw)
                if new is not None:
                    assert len(new) == len(raw)
                    raw = new
            s.o += raw; s.p += size

    def rest(s):
        s.copy(s.end - s.p)


def conv_poly(t):
    nv = t.ci()
    t.copy(48 + 12 * nv)
    t.o += t.d[t.p:t.p + 4]; t.skip(8)          # PolyFlags 64 -> 32
    for _ in range(5):
        t.ci()                                   # Actor Texture ItemName Link BrushPoly
    t.copy(4)                                    # PanU PanV
    t.skip(4); t.skip(4); t.ci(keep=False)       # LightMapScale, DWORD, compact


def polys(t):
    t.header(); t.props()
    num = t.i32(); t.i32()
    for _ in range(num):
        conv_poly(t)


def model(t):
    t.header(); t.props()
    t.copy(25 + 16)                              # BoundingBox, BoundingSphere
    for _ in range(2):                           # Vectors, Points
        n = t.ci(); t.copy(12 * n)
    n = t.ci()                                   # Nodes
    for _ in range(n):
        t.copy(16); t.mask(32)
        t.copy(1)
        for _ in range(7):
            t.ci()                               # VertPool Surf Child[3] Collision Render
        t.copy(2 + 1 + 8)                        # iZone[2], NumVertices, iLeaf[2]
    n = t.ci()                                   # Surfs
    for _ in range(n):
        t.ci()
        t.o += t.d[t.p:t.p + 4]; t.skip(8)
        for _ in range(6):
            t.ci()
        t.copy(4); t.ci()
        t.skip(4); t.skip(4); t.ci(keep=False)
    n = t.ci()                                   # Verts
    for _ in range(n):
        t.ci(); t.ci()
    t.i32()                                      # NumSharedSides
    nz = t.i32()
    for _ in range(nz):
        t.ci(); t.mask(32); t.mask(32)
    t.ci()                                       # Polys
    n = t.ci()                                   # LightMap
    for _ in range(n):
        t.copy(4 + 12); t.ci(); t.ci(); t.copy(12)
    n = t.ci(); t.copy(n)                        # LightBits
    n = t.ci(); t.copy(25 * n)                   # Bounds
    n = t.ci(); t.copy(4 * n)                    # LeafHulls
    n = t.ci()                                   # Leaves
    for _ in range(n):
        t.ci(); t.ci(); t.ci(); t.mask(32)
    n = t.ci()                                   # Lights
    for _ in range(n):
        t.ci()
    t.copy(8)                                    # RootOutside, Linked


def level(t):
    t.header(); t.props()
    num = t.i32(); t.i32()
    for _ in range(num):
        t.ci()
    def fstr():
        n = t.ci(); t.copy(-2 * n if n < 0 else n)
    for _ in range(4):
        fstr()
    n = t.ci()
    for _ in range(n):
        fstr()
    t.copy(8)                                    # Port, Valid
    t.ci()                                       # Model
    n = t.ci()                                   # ReachSpecs
    for _ in range(n):
        t.copy(4); t.ci(); t.ci(); t.copy(12 + 1)
    t.copy(4); t.ci()                            # ApproxTime, FirstDeleted
    for _ in range(16):
        t.ci()
    n = t.ci()                                   # TravelInfo
    for _ in range(n):
        fstr(); fstr()
    assert t.end - t.p == 65536, ('level tail', t.end - t.p)
    t.p = t.end                                  # drop the v106 zone table


def texture(t, comp_flag):
    fmt = {}

    def patch(nm, ty, raw):
        if nm == 'Format' and ty == 1:
            fmt['v'] = raw[0]
            if raw[0] == 11:
                return bytes([5])
        return None
    t.header(); t.props(patch)
    for _ in range(2 if comp_flag else 1):
        n = t.ci()
        for _ in range(n):
            t.i32(keep=False)
            size, t.p = read_ci(t.d, t.p)
            data = t.d[t.p:t.p + size]; t.p += size
            us, vs = struct.unpack_from('<ii', t.d, t.p)
            if fmt.get('v') == 11:
                bw, bh = max(4, (us + 3) // 4 * 4), max(4, (vs + 3) // 4 * 4)
                need = (bw // 4) * (bh // 4) * 16
                im = Image.frombytes('RGBA', (bw, bh), data[:need].ljust(need, b'\0'), 'bcn', 7)
                data = im.crop((0, 0, us, vs)).tobytes()
            body = write_ci(len(data)) + data
            # SeekPos: absolute file offset just after the data.
            seek = t.base + len(t.o) + 4 + len(body)
            t.o += struct.pack('<i', seek) + body
            t.copy(8 + 2)                        # USize VSize UBits VBits
    return fmt.get('v')


def main(src, dst):
    pk = Pkg(src)
    # Redirect Metallicafan's classes to retail ones.
    for im in pk.imps:
        nm = pk.names[im[3]][0]
        if nm in REDIRECT:
            im[3] = pk.name_index(REDIRECT[nm])
        if pk.names[im[0]][0] == 'M212Share':
            im[0] = pk.name_index('HGame')
    # Layout: header, names, imports, export data, export table.
    names_blob = bytearray()
    for st, fl in pk.names:
        b = st.encode('latin1') + b'\0'
        names_blob += write_ci(len(b)) + b + struct.pack('<I', fl)
    imps_blob = bytearray()
    for a, b, p, n in pk.imps:
        imps_blob += write_ci(a) + write_ci(b) + struct.pack('<i', p) + write_ci(n)
    head_len = 36 + 16 + 4 + 8
    name_off = head_len
    imp_off = name_off + len(names_blob)
    data_off = imp_off + len(imps_blob)
    blobs = bytearray(); counts = {}
    for e in pk.exps:
        if e['size'] <= 0:
            e['new'] = (0, 0)
            continue
        c = pk.cname(e)
        base = data_off + len(blobs)
        t = T(pk, e, base)
        try:
            if c == 'Polys':
                polys(t)
            elif c == 'Model':
                model(t)
            elif c == 'Level':
                level(t)
            elif c in ('Texture', 'WetTexture', 'FireTexture', 'WaterTexture', 'IceTexture', 'WaveTexture'):
                comp = False
                tt = T(pk, e, 0); tt.header()
                def probe(nm, ty, raw):
                    nonlocal comp
                    if nm == 'bHasComp' and ty == 3:
                        comp = True
                tt.props(probe)
                f = texture(t, comp)
                if f == 11:
                    counts['bc7'] = counts.get('bc7', 0) + 1
            t.rest()
        except Exception as ex:
            raise RuntimeError('%s %s: %r' % (c, pk.names[e['name']][0], ex))
        assert t.p == t.end, (c, pk.names[e['name']][0], t.p - e['off'], e['size'])
        counts[c] = counts.get(c, 0) + 1
        e['new'] = (len(t.o), base)
        blobs += t.o
    exp_off = data_off + len(blobs)
    exps_blob = bytearray()
    for e in pk.exps:
        size, off = e['new']
        exps_blob += write_ci(e['cls']) + write_ci(e['sup']) + struct.pack('<i', e['pkg']) + write_ci(e['name'])
        exps_blob += struct.pack('<I', e['flags']) + write_ci(size)
        if size > 0:
            exps_blob += write_ci(off)
    head = struct.pack('<IIIIIIIII', pk.tag, 79, pk.flags, len(pk.names), name_off, len(pk.exps), exp_off, len(pk.imps), imp_off)
    head += pk.guid + struct.pack('<iii', 1, len(pk.exps), len(pk.names))
    assert len(head) == head_len
    with open(dst, 'wb') as f:
        f.write(head); f.write(names_blob); f.write(imps_blob); f.write(blobs); f.write(exps_blob)
    print(src.split('/')[-1], '->', dst.split('/')[-1], 'v%d -> v79' % (pk.filever & 0xffff), {k: v for k, v in counts.items() if k in ('Model', 'Polys', 'Level', 'Texture', 'bc7')})


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

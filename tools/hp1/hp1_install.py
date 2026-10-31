"""Copies Harry Potter 1 (PC) packages into the HP2 data folder, next to
the HP2 ones, without replacing anything.

  python3 -I hp1_install.py <HP1 'Game Files' dir> <HP2 Data/Unreal dir> [--dry]

HP1 packages are version 76 and the HP2 engine loads them as they are
(retail HP2 already ships some byte-identical HP1 texture packages). Only
the packages whose names clash with a different HP2 package get a new
name (HProps -> H1Props, ...); every copied file has those package names
rewritten in its name table so its imports follow. The new name table is
appended at the end of the file and the header points at it, so no other
byte moves (lazy-array seek positions stay valid).
"""
import os, struct, sys, hashlib, json, subprocess

RENAME = {
    'HProps': 'H1Props', 'HPModels': 'H1Models', 'HPParticle': 'H1Particle', 'HPSounds': 'H1Sounds',
    'HPMenu': 'H1Menu', 'HPDialog': 'H1Dialog',
    'GenFX': 'H1GenFX', 'HPEdit': 'H1HPEdit', 'HP_FX': 'H1_FX', 'HP_Portraits2': 'H1_Portraits2',
    'HP_Quidditch': 'H1_Quidditch', 'Liquids': 'H1Liquids', 'MenuArt': 'H1MenuArt', 'AllDialog': 'H1AllDialog',
}
# Engine-level packages: HP1 content uses HP2's own copies.
ENGINE = {'core', 'engine', 'editor', 'fire', 'uwindow', 'unrealshare', 'ipdrv', 'ipserver', 'ubrowser', 'uweb'}
CODE = ['HarryPotter', 'HPBase', 'Hog1', 'Hog2', 'Hub2', 'Hub3', 'Hub4', 'Hub5', 'Tut1', 'Tut2', 'Tut3', 'DProps',
        'HPDialog', 'HPMenu', 'HPModels', 'HProps', 'HPParticle', 'HPSounds']
INTS = {'HPMenu.int': 'H1Menu.int', 'hpdialog.int': 'H1Dialog.int', 'hp.int': 'hp.int', 'Pickup.int': 'Pickup.int',
        'Pickup2.int': 'Pickup2.int', 'Multimesh.int': 'Multimesh.int'}
SKIP_MAPS = {'entry.unr', 'startup.unr'}
IS_MAP = False


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
    out = bytearray(); a = abs(v)
    b0 = (0x80 if v < 0 else 0) | (a & 0x3f); a >>= 6
    if a:
        b0 |= 0x40
    out.append(b0)
    while a:
        b = a & 0x7f; a >>= 7
        if a:
            b |= 0x80
        out.append(b)
    return bytes(out)


# Imports HP1 resolves differently from HP2: music kept inside HP1's
# Engine.u (also shipped as .umx files) and two group paths HP1's linker
# forgave.
MUSIC_IN_ENGINE = {'JS_StoryBook_v2_mx', 'JS_HP_Title_Screen_v2'}
COLLAPSE = [('H1HPEdit', 'Icons', 'Icons'), ('H1Sounds', 'Hub3_sfx', 'Hub5_sfx')]


CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wavcache')


def to_wav(raw, kind):
    """HP2 plays 8/16-bit PCM WAV only; HP1 stores most sounds as MPEG layer 2."""
    if raw[:4] == b'RIFF':
        return raw
    os.makedirs(CACHE, exist_ok=True)
    key = hashlib.sha1(raw).hexdigest()
    out = os.path.join(CACHE, key + '.wav')
    if not os.path.exists(out):
        src = os.path.join(CACHE, key + '.' + ('mp2' if kind.lower() in ('mp2', 'mp3', 'mpg') else kind.lower()))
        open(src, 'wb').write(raw)
        subprocess.run(['/usr/bin/afconvert', '-f', 'WAVE', '-d', 'LEI16', src, out + '.tmp'], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.replace(out + '.tmp', out)
        os.remove(src)
    return plain_wav(open(out, 'rb').read())


def plain_wav(wav):
    """Keeps only the fmt and data chunks (afconvert adds FLLR padding)."""
    i = 12; fmt = data = None
    while i + 8 <= len(wav):
        cid = wav[i:i + 4]; clen = struct.unpack_from('<I', wav, i + 4)[0]
        if cid == b'fmt ':
            fmt = wav[i:i + 8 + clen]
        elif cid == b'data':
            data = wav[i:i + 8 + clen]
        i += 8 + clen + (clen & 1)
    body = b'WAVE' + fmt + data
    return b'RIFF' + struct.pack('<I', len(body)) + body


def wav_seconds(wav):
    i = 12; rate = dlen = 0
    while i + 8 <= len(wav):
        cid = wav[i:i + 4]; clen = struct.unpack_from('<I', wav, i + 4)[0]
        if cid == b'fmt ':
            rate = struct.unpack_from('<I', wav, i + 16)[0]
        elif cid == b'data':
            dlen = clen
        i += 8 + clen + (clen & 1)
    return dlen / float(rate) if rate else -1.0


def rewrite(data):
    """Renames clashing package names and fixes imports; returns (data, notes)."""
    tag, ver, flags, nc, no, ec, eo, ic, io = struct.unpack_from('<IIIIIIIII', data, 0)
    assert tag == 0x9E2A83C1, 'not a package'
    names = []; p = no
    old = (ver & 0xffff) < 64                         # older packages: plain C strings
    for _ in range(nc):
        if old:
            e = data.index(b'\0', p); st = data[p:e].decode('latin1'); p = e + 1
        else:
            ln, p = read_ci(data, p)
            st = data[p:p + ln - 1].decode('latin1') if ln > 0 else ''
            p += max(ln, 0)
        fl = struct.unpack_from('<I', data, p)[0]; p += 4
        names.append([st, fl])
    imps = []; p = io
    for _ in range(ic):
        a, p = read_ci(data, p); b, p = read_ci(data, p)
        o = struct.unpack_from('<i', data, p)[0]; p += 4
        n, p = read_ci(data, p)
        imps.append([a, b, o, n])
    notes = [n for n, _ in names if n in RENAME]
    for nm in names:
        nm[0] = RENAME.get(nm[0], nm[0])

    def name(s):
        for i, (n, _) in enumerate(names):
            if n.lower() == s.lower():
                return i
        names.append([s, 0x00070010]); return len(names) - 1

    def nm(i):
        return names[imps[i][3]][0]
    changed = bool(notes)
    # In the levels, HP1's placed Harry becomes HP2's harry: the player takes
    # over the placed PlayerPawn, and the mod and HP2's spells need HGame's.
    if IS_MAP:
        for i, im in enumerate(imps):
            if names[im[1]][0] == 'Class' and im[2] < 0 and (nm(i).lower(), nm(-im[2] - 1)) in (
                    ('harry', 'HarryPotter'), ('invisibleharry', 'Hub4'), ('harryl502', 'Hub5')):
                imps.append([name('Core'), name('Package'), 0, name('HGame')])
                im[2] = -len(imps)
                im[3] = name('harry')
                notes.append('harry -> HGame.harry'); changed = True
    for i, im in enumerate(imps):
        o = im[2]
        if names[im[1]][0] == 'Music' and nm(i) in MUSIC_IN_ENGINE and o < 0 and nm(-o - 1) == 'Engine':
            imps.append([name('Core'), name('Package'), 0, name(nm(i))])
            im[2] = -len(imps)
            notes.append('music ' + nm(i)); changed = True
        for root, mid, leaf in COLLAPSE:
            if nm(i) == leaf and o < 0 and nm(-o - 1) == mid:
                oo = imps[-o - 1][2]
                if oo < 0 and nm(-oo - 1) in (root, leaf) and not (mid == leaf and nm(-oo - 1) == root):
                    im[2] = oo
                    notes.append('path %s.%s.%s' % (root, mid, leaf)); changed = True
                elif mid == leaf and oo < 0 and nm(-oo - 1) == root:
                    # Icons.Icons.x: move the children up to the outer Icons.
                    for ch in imps:
                        if ch[2] == -(i + 1):
                            ch[2] = o
                    im[2] = oo                   # the inner one becomes a duplicate of the outer
                    notes.append('path %s.%s.%s' % (root, mid, leaf)); changed = True
    # Sounds: HP2 reads CoreFlags and Duration after FileType from version
    # 76 on, HP1's version 76 sounds do not have them. Those exports are
    # written anew at the end of the file; every other export stays where
    # it was, so its lazy-array seek positions stay valid.
    exps = []; p = eo
    for _ in range(ec):
        c, p = read_ci(data, p); sp, p = read_ci(data, p)
        pk = struct.unpack_from('<i', data, p)[0]; p += 4
        n, p = read_ci(data, p); fl = struct.unpack_from('<I', data, p)[0]; p += 4
        sz, p = read_ci(data, p); off = 0
        if sz > 0:
            off, p = read_ci(data, p)
        exps.append([c, sp, pk, n, fl, sz, off])

    def cls_name(c):
        return names[imps[-c - 1][3]][0] if c < 0 else (names[exps[c - 1][3]][0] if c > 0 else 'Class')
    tail = bytearray()
    nsounds = 0
    if 62 <= (ver & 0xffff) <= 76:
        for e in exps:
            if e[5] > 0 and cls_name(e[0]) == 'Music':
                # HP2 streams its music from .ogg files and keeps no data in
                # Music objects; HP1's tracks stay referable but silent here.
                start = len(data) + len(tail)
                tail += write_ci(name('None'))         # just 'None': no properties
                e[5], e[6] = 1, start
                nsounds += 1
                continue
            if e[5] <= 0 or cls_name(e[0]) != 'Sound':
                continue
            q = e[6]
            while True:                               # tagged properties
                n, q = read_ci(data, q)
                if names[n][0] == 'None':
                    break
                info = data[q]; q += 1
                t = info & 0xf; sz = (info >> 4) & 7
                if t == 10:
                    _, q = read_ci(data, q)
                if sz < 5:
                    size = [1, 2, 4, 12, 16][sz]
                elif sz == 5:
                    size = data[q]; q += 1
                elif sz == 6:
                    size = struct.unpack_from('<H', data, q)[0]; q += 2
                else:
                    size = struct.unpack_from('<I', data, q)[0]; q += 4
                if info & 0x80 and t != 3:
                    b = data[q]; q += 1 if b < 128 else (2 if b & 0xc0 == 0x80 else 4)
                q += 0 if t == 3 else size
            props = data[e[6]:q]
            ft, q = read_ci(data, q)                  # FileType
            q += 4                                    # old SeekPos
            cnt, q2 = read_ci(data, q)
            raw = data[q2:q2 + cnt]                   # HP1 keeps 28 more bytes after the data; HP2 has no use for them
            wav = to_wav(raw, names[ft][0])
            dur = wav_seconds(wav)
            head = props + write_ci(name('WAV'))
            start = len(data) + len(tail)
            body = bytes(head) + struct.pack('<if', 0, dur)
            lazy = write_ci(len(wav)) + wav               # the WAV's own size, not the MP2's
            seek = start + len(body) + 4 + len(lazy)
            blob = body + struct.pack('<i', seek) + lazy
            e[5], e[6] = len(blob), start
            tail += blob; nsounds += 1
        if nsounds:
            notes.append('%d sounds/music' % nsounds); changed = True
    if not changed:
        return data, []
    nblob = bytearray()
    for st, fl in names:
        b = st.encode('latin1') + b'\0'
        nblob += (b'' if old else write_ci(len(b))) + b + struct.pack('<I', fl)
    iblob = bytearray()
    for a, b, o, n in imps:
        iblob += write_ci(a) + write_ci(b) + struct.pack('<i', o) + write_ci(n)
    eblob = bytearray()
    for c, sp, pk, n, fl, sz, off in exps:
        eblob += write_ci(c) + write_ci(sp) + struct.pack('<i', pk) + write_ci(n) + struct.pack('<I', fl) + write_ci(sz)
        if sz > 0:
            eblob += write_ci(off)
    out = bytearray(data)
    base = len(data) + len(tail)
    struct.pack_into('<II', out, 12, len(names), base)                                  # names appended
    struct.pack_into('<II', out, 20, len(exps), base + len(nblob) + len(iblob))         # exports last
    struct.pack_into('<II', out, 28, len(imps), base + len(nblob))                      # imports
    return bytes(out) + bytes(tail) + bytes(nblob) + bytes(iblob) + bytes(eblob), notes


def plan(G, H):
    global RETAIL
    RETAIL = json.load(open(os.path.join(H, 'overlay-manifest.json')))['entries']
    jobs = []
    # HP1's levels are not installed: the mod only uses HP1's assets and voices.
    for d in ('System', 'Textures', 'Sounds', 'Music') + (('Maps',) if '--maps' in sys.argv else ()):
        # Only the game's own files count as a clash; ours are rewritten.
        have = {os.path.basename(e['path']).lower(): os.path.basename(e['path']) for e in RETAIL if e['path'].startswith(d + '/')}
        for f in sorted(os.listdir(os.path.join(G, d))):
            if f.startswith('._'):
                continue
            base, ext = os.path.splitext(f)
            src = os.path.join(G, d, f)
            if d == 'System':
                if ext.lower() == '.int' and f in INTS:
                    jobs.append((src, os.path.join(H, d, INTS[f]), False)); continue
                if ext.lower() != '.u' or base not in CODE:
                    continue
            elif d == 'Maps':
                if f.lower() in SKIP_MAPS:
                    continue
            elif ext.lower() not in ('.utx', '.uax', '.umx'):
                continue
            if base.lower() in ENGINE:
                continue
            out = RENAME.get(base, base) + ext
            if out.lower() in have and RENAME.get(base) is None:
                # Same name already in HP2: identical files are shared.
                if hashlib.md5(open(src, 'rb').read()).digest() == hashlib.md5(open(os.path.join(H, d, have[out.lower()]), 'rb').read()).digest():
                    continue
                raise SystemExit('unexpected clash: %s/%s' % (d, f))
            jobs.append((src, os.path.join(H, d, out), True))
    return jobs


def main():
    G, H = sys.argv[1], sys.argv[2]
    dry = '--dry' in sys.argv
    total = 0
    for src, dst, pkg in plan(G, H):
        data = open(src, 'rb').read()
        hits = []
        if pkg:
            global IS_MAP
            IS_MAP = dst.lower().endswith('.unr')
            data, hits = rewrite(data)
        total += len(data)
        print('%-60s -> %s %s' % (os.path.relpath(src, G), os.path.relpath(dst, H), ("fix " + ",".join(hits)) if hits else ''))
        if not dry:
            if os.path.exists(dst):
                old = open(dst, 'rb').read()
                if old == data:
                    continue
            tmp = dst + '.tmp'
            open(tmp, 'wb').write(data)
            os.replace(tmp, dst)
    print('total %.1f MB%s' % (total / 1e6, ' (dry run)' if dry else ''))


if __name__ == '__main__':
    main()

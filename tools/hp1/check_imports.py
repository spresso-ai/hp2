"""Lists imports of the installed HP1 packages that the HP2 data does not
have: per missing object its class and which HP1 packages ask for it.

  python3 -I check_imports.py <HP2 Data/Unreal dir> <install.log>
"""
import os, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m212 import Pkg

H = sys.argv[1]
FIND = {}


def find_pkg(name):
    low = name.lower()
    if low in FIND:
        return FIND[low]
    hit = None
    for d in ('System', 'Textures', 'Sounds', 'Music', 'Maps'):
        for f in os.listdir(os.path.join(H, d)):
            b, e = os.path.splitext(f)
            if b.lower() == low and e.lower() in ('.u', '.utx', '.uax', '.umx', '.unr', '.usa_uax'):
                hit = os.path.join(H, d, f); break
        if hit:
            break
    if hit is None and low == 'alldialog':
        hit = os.path.join(H, 'Sounds', 'AllDialog.USA_uax')
    FIND[low] = Pkg(hit) if hit else None
    return FIND[low]


def epath(pk, i):
    e = pk.exps[i]
    return ((epath(pk, e['pkg'] - 1) + '.') if e['pkg'] else '') + pk.names[e['name']][0]


EXPORTS = {}


def exports_of(pk):
    if id(pk) not in EXPORTS:
        EXPORTS[id(pk)] = {epath(pk, i).lower() for i in range(len(pk.exps))}
    return EXPORTS[id(pk)]


def ipath(pk, r):
    a, b, o, n = pk.imps[-r - 1]
    return ((ipath(pk, o) + '.') if o < 0 else '') + pk.names[n][0]


missing = collections.defaultdict(set)
files = [l.split('-> ')[1].split()[0] for l in open(sys.argv[2]) if '-> ' in l]
for rel in files:
    if not rel.endswith(('.u', '.utx', '.uax', '.umx', '.unr')):
        continue
    pk = Pkg(os.path.join(H, rel))
    for i, (a, b, o, n) in enumerate(pk.imps):
        if o == 0:
            continue                      # a package itself
        full = ipath(pk, -(i + 1))
        root = full.split('.')[0]
        src = find_pkg(root)
        cls = pk.names[b][0]
        if src is None:
            missing[('NOPKG ' + root, cls)].add(rel); continue
        rest = '.'.join(full.split('.')[1:]).lower()
        if rest not in exports_of(src):
            missing[(full, cls)].add(rel)
by_root = collections.Counter(k[0].split('.')[0] for k in missing)
print('missing imports by package:', dict(by_root))
for (full, cls), users in sorted(missing.items()):
    print('%-60s %-14s %s' % (full, cls, ','.join(sorted(os.path.basename(u) for u in users))[:120]))

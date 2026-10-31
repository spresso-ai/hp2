"""Prints the animation sequence names of HP1 Animation objects (heuristic scan)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m212 import Pkg, read_ci

def seqs(pk, e):
    d = pk.d[e['off']:e['off'] + e['size']]
    out = []; i = 0; n = len(pk.names)
    while i < len(d) - 14:
        try:
            a, j = read_ci(d, i); b, j = read_ci(d, j)
            if 0 < a < n and 0 <= b < n:
                s, c = struct.unpack_from('<ii', d, j); j += 8
                if 0 <= s < 100000 and 0 < c < 5000 and d[j] == 0:
                    r = struct.unpack_from('<f', d, j + 1)[0]
                    if 1.0 <= r <= 120.0 and r == round(r, 3):
                        out.append(pk.names[a][0]); i = j + 5; continue
        except Exception:
            pass
        i += 1
    return out

if __name__ == '__main__':
    pk = Pkg(sys.argv[1])
    want = sys.argv[2:]
    for e in pk.exps:
        nm = pk.names[e['name']][0]
        if pk.cname(e) == 'Animation' and (not want or nm in want):
            print(nm, ' '.join(seqs(pk, e)))

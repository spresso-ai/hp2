"""Inventory of HP1 packages: actor classes per map."""
import sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m212 import Pkg

def path(pk, r):
    if r == 0: return ''
    if r < 0:
        a, b, o, n = pk.imps[-r - 1]
        return (path(pk, o) + '.' if o else '') + pk.names[n][0]
    e = pk.exps[r - 1]
    return (path(pk, e['pkg']) + '.' if e['pkg'] else '') + pk.names[e['name']][0]

if __name__ == '__main__':
    G = sys.argv[1]
    for f in sorted(os.listdir(G + '/Maps')):
        if f.startswith('._'): continue
        pk = Pkg(G + '/Maps/' + f)
        cnt = collections.Counter()
        for e in pk.exps:
            if e['cls'] < 0:
                full = path(pk, e['cls'])
                if full.split('.')[0] in ('Engine', 'Core', 'Fire', 'Editor', 'UnrealShare'): continue
                cnt[full] += 1
        print('==', f, len(pk.exps))
        print('   ', ', '.join('%s x%d' % kv for kv in cnt.most_common(45)))

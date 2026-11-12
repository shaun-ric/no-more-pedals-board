"""Validate a generated RigBench library folder.

Usage: python3 validate_library.py <library root> <asset root> [<asset root> ...]
Checks that every scene preset only uses nodes and edges that exist in its scene, that every
model/IR a node preset names is the file its scene node loads (no swaps inside a song), that
every referenced NAM/IR file exists under one of the asset roots, and that scene preset slots
are unique per scene.
"""
import glob, json, os, sys

root = sys.argv[1]
assets = sys.argv[2:]
missing = set()
for sc in glob.glob(f'{root}/scenes/*.scene.json'):
    d = json.load(open(sc)); sid = os.path.basename(sc)[:-len('.scene.json')]
    nodes = {n['id']: n for n in d['graph']['nodes']}
    edges = {e['from'] + '>' + e['to'] for e in d['graph']['edges']}
    for n in nodes.values():
        f = n.get('file')
        if f and not any(os.path.exists(os.path.join(a, f[4:])) for a in assets):
            missing.add(f)
    slots = []
    for pf in glob.glob(f'{root}/scenes/{sid}/presets/*.json'):
        p = json.load(open(pf)); slots.append(p['slot'])
        for n in p['nodes']:
            assert n['node'] in nodes, (sid, n)
            if n['preset']:
                hits = glob.glob(f"{root}/presets/*/{n['preset']}.preset.json")
                assert hits, ('missing node preset', sid, n)
                nf = json.load(open(hits[0])).get('file')
                if nf:
                    assert nf == nodes[n['node']].get('file'), ('file differs from scene node', sid, n, nf)
        for e in p['edges']:
            assert e['edge'] in edges, (sid, e)
    assert len(set(slots)) == len(slots), ('duplicate slot', sid)
print('missing files:', sorted(missing) or 'none')
print('ok')

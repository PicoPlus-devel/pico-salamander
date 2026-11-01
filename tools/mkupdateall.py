#!/usr/bin/env python3
"""Writes picoSalamander's sets into pico-bootLoader's updateAll.json.

salamand/sal_sets.c is the single source of truth for the ROM sets: for each
game the folder, the MAME set, its versions in the order they are preferred,
and the files with their sizes and CRC32s. updateAll installs one version per
game, the first: Salamander version D (salamand) and Lifeforce US (lifefrce).
Lifeforce is a clone of salamand in MAME, so its zips are the parent's (a
merged salamand.zip, which the download database provides, holds lifefrce as
well) and then its own.

    tools/mkupdateall.py ../pico-bootLoader/updateAll/updateAll.json

An entry with the game's destination folder is replaced in place and keeps its
id; a game without one gets a new entry after the last set below /roms/arcade.
Only the "cores" list is touched, and every other entry in it is kept exactly
as it is written. With --print the entries are printed instead of written.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SETS_C = os.path.join(HERE, '..', 'salamand', 'sal_sets.c')
ROMROOT = '/roms/arcade'

# MAME's parent of a set, where sal_sets.c names a clone as a game's own set
PARENT = {'lifefrce': 'salamand'}


def strings(s):
    return re.findall(r'"([^"]*)"', s)


def parse_sets(path):
    src = open(path, encoding='utf-8').read()
    tables = {}
    for m in re.finditer(r'static const sal_romfile_t (\w+)\[\] = \{(.*?)\n\};', src, re.S):
        tables[m.group(1)] = [
            (name, int(crc, 16), int(size, 16), var.strip()) for name, crc, size, var in
            re.findall(r'\{"([^"]+)", 0x([0-9a-fA-F]{8}), 0x([0-9a-fA-F]+), \w+, \d+, 0x[0-9a-fA-F]+, ([^}]+)\}',
                       m.group(2))]
    # a file's versions, as sal_variant_files() reads them: 0 for every
    # version, else bit v for version v
    masks = {k: int(v, 16) for k, v in re.findall(r'#define (V_\w+) (0x[0-9a-fA-F]+)', src)}

    def mask(var):
        if var == '0':
            return ~0
        return sum(masks[v.strip()] for v in var.split('|'))

    sets = []
    for m in re.finditer(r'\[SAL_SET_(\w+)\] =\s*\{(.*?)\n        \},', src, re.S):
        body = m.group(2)
        field = lambda f: re.search(r'\.%s = (.*?),\n' % f, body).group(1)
        title, folder, mameset = (strings(field(f))[0] for f in ('title', 'folder', 'mameset'))
        variants = strings(re.search(r'\.variant_set = \{(.*?)\}', body).group(1))
        vnames = strings(re.search(r'\.variant_name = \{(.*?)\}', body).group(1))
        # the first version's files: those of every version, and its own
        chosen = [(n, c, s) for n, c, s, var in tables[field('files')] if mask(var) & 1]
        v0 = variants[0]
        parent = mameset if v0 != mameset else PARENT.get(v0)
        if parent:
            desc, zips = 'MAME %s (%s), a clone of %s' % (v0, vnames[0], parent), [parent + '.zip', v0 + '.zip']
        else:
            desc, zips = 'MAME %s (%s)' % (v0, vnames[0]), [v0 + '.zip']
        sets.append({
            'id': mameset,
            'name': title,
            'set': desc,
            'zips': zips,
            'destination': ROMROOT + '/' + folder,
            'files': [{'name': n, 'size': s, 'crc': '%08x' % c} for n, c, s in chosen],
        })
    if not sets:
        raise SystemExit('no sets found in ' + path)
    return sets


def render_entry(e, indent='    '):
    """One entry as updateAll.json writes it: a file per line, columns aligned."""
    i2 = indent + '  '
    lines = [indent + '{']
    lines.append('%s"id": %s,' % (i2, json.dumps(e['id'])))
    lines.append('%s"name": %s,' % (i2, json.dumps(e['name'], ensure_ascii=False)))
    lines.append('%s"set": %s,' % (i2, json.dumps(e['set'], ensure_ascii=False)))
    lines.append('%s"zips": [%s],' % (i2, ', '.join(json.dumps(z) for z in e['zips'])))
    lines.append('%s"destination": %s,' % (i2, json.dumps(e['destination'])))
    lines.append('%s"files": [' % i2)
    names = ['"name": %s,' % json.dumps(f['name']) for f in e['files']]
    sizes = ['"size": %d,' % f['size'] for f in e['files']]
    wn = max(len(n) for n in names)
    ws = max(len(s) for s in sizes)
    for k, f in enumerate(e['files']):
        comma = ',' if k < len(e['files']) - 1 else ''
        lines.append('%s  { %s %s "crc": "%s" }%s' % (i2, names[k].ljust(wn), sizes[k].ljust(ws), f['crc'], comma))
    lines.append(i2 + ']')
    lines.append(indent + '}')
    return '\n'.join(lines)


def split_cores(text):
    """The file's lines before the "cores" entries, the entries' texts (each
    without its trailing comma) and the lines after them."""
    lines = text.split('\n')
    try:
        start = lines.index('  "cores": [') + 1
    except ValueError:
        raise SystemExit('no "cores": [ line found')
    end = start
    while end < len(lines) and not lines[end].startswith('  ]'):
        end += 1
    blocks, cur = [], None
    for line in lines[start:end]:
        if line == '    {':
            cur = [line]
        elif cur is not None:
            cur.append(line.rstrip(',') if line in ('    },', '    }') else line)
            if line in ('    },', '    }'):
                blocks.append('\n'.join(cur))
                cur = None
        elif line.strip():
            raise SystemExit('unexpected line in "cores": %r' % line)
    return lines[:start], blocks, lines[end:]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    sets = parse_sets(SETS_C)
    if '--print' in sys.argv or not args:
        print(',\n'.join(render_entry(s) for s in sets))
        return
    path = args[0]
    text = open(path, encoding='utf-8').read()
    before = json.loads(text)
    head, blocks, tail = split_cores(text)
    entries = [json.loads(b) for b in blocks]
    if entries != before['cores']:
        raise SystemExit('%s: the "cores" entries are not laid out as this script expects; not touching it' % path)

    added = 0
    for s in sets:
        dests = [e['destination'] for e in entries]
        if s['destination'] in dests:
            i = dests.index(s['destination'])
            s = dict(s, id=entries[i]['id'])  # keep ids others rely on
            entries[i], blocks[i] = s, render_entry(s)
        else:
            arcade = [i for i, d in enumerate(dests) if d.startswith(ROMROOT + '/')]
            i = arcade[-1] + 1 if arcade else len(entries)
            entries.insert(i, s)
            blocks.insert(i, render_entry(s))
            added += 1
    ids = [e['id'] for e in entries]
    dup = set(i for i in ids if ids.count(i) > 1)
    if dup:
        raise SystemExit('duplicate ids: ' + ', '.join(sorted(dup)))

    out = '\n'.join(head) + '\n' + ',\n'.join(blocks) + '\n' + '\n'.join(tail)
    after = json.loads(out)
    ours = {s['destination'] for s in sets}
    keep = lambda doc: dict(doc, cores=[e for e in doc['cores'] if e['destination'] not in ours])
    if keep(after) != keep(before):
        raise SystemExit('internal error: the rest of the file would change')
    open(path, 'w', encoding='utf-8').write(out)
    print('%s: %d sets, %d added, %d entries in all' % (path, len(sets), added, len(entries)))


if __name__ == '__main__':
    main()

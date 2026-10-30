#!/usr/bin/env python3
"""Stage R87 Omni-Bot 0.93 soldier weapon table from a stock script.

Non-destructive: input never written; output must not exist unless byte-identical.
The precise anchor is checked so future upstream changes cannot be silently
lost. Only ET:Legacy gets ID 77; other mod weapon profiles unchanged.
"""
import argparse
from pathlib import Path

ANCHOR = 'PrimaryWeaponChoices[TEAM.AXIS][CLASS.SOLDIER] = tbl1;'
INJECTION = '''// Bombas R87: M1897 is Soldier-only on both teams, never other mods.
// This matches ET_WP_BOMBAS_M1897=77 in the native ET bot bridge.
if (isLegacy) {
    tbl1[77] = true;
    tbl2[77] = true;
}
'''

def stage(source: Path, dest: Path):
    original = source.read_text()
    if original.count(ANCHOR) != 1 or original.count('isLegacy = modName == "legacy";') != 1:
        raise ValueError('Unknown OmniBot weapon-table format; refuse destructive patch')
    if INJECTION.strip() in original:
        patched = original
    else:
        patched = original.replace(ANCHOR, INJECTION + ANCHOR, 1)
    if dest.exists() or dest.is_symlink():
        if dest.read_text() == patched:
            print('ALREADY_STAGED_IDENTICAL', dest)
            return
        raise FileExistsError('Refuse to overwrite existing OmniBot script: '+str(dest))
    dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_text(patched)
    assert source.read_text() == original
    print('STAGED_ISOLATED_R87_SOLDIER_TABLE',dest,'bytes',len(patched))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',required=True,type=Path);p.add_argument('--dest',required=True,type=Path);a=p.parse_args();stage(a.source,a.dest)

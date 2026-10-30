#!/usr/bin/env python3
"""Create R89 Omni-Bot 0.93 ET:Legacy-specific M1897 class overlay."""
import argparse
from pathlib import Path

SOLDIER = 'PrimaryWeaponChoices[TEAM.AXIS][CLASS.SOLDIER] = tbl1;'
COMMON = '\tPrimaryWeaponChoices[TEAM.AXIS][class] = tbl1;'
INSERT = '''\tif (isLegacy) {
\t\ttbl1[77] = true;
\t\ttbl2[77] = true;
\t}
'''

p=argparse.ArgumentParser()
p.add_argument('--source',type=Path,required=True)
p.add_argument('--dest',type=Path,required=True)
a=p.parse_args()
s=a.source.read_text()
if s.count(SOLDIER)!=1 or s.count(COMMON)!=1 or s.count('isLegacy = modName == "legacy";')!=1:
    raise SystemExit('Unexpected Omni-Bot table, refusing modification')
old='''// Bombas R87: M1897 is Soldier-only on both teams, never other mods.
// This matches ET_WP_BOMBAS_M1897=77 in the native ET bot bridge.
if (isLegacy) {
    tbl1[77] = true;
    tbl2[77] = true;
}
'''
if old in s:
    s=s.replace(old,'',1)
if s.count(INSERT)!=0:
    raise SystemExit('R89 table already injected or modified unexpectedly')
s=s.replace(COMMON,INSERT+COMMON,1)
if a.dest.exists() or a.dest.is_symlink():
    if a.dest.read_text()!=s:raise SystemExit('Existing different destination, no overwrite')
else:
    a.dest.parent.mkdir(parents=True,exist_ok=True)
    a.dest.write_text(s)
print('R89_TABLE_OK',a.dest)

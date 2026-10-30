#!/usr/bin/env python3
import argparse
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument('--source',type=Path,required=True)
p.add_argument('--dest',type=Path,required=True)
a=p.parse_args()
s=a.source.read_text()
for old,new in [
 ('PrimaryWeaponChoices[TEAM.AXIS][CLASS.SOLDIER] = tbl1;', 'if (isLegacy) {\n\ttbl1[79] = true;\n\ttbl2[80] = true;\n}\nPrimaryWeaponChoices[TEAM.AXIS][CLASS.SOLDIER] = tbl1;'),
 ('PrimaryWeaponChoices[TEAM.AXIS][CLASS.COVERTOPS] = tbl1;', 'if (isLegacy) {\n\ttbl1[79] = true;\n\ttbl2[80] = true;\n}\nPrimaryWeaponChoices[TEAM.AXIS][CLASS.COVERTOPS] = tbl1;'),
 ('\t\ttbl1[77] = true;\n\t\ttbl2[77] = true;', '\t\ttbl1[77] = true;\n\t\ttbl2[77] = true;\n\t\ttbl1[78] = true;\n\t\ttbl2[78] = true;')]:
 if s.count(old)!=1:raise SystemExit('Unexpected legacy weapon tables')
 s=s.replace(old,new,1)
a.dest.parent.mkdir(parents=True,exist_ok=True)
if a.dest.exists():
 if a.dest.read_text()!=s:raise SystemExit('Existing target differs')
else:a.dest.write_text(s)
print('R94_BOT_CLASS_MATRIX_READY',a.dest)

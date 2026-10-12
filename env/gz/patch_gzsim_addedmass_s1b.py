#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""S1 补丁 b：Physics.cc 补 include <gz/physics/AddedMass.hh>。"""
import shutil
import sys
from pathlib import Path

P = Path("/home/keship/gz-build/gz-sim-8.11.0/src/systems/physics/Physics.cc")
OLD = "#include <gz/physics/BoxShape.hh>"
NEW = "#include <gz/physics/AddedMass.hh>\n#include <gz/physics/BoxShape.hh>"

s = P.read_text(encoding="utf-8")
n = s.count(OLD)
if n != 1:
    print("  [FAIL] 匹配数=%d（应为 1）" % n)
    sys.exit(1)
if "gz/physics/AddedMass.hh" in s:
    print("  [SKIP] 已存在")
    sys.exit(0)
shutil.copy2(str(P), str(P) + ".bak_s1b")
P.write_text(s.replace(OLD, NEW), encoding="utf-8")
print("  [OK] 已补 include <gz/physics/AddedMass.hh>")

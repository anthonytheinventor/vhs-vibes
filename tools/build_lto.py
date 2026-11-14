#!/usr/bin/env python3
"""build_lto.py - runs the SDK's tools/build.py with link-time optimisation (-flto) switched on,
without changing the SDK.

    python3 "plugins/VHS Vibes/tools/build_lto.py" "plugins/VHS Vibes" [any build.py options]

Everything else is exactly tools/build.py (targets, validation, skins, output folder).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(SDK, "tools"))
import build  # noqa: E402

build.COMMON_FLAGS.append("-flto")
print("build_lto: link-time optimisation on (-flto)")
sys.exit(build.main(sys.argv[1:]))

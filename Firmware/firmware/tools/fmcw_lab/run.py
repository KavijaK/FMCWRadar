#!/usr/bin/env python3
"""Launch the FMCW Lab GUI."""

from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.fmcw_lab.app import main


if __name__ == "__main__":
    main()

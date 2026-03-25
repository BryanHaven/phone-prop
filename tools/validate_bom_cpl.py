#!/usr/bin/env python3
"""
validate_bom_cpl.py

Validates EasyEDA Pro BOM and CPL exports before JLCPCB submission.
Run from the project root:
  python3 tools/validate_bom_cpl.py hardware/slic-daughterboard/exports/

Checks:
  - BOM has required columns
  - All LCSC part numbers are plausible (C + digits format)
  - DNP components are flagged consistently
  - CPL has all components from BOM (except DNP)
  - CPL rotation values are within expected ranges
  - Package names match expected formats for JLCPCB
  - Warns on known problematic packages (LGA, QFN) that need rotation verification
"""

import sys
import csv
import os
import re
from pathlib import Path


# ─── Known packages requiring rotation verification at JLCPCB ──────────────
ROTATION_VERIFY_PACKAGES = {
    "LGA-42",    # Si32177 — often needs 0° or 180° correction
    "QFN-48",    # W5500 — verify orientation
    "QFN-32",
    "SOT-23-5",  # LP2985 — usually 0° but verify
    "SOP-8",     # CH340N
}

# ─── Required BOM columns (EasyEDA Pro export format) ─────────────────────
BOM_REQUIRED_COLS = {"Comment", "Designator", "Footprint", "LCSC Part #"}

# ─── Required CPL columns ─────────────────────────────────────────────────
CPL_REQUIRED_COLS = {"Designator", "Mid X", "Mid Y", "Layer", "Rotation"}


def validate_bom(bom_path: Path) -> list[str]:
    issues = []
    if not bom_path.exists():
        return [f"BOM file not found: {bom_path}"]

    with open(bom_path, newline="", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        cols = set(reader.fieldnames or [])

        missing = BOM_REQUIRED_COLS - cols
        if missing:
            issues.append(f"BOM missing columns: {missing}")
            return issues

        lcsc_col = next((c for c in cols if "LCSC" in c.upper()), None)
        dnp_components = []
        all_designators = []

        for i, row in enumerate(reader, start=2):
            ref  = row.get("Designator", "").strip()
            lcsc = row.get(lcsc_col, "").strip() if lcsc_col else ""
            pkg  = row.get("Footprint", "").strip()
            comment = row.get("Comment", "").strip().upper()

            all_designators.append(ref)

            # Flag DNP components
            if "DNP" in comment or "DO NOT POPULATE" in comment:
                dnp_components.append(ref)
                continue

            # Validate LCSC part number format
            if lcsc and not re.match(r"^C\d+$", lcsc):
                issues.append(f"Row {i} [{ref}]: suspicious LCSC part# '{lcsc}' "
                               f"(expected C followed by digits)")

            # Warn on empty LCSC for non-DNP parts
            if not lcsc:
                issues.append(f"Row {i} [{ref}]: no LCSC part number (add or mark DNP)")

            # Warn on rotation-sensitive packages
            for rpkg in ROTATION_VERIFY_PACKAGES:
                if rpkg.lower() in pkg.lower():
                    issues.append(f"⚠ ROTATION CHECK [{ref}]: package '{pkg}' "
                                  f"requires CPL rotation verification at JLCPCB")
                    break

    if dnp_components:
        print(f"  DNP components (will not be assembled): {', '.join(dnp_components)}")

    return issues


def validate_cpl(cpl_path: Path, bom_path: Path) -> list[str]:
    issues = []
    if not cpl_path.exists():
        return [f"CPL file not found: {cpl_path}"]

    # Load BOM designators (excluding DNP)
    bom_designators = set()
    dnp_designators = set()
    if bom_path.exists():
        with open(bom_path, newline="", encoding="utf-8-sig") as f:
            reader = csv.DictReader(f)
            lcsc_col = next((c for c in (reader.fieldnames or []) if "LCSC" in c.upper()), None)
            for row in reader:
                ref     = row.get("Designator", "").strip()
                comment = row.get("Comment", "").strip().upper()
                if "DNP" in comment or "DO NOT POPULATE" in comment:
                    dnp_designators.add(ref)
                else:
                    bom_designators.add(ref)

    # Load CPL
    cpl_designators = set()
    with open(cpl_path, newline="", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        cols = set(reader.fieldnames or [])
        missing = CPL_REQUIRED_COLS - cols
        if missing:
            issues.append(f"CPL missing columns: {missing}")
            return issues

        for i, row in enumerate(reader, start=2):
            ref      = row.get("Designator", "").strip()
            rotation = row.get("Rotation", "").strip()
            layer    = row.get("Layer", "").strip()

            cpl_designators.add(ref)

            # Check rotation is a number
            try:
                rot_val = float(rotation)
                if rot_val not in (0, 90, 180, 270, -90, -180, -270):
                    issues.append(f"Row {i} [{ref}]: unusual rotation {rot_val}° — verify")
            except ValueError:
                issues.append(f"Row {i} [{ref}]: non-numeric rotation '{rotation}'")

            # Check layer
            if layer not in ("Top", "Bottom", "T", "B"):
                issues.append(f"Row {i} [{ref}]: unexpected layer '{layer}'")

    # Check BOM vs CPL coverage
    in_bom_not_cpl = bom_designators - cpl_designators
    if in_bom_not_cpl:
        issues.append(f"Components in BOM but missing from CPL: {sorted(in_bom_not_cpl)}")

    in_cpl_not_bom = cpl_designators - bom_designators - dnp_designators
    if in_cpl_not_bom:
        issues.append(f"Components in CPL but not in BOM (and not DNP): {sorted(in_cpl_not_bom)}")

    return issues


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <exports_directory>")
        print(f"  e.g. {sys.argv[0]} hardware/slic-daughterboard/exports/")
        sys.exit(1)

    exports_dir = Path(sys.argv[1])
    bom_path    = exports_dir / "bom.csv"
    cpl_path    = exports_dir / "cpl.csv"

    print(f"\n{'='*60}")
    print(f"BOM/CPL Validation: {exports_dir}")
    print(f"{'='*60}\n")

    all_issues = []

    print("Validating BOM...")
    bom_issues = validate_bom(bom_path)
    all_issues.extend(bom_issues)

    print("Validating CPL...")
    cpl_issues = validate_cpl(cpl_path, bom_path)
    all_issues.extend(cpl_issues)

    print()
    if all_issues:
        print(f"Found {len(all_issues)} issue(s):\n")
        for i, issue in enumerate(all_issues, 1):
            prefix = "⚠ " if issue.startswith("⚠") else "✗ "
            print(f"  {i}. {prefix}{issue}")
        print()
        warnings = sum(1 for i in all_issues if i.startswith("⚠"))
        errors   = len(all_issues) - warnings
        if errors:
            print(f"Result: FAIL — {errors} error(s), {warnings} warning(s)")
            sys.exit(1)
        else:
            print(f"Result: PASS WITH WARNINGS — {warnings} rotation check(s) to verify")
    else:
        print("Result: PASS — no issues found")
        print("Ready for JLCPCB submission (verify rotation offsets manually for flagged packages)")

    print()


if __name__ == "__main__":
    main()

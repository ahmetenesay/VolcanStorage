#!/usr/bin/env python3
"""
VolcanStorage Automated Multiplatform Packaging Script
Packages all 6 target distributions with ultra-high Zstandard Level 22 compression into dist/:
  - VolcanStorage-Windows-x64.zip
  - VolcanStorage-Windows-arm64.zip
  - VolcanStorage-Linux-x64.zip
  - VolcanStorage-Linux-arm64.zip
  - VolcanStorage-macOS-x64.zip
  - VolcanStorage-macOS-arm64.zip
"""

import os
import sys
import shutil
import hashlib
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DIST_DIR = REPO_ROOT / "dist"
STAGING_DIR = REPO_ROOT / "build" / "staging"
if not STAGING_DIR.exists() and (REPO_ROOT / "staging").exists():
    STAGING_DIR = REPO_ROOT / "staging"
DOCS_DIR = REPO_ROOT / "Docs"
INCLUDE_DIR = REPO_ROOT / "include"
SHADERS_DIR = REPO_ROOT / "shaders"

MANIFEST_FILES = [
    "LICENSE",
    "NOTICES.txt",
    "README.md",
    "SECURITY.md",
    "SUPPORT.md",
]

TARGETS = [
    ("win-x64", "VolcanStorage-Windows-x64.zip"),
    ("win-arm64", "VolcanStorage-Windows-arm64.zip"),
    ("linux-x64", "VolcanStorage-Linux-x64.zip"),
    ("linux-arm64", "VolcanStorage-Linux-arm64.zip"),
    ("macos-x64", "VolcanStorage-macOS-x64.zip"),
    ("macos-arm64", "VolcanStorage-macOS-arm64.zip"),
]

def sha256_file(filepath: Path) -> str:
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def sync_common_assets(stage_path: Path):
    """Ensure latest public header, compiled shader, docs, and manifests are present."""
    inc_dest = stage_path / "include" / "volcanstorage"
    inc_dest.mkdir(parents=True, exist_ok=True)
    header_src = INCLUDE_DIR / "volcanstorage" / "volcanstorage.h"
    if header_src.exists():
        shutil.copy2(header_src, inc_dest / "volcanstorage.h")

    shader_dest = stage_path / "bin" / "shaders"
    shader_dest.mkdir(parents=True, exist_ok=True)
    spv_src = SHADERS_DIR / "GDeflate.spv"
    if spv_src.exists():
        shutil.copy2(spv_src, shader_dest / "GDeflate.spv")

    docs_dest = stage_path / "docs"
    docs_dest.mkdir(parents=True, exist_ok=True)
    for doc_ext in ["*.pdf", "*.html"]:
        for doc_file in DOCS_DIR.glob(doc_ext):
            shutil.copy2(doc_file, docs_dest / doc_file.name)

    for manifest in MANIFEST_FILES:
        manifest_src = REPO_ROOT / manifest
        if manifest_src.exists():
            shutil.copy2(manifest_src, stage_path / manifest)

def package_target(target_key: str, zip_filename: str):
    stage_path = STAGING_DIR / target_key
    if not stage_path.exists():
        print(f"[-] Staging directory missing for {target_key}: {stage_path}")
        return False

    sync_common_assets(stage_path)

    DIST_DIR.mkdir(parents=True, exist_ok=True)
    output_zip = DIST_DIR / zip_filename

    # Check compression capabilities
    compression_method = getattr(zipfile, "ZIP_ZSTANDARD", zipfile.ZIP_DEFLATED)
    compress_level = 22 if compression_method == getattr(zipfile, "ZIP_ZSTANDARD", None) else 9

    method_name = "Zstandard (Level 22)" if compression_method == getattr(zipfile, "ZIP_ZSTANDARD", None) else "Deflate (Level 9)"
    print(f"[*] Packaging {zip_filename} using {method_name}...")

    # Overwrite if exists
    if output_zip.exists():
        output_zip.unlink()

    total_uncompressed = 0
    file_count = 0

    with zipfile.ZipFile(output_zip, "w", compression=compression_method, compresslevel=compress_level) as zf:
        for root, dirs, files in os.walk(stage_path):
            dirs.sort()
            files.sort()
            for file in files:
                file_path = Path(root) / file
                arcname = file_path.relative_to(stage_path).as_posix()
                zf.write(file_path, arcname=arcname)
                total_uncompressed += file_path.stat().st_size
                file_count += 1

    final_size = output_zip.stat().st_size
    ratio = (1.0 - (final_size / total_uncompressed)) * 100 if total_uncompressed > 0 else 0
    c_hash = sha256_file(output_zip)

    print(f"    -> Files: {file_count}, Uncompressed: {total_uncompressed:,} B, Compressed: {final_size:,} B ({ratio:.1f}% reduction)")
    print(f"    -> SHA256: {c_hash}")
    return True

def verify_all():
    print("\n" + "=" * 80)
    print("DISTRIBUTION VERIFICATION & MANIFEST")
    print("=" * 80)
    for _, zip_filename in TARGETS:
        zip_path = DIST_DIR / zip_filename
        if not zip_path.exists():
            print(f"[!] MISSING: {zip_filename}")
            continue
        try:
            with zipfile.ZipFile(zip_path, "r") as zf:
                entries = zf.namelist()
                print(f"[OK] {zip_filename:<35} | {zip_path.stat().st_size:>8,} bytes | {len(entries):>2} entries")
        except Exception as e:
            print(f"[FAIL] {zip_filename}: {e}")

if __name__ == "__main__":
    success_count = 0
    for target_key, zip_filename in TARGETS:
        if package_target(target_key, zip_filename):
            success_count += 1
    verify_all()
    if success_count == len(TARGETS):
        print(f"\n[SUCCESS] Successfully packaged all {len(TARGETS)} distributions to {DIST_DIR} with Zstandard-22!")
    else:
        print(f"\n[WARNING] Completed {success_count}/{len(TARGETS)} packages.")

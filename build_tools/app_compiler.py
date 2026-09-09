#!/usr/bin/env python3
import os
import sys
import json
import struct
import re
import argparse
from pathlib import Path

MAGIC = b'NKPK'
VERSION = 1

def find_nykon_app_info(c_file_path):
    """Parse NykonApp struct from a .c file to extract app name and icon path."""
    name = None
    icon = None
    color = 0xFFFFFFFF

    try:
        with open(c_file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()

        # Match NykonApp variable initialization
        # e.g.: NykonApp flappy_app = {"Flappy Bird", "apps/flappy bird/icon.png", ...};
        match = re.search(r'NykonApp\s+[a-zA-Z0-9_]+\s*=\s*\{([^}]+)\}', content, re.DOTALL)
        if match:
            body = match.group(1)
            # Find strings
            strings = re.findall(r'"([^"]*)"', body)
            if len(strings) >= 1:
                name = strings[0]
            if len(strings) >= 2:
                icon = os.path.basename(strings[1])
    except Exception as e:
        print(f"[!] Note: Could not parse C struct: {e}")

    return name, icon, color

def package_app(app_dir, output_file=None):
    app_path = Path(app_dir).resolve()
    if not app_path.exists() or not app_path.is_dir():
        print(f"[Error] Directory not found: {app_dir}")
        return False

    app_folder_name = app_path.name
    print(f"[*] Packaging app from: {app_path}")

    # Discover C source files
    c_files = list(app_path.glob("*.c"))
    app_name = app_folder_name.capitalize()
    icon_name = "icon.png"

    for c_file in c_files:
        parsed_name, parsed_icon, _ = find_nykon_app_info(c_file)
        if parsed_name:
            app_name = parsed_name
        if parsed_icon:
            icon_name = parsed_icon

    # Discover all files to pack
    all_files = []
    for root, dirs, files in os.walk(app_path):
        for f in files:
            # Skip temp or hidden files
            if f.startswith('.') or f.endswith('.o') or f.endswith('.nkpkg'):
                continue
            full_path = Path(root) / f
            rel_path = full_path.relative_to(app_path).as_posix()
            all_files.append((rel_path, full_path))

    if not all_files:
        print(f"[Error] No valid files found in {app_dir}")
        return False

    print(f"[*] App Name: {app_name}")
    print(f"[*] Files found ({len(all_files)}): {[f[0] for f in all_files]}")

    # Build manifest
    manifest = {
        "name": app_name,
        "folder": app_folder_name,
        "version": "1.0",
        "author": "Nykon Developer",
        "icon": icon_name,
        "file_count": len(all_files)
    }

    manifest_bytes = json.dumps(manifest).encode('utf-8')

    # Default output path
    if not output_file:
        out_dir = Path("src/packages")
        out_dir.mkdir(parents=True, exist_ok=True)
        # Clean file name
        safe_name = re.sub(r'[^a-zA-Z0-9_\-]', '_', app_name.lower())
        output_file = out_dir / f"{safe_name}.nkpkg"
    else:
        output_file = Path(output_file)
        output_file.parent.mkdir(parents=True, exist_ok=True)

    # Pack into .nkpkg format
    with open(output_file, 'wb') as out:
        # 1. Header
        out.write(MAGIC)
        out.write(struct.pack('<H', VERSION))
        
        # 2. Manifest
        out.write(struct.pack('<I', len(manifest_bytes)))
        out.write(manifest_bytes)

        # 3. File Table & Data
        out.write(struct.pack('<H', len(all_files)))
        for rel_path, full_path in all_files:
            rel_bytes = rel_path.encode('utf-8')
            with open(full_path, 'rb') as f_in:
                file_data = f_in.read()

            out.write(struct.pack('<H', len(rel_bytes)))
            out.write(rel_bytes)
            out.write(struct.pack('<I', len(file_data)))
            out.write(file_data)

    pkg_size = os.path.getsize(output_file)
    print(f"\n[+] Successfully created package: {output_file} ({pkg_size} bytes)")
    return True

def interactive_mode():
    print("========================================")
    print("       Nykon OS App Compiler (.nkpkg)   ")
    print("========================================")

    # Search for candidate app folders
    search_dirs = [Path("src/apps"), Path("apps"), Path(".")]
    candidates = []

    for s_dir in search_dirs:
        if s_dir.exists() and s_dir.is_dir():
            for item in s_dir.iterdir():
                if item.is_dir() and not item.name.startswith('.') and not item.name in ('sys', 'fonts', 'wallpapers', 'build', 'build_tools', 'packages'):
                    if list(item.glob("*.c")) or (item / "icon.png").exists():
                        if item.resolve() not in [c.resolve() for c in candidates]:
                            candidates.append(item)

    if not candidates:
        print("[!] No app folders found automatically in src/apps/")
        user_input = input("Enter path to app folder: ").strip()
        if user_input:
            package_app(user_input)
        return

    print("\nSelect an app to package:")
    for idx, c in enumerate(candidates, 1):
        print(f"  [{idx}] {c.name} ({c})")

    print("  [0] Enter a custom path")

    try:
        choice = input("\nChoice (1-%d): " % len(candidates)).strip()
        if choice == "0":
            custom_path = input("Enter path to app folder: ").strip()
            if custom_path:
                package_app(custom_path)
        else:
            num = int(choice)
            if 1 <= num <= len(candidates):
                package_app(candidates[num - 1])
            else:
                print("[Error] Invalid selection.")
    except (ValueError, KeyboardInterrupt):
        print("\nExiting.")

def main():
    parser = argparse.ArgumentParser(description="Nykon OS App Package (.nkpkg) Compiler")
    parser.add_argument("app_dir", nargs="?", help="Path to app directory to package")
    parser.add_argument("-o", "--output", help="Output .nkpkg file path")

    args = parser.parse_args()

    if args.app_dir:
        package_app(args.app_dir, args.output)
    else:
        interactive_mode()

if __name__ == "__main__":
    main()

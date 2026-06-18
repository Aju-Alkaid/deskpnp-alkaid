"""
pnp_edit.py - PnP firmware project code editing helper

Usage (from Codex):
  python pnp_edit.py

Conventions:
  - All file I/O uses UTF-8 without BOM
  - Line endings: CRLF on write (Keil MDK standard)
  - Reads normalize to LF in memory, writes convert to CRLF
  - No implicit encoding guessing

Why this exists:
  PowerShell here-strings, .NET file I/O, and Python subprocess
  each handle BOM/line-endings differently. Mixing them causes
  encoding corruption. A single Python process with binary I/O
  eliminates these issues.
"""

import os
import sys

PROJECT_ROOT = r"E:\Desktop\qiansai\pnp_1"


def read_file(relpath):
    """Read project file, UTF-8 no-BOM, normalize line endings to LF"""
    abspath = os.path.join(PROJECT_ROOT, relpath)
    with open(abspath, "rb") as f:
        raw = f.read()
    if raw.startswith(b"\xef\xbb\xbf"):
        raw = raw[3:]
    text = raw.decode("utf-8")
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return text


def write_file(relpath, text):
    """Write project file, UTF-8 no-BOM, CRLF line endings"""
    abspath = os.path.join(PROJECT_ROOT, relpath)
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = text.replace("\n", "\r\n")
    with open(abspath, "wb") as f:
        f.write(text.encode("utf-8"))


def replace_once(text, old, new, label=""):
    """Replace and verify exactly 1 occurrence"""
    count = text.count(old)
    if count == 0:
        print(f"  WARN [{label}]: pattern not found")
    elif count > 1:
        print(f"  WARN [{label}]: found {count} occurrences")
    else:
        print(f"  OK   [{label}]: replaced 1")
    return text.replace(old, new)


# -- Edit logic below --

def apply_changes():
    content = read_file(r"Task\app_host.c")

    # Example:
    # content = replace_once(content, "old", "new", "description")

    write_file(r"Task\app_host.c", content)
    print("Done.")


if __name__ == "__main__":
    apply_changes()
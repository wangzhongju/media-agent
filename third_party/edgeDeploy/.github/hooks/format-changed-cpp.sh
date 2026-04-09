#!/usr/bin/env bash
set -euo pipefail

payload="$(cat)"

python3 - <<'PY' <<<"$payload"
import json
import os
import subprocess
import sys


def flatten(value):
    if isinstance(value, dict):
        for nested in value.values():
            yield from flatten(nested)
    elif isinstance(value, list):
        for nested in value:
            yield from flatten(nested)
    elif isinstance(value, str):
        yield value


data = json.loads(sys.stdin.read() or "{}")
tool_name = data.get("tool_name", "")
tool_input = data.get("tool_input", {})

if tool_name not in {"editFiles", "createFile", "replaceStringInFile", "create_file", "apply_patch"}:
    print(json.dumps({"continue": True}))
    raise SystemExit(0)

files = []
for item in flatten(tool_input):
    normalized = item.replace("\\", "/")
    if normalized.endswith((".c", ".cc", ".cpp", ".cxx", ".h", ".hpp")):
        files.append(normalized)

files = sorted(set(files))
if not files:
    print(json.dumps({"continue": True}))
    raise SystemExit(0)

clang_format = subprocess.run(["bash", "-lc", "command -v clang-format"], capture_output=True, text=True)
if clang_format.returncode != 0:
    print(json.dumps({
        "continue": True,
        "systemMessage": "clang-format not found; skipped C/C++ auto-format hook."
    }))
    raise SystemExit(0)

subprocess.run([clang_format.stdout.strip(), "-i", *files], check=False)
print(json.dumps({
    "continue": True,
    "systemMessage": "Auto-formatted changed C/C++ files: {}".format(", ".join(files))
}))
PY
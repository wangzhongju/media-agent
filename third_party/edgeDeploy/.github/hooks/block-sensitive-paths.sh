#!/usr/bin/env bash
set -euo pipefail

payload="$(cat)"

python3 - <<'PY' <<<"$payload"
import json
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
blocked_markers = [
    "build/",
    "install/",
    "weights/",
    ".rknn",
    "generated/",
]

tool_name = data.get("tool_name", "")
tool_input = data.get("tool_input", {})

matches = []
for item in flatten(tool_input):
    normalized = item.replace("\\", "/")
    if any(marker in normalized for marker in blocked_markers):
        matches.append(normalized)

if matches and tool_name in {"editFiles", "createFile", "deleteFile", "replaceStringInFile", "runInTerminal", "run_in_terminal", "create_file", "apply_patch"}:
    response = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": "Protected paths require manual review: {}".format(", ".join(sorted(set(matches))))
        },
        "systemMessage": "Blocked access to generated, installed, or model artifact paths."
    }
    print(json.dumps(response))
else:
    print(json.dumps({"continue": True}))
PY
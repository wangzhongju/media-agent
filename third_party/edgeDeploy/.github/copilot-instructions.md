# edgeDeploy AI Instructions

- Prefer minimal, targeted changes over broad refactors.
- Read the existing C++ and CMake structure before proposing edits.
- Keep compatibility with the current Linux aarch64 and armhf deployment layout.
- Avoid editing generated, installed, or model weight artifacts unless explicitly requested.
- When changing inference logic, preserve the separation between preprocessing, inference, and postprocessing.
- When suggesting tests, prioritize edge cases, error paths, and resource lifecycle behavior.

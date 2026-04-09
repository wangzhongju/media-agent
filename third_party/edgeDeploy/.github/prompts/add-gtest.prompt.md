---
name: add-gtest
description: Generate or extend gtest cases for a C++ module in this repository
agent: agent
tools: ["codebase", "search", "usages", "edit", "problems"]
argument-hint: 输入模块路径、类名或函数名，例如 src/infer 或 HardhatDetector
---

Generate or extend GoogleTest coverage for the user-provided target.

Requirements:
- Inspect the existing code and current test layout first.
- Prefer the smallest viable change set.
- Cover at least these paths when relevant: success path, invalid input, empty input, resource or initialization failure.
- If the repository is missing a test target or gtest dependency, explain the minimum integration work before editing build files.

Output format:
1. Target under test
2. Proposed test cases
3. Files to add or edit
4. Risks or blockers

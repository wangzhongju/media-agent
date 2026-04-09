---
name: cpp-tests
description: C++ test generation and review rules for this repository
applyTo: "**/*{test,tests}*.{cpp,cc,cxx,h,hpp}"
---

- Use clear scenario-based test names.
- Each test should validate one main behavior.
- Add separate cases for invalid input, empty input, and normal success paths.
- Prefer assertions that expose actual and expected values clearly.
- Avoid changing production code only to make tests easier unless there is a concrete design issue.

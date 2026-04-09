---
name: rknn-api-review
description: Review RKNN or inference-facing C++ APIs for maintainability and error handling
agent: agent
tools: ["codebase", "search", "usages", "problems"]
argument-hint: 输入模块路径、类名或头文件，例如 include/infer 或 modelEngine
---

Review the specified RKNN or inference-related API surface.

Check for:
- unclear ownership or resource lifecycle
- weak error handling or missing diagnostics
- tight coupling between model loading, inference, and postprocessing
- interfaces that are hard to test or extend

Output exactly these sections:
1. Interface design issues
2. Error handling issues
3. Naming or responsibility issues
4. Smallest useful refactor plan
5. Tests that should exist but are missing
6. NPU Performance Impact: Evaluate if current API design leads to unnecessary data copying between CPU and NPU.

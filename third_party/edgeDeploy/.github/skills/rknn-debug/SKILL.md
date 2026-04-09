---
name: rknn-debug
description: Use when debugging RKNN model loading failures, device initialization errors, runtime dependency issues, empty inference results, or edge deployment problems on aarch64 or armhf targets
argument-hint: 输入错误日志、目标平台、模型路径或相关模块路径
---

# RKNN Debug Skill

Use this skill when the task involves RKNN deployment, model loading, runtime failures, abnormal outputs, or platform-specific dependency issues.

## Workflow

1. Classify the problem first:
   - build or link failure
   - startup or initialization failure
   - inference output abnormal
   - deployment or dependency mismatch
2. Collect the minimum context:
   - target architecture: aarch64 or armhf
   - relevant executable or demo name
   - model file path
   - exact error log or symptom
3. Check for common root causes:
   - wrong library architecture
   - missing runtime library on target
   - wrong model path or missing file
   - device runtime initialization failure
   - preprocessing or postprocessing mismatch
4. When environment inspection is needed, use [check-env.sh](./check-env.sh).
5. If the user asks for a fix, prefer the smallest change that preserves preprocessing, inference, and postprocessing separation.

## Output Format

1. Most likely root cause
2. What evidence supports it
3. Commands or checks to run next
4. Minimum fix
5. Remaining risks

## Reference

- [Environment checks](./check-env.sh)
- [Expected troubleshooting notes](./examples/expected-log.md)
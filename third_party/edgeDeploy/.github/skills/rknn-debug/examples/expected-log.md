# RKNN Troubleshooting Notes

Common patterns to look for:

- `load model fail`: wrong path, missing file, unsupported runtime, or incompatible model artifact
- `symbol not found`: shared library mismatch between target rootfs and packaged dependencies
- `device init fail`: runtime or permission issue on target board
- empty detections or all-zero output: preprocessing mismatch, tensor layout mismatch, or postprocessing threshold issue

Recommended first split:

1. Can the project build successfully?
2. Can the executable start successfully?
3. Does the model load successfully?
4. Does inference run but produce abnormal output?
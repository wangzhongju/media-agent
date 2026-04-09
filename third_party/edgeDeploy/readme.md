# edgeDeploy — RK3588 NPU 推理库

edgeDeploy 是面向 RK3588 平台的轻量级模型推理库，封装了多种检测模型（例如 yolov8-det、yolov8-obb 等）。
核心能力：基于 RKNN 执行推理，使用 RGA 做图像预处理与 zero-copy 优化，最终产出可供调用的共享库（`.so`）。

--

## 目录结构（概览）

- `edgeDeploy/`
  - `include/infer/` — 对外调用接口（例如 `edgeInfer.h`）
  - `include/modelEngine/` — 模型引擎抽象与实现（继承自 `ModelBase`）
  - `src/` — 实现代码
- `3rdparty/` — 第三方依赖（`nlohmann/json` 等）
- `examples/` — 示例程序与配置

--

## 主要特性

- 支持多种检测模型（yolov8 系列及自定义引擎）
- 抽象化模型基类 `ModelBase`，方便扩展
- JSON 配置驱动，运行时可灵活指定参数
- 利用 RKNN + RGA 实现高效推理与图像预处理

--

## 快速开始（构建）

项目使用 CMake，默认会输出共享库（`.so`）。

项目使用 CMake，推荐使用仓库根目录下的 `buildComplier.sh` 来构建（该脚本封装了常用的本地构建与交叉编译选项，并会生成 `install/` 目录）。

本机构建（开发与验证）：

```bash
# 在仓库根目录运行脚本（默认：TARGET_SOC=rk3588, TARGET_ARCH=aarch64, BUILD_TYPE=Release）
./buildComplier.sh

# 指定构建类型（Debug/Release）
BUILD_TYPE=Debug ./buildComplier.sh

# 启用 AddressSanitizer（通常配合 Debug）
ENABLE_ASAN=ON BUILD_TYPE=Debug ./buildComplier.sh
```

交叉编译 / 指定交叉编译器：

脚本使用 `GCC_COMPILER` 环境变量来选择交叉编译器前缀或路径（脚本会导出 `CC`/`CXX`）。例如：

```bash
# 使用预设前缀（例如 aarch64-linux-gnu）
export GCC_COMPILER=aarch64-linux-gnu
./buildComplier.sh

# 或使用自定义工具链路径前缀
export GCC_COMPILER=~/opt/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf
./buildComplier.sh
```

脚本支持的主要环境变量：
- `GCC_COMPILER`: 交叉编译器前缀或路径（脚本会导出 `CC`/`CXX`）。
- `TARGET_SOC`: 目标 SoC（默认 `rk3588`）。
- `TARGET_ARCH`: 目标架构（默认 `aarch64`）。
- `BUILD_TYPE`: 构建类型（`Release` 或 `Debug`，默认 `Release`）。
- `ENABLE_ASAN`: 是否启用 AddressSanitizer（`ON`/`OFF`，默认 `OFF`）。
- `DISABLE_RGA`: 是否禁用 RGA（`ON`/`OFF`，默认 `OFF`）。

如果你更愿意直接使用 CMake，也可以按下列方式手动构建（脚本本质上调用了类似的 CMake 命令）：

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

注意：交叉编译时请确保工具链与目标 rootfs 中的运行时库（RKNN、RGA 等）版本匹配。

--

## 配置示例（JSON）

模型通过 JSON 文件初始化。示例配置：

```json
{
    "model_name": "hardhat_detect_yolov8s.rknn",
    "model_path": "/userdata/workspace/edgeDeploy/install/weights/hardhat_detect_yolov8s.rknn",
    "model_type": "yolov8_det",
    "num_class": 2,
    "class_names": ["hat", "person"],
    "input_w": 512,
    "input_h": 512,
    "conf_thresh": 0.25,
    "iou_thresh": 0.45,
    "topk": 100,
    "device_id": 0,
    "device": "gpu",
    "backend": "rknn"
}
```

常用字段说明：
- `model_name/model_path/model_type`：模型文件与类型
- `class_names/num_class`：类别定义
- `input_w/input_h`：输入分辨率
- `conf_thresh/iou_thresh/topk`：后处理参数（置信度、NMS 等）

--

## 自定的 pt模型转化为rknn

## 从 PyTorch (.pt) 转为 RKNN（概览）

下面提供一个推荐的、可复现的工作流：

1) 在训练/导出环境将 `.pt` 导出为 ONNX；
2) 在支持的环境中（或使用 RKNN 工具链）将 ONNX 转为 `.rknn`；
3) 在边缘设备上验证并应用外部后处理（脚本/CPU）。

环境准备
- 训练环境。
```bash
ssh cdky@192.168.88.91
docker exec -u root -it wvison-cdky bash
cd /workspace/rk3588
git clone https://github.com/airockchip/ultralytics_yolov8.git

```


- RKNN 转换需要 RKNN Toolkit，对应 wheel 在仓库或工具目录中，例如 `tools/rknn_toolkit2-2.3.0-...whl`。

步骤细化

- 导出 ONNX（在训练或导出环境中，示例）：

```bash
# 训练环境
# 参考文档https://github.com/airockchip/ultralytics_yolov8/blob/main/RKOPT_README.zh-CN.md
# 调整 ./ultralytics/cfg/default.yaml 中 model 文件路径，默认为 yolov8n.pt，若自己训练模型，请调接至对应的路径。支持检测、分割、姿态、旋转框检测模型。
# 如填入 yolov8n.pt 导出检测模型
# 如填入 yolov8-seg.pt 导出分割模型

export PYTHONPATH=./
python ./ultralytics/engine/exporter.py

# 执行完毕后，会生成 ONNX 模型. 假如原始模型为 yolov8n.pt，则生成 yolov8n.onnx 模型。
```

- ONNX → RKNN（使用 RKNN_Model_Zoo 的转换脚本或 rknn_toolkit 提供的工具）：

```bash
# 先把服务器转换好的 onnx模型scp到边缘端
# 安装/准备 rknn toolkit（视平台而定）
# 例如（在兼容的环境中）：
pip install tools/rknn_toolkit2-2.3.0-cp38-cp38-manylinux_2_17_aarch64.manylinux2014_aarch64.whl

# 使用 rknn_model_zoo 的转换脚本（示例路径）
cd rknn_model_zoo-2.3.0/examples/yolov8/python
python convert.py ../model/yolov8n.onnx rk3588

# 默认会在 ../model/ 下生成 yolov8.rknn（可通过参数指定输出路径和数据类型）
```

模型修改与注意事项
- 为了适配量化与 NPU，通常需要将模型中的后处理（NMS、阈值筛选等）从网络中移除，改由外部 CPU 执行；
- 删除或替换对 NPU 不友好的算子（如某些 DF L/自定义层），并在导出或转换阶段保证 ONNX 的 opset 与兼容性；
- 转换后请在目标设备上运行验证脚本，确认推理结果与期望一致（包括尺度、坐标系与类别映射）。

参考与示例
- .pt to .onnx 示例： https://github.com/airockchip/ultralytics_yolov8/blob/main/RKOPT_README.zh-CN.md
- RKNN Model Zoo 示例： https://github.com/airockchip/rknn_model_zoo/tree/main/examples/yolov8
- 仓库中的转换 wheel： `tools/rknn_toolkit2-2.3.0-...whl`

若需要，我可以为你：
- 在仓库中添加一个小脚本，自动化“.pt → onnx → rknn”的执行流程并记录常用参数；
- 添加一个在 RK3588 上验证 .rknn 的示例程序和说明。


## 使用示例（C++ / 伪代码）

```cpp
#include "infer/edgeInfer.h"

int main() {
  EdgeInfer engine;
  int ret = engine.init("/path/to/config.json");
  if (ret != 0) return -1;

  ImageBuffer img = loadImage("test.jpg");
  std::vector<Result> results;
  ret = engine.infer(img.data, results);
  if (ret == 0) printResults(results);
  return 0;
}
```

具体 API、类型定义与返回值请参考头文件：
- [edgeDeploy/include/infer/edgeInfer.h](edgeDeploy/include/infer/edgeInfer.h)

--

## 示例与测试

查看 `examples/` 目录，里面有若干演示程序和配置文件，包含如何加载模型、执行推理并可视化结果。

--

## 常见问题与建议

- 在目标设备上验证 RKNN 与 RGA 的兼容性，确保目标系统包含运行所需的动态库。
- 若使用 zero-copy 或 RGA 优化，确认内核/驱动支持并调整 buffer 管线。
- 推荐在 `examples/` 中添加至少一个端到端演示（图片读取 → init → infer → 可视化），便于快速验证。

--

## 贡献与许可证

请在仓库根目录添加 `LICENSE` 文件（若尚未添加）。欢迎提交 issue 与 PR，贡献指南可在 PR 中补充。

--

如果你希望我把这个 README 写入仓库、生成示例演示，或同时添加 `LICENSE`，告诉我下一步操作。



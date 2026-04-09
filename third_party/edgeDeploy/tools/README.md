# tools 目录说明

本 README 面向 `tools` 目录的使用者，包含依赖安装和常用检查命令的示例（包括使用阿里云 PyPI 镜像）。

## 1. 简介

`tools` 目录用于存放辅助脚本、工具和运行所需的 Python 依赖。

> 说明：你的环境中已安装 `rknn_toolkit2`（如果是系统或虚拟环境中已存在的包）。下面提供如何创建干净的 Python 虚拟环境并安装其它依赖的步骤。

## 2. 准备（推荐）—— 创建并激活虚拟环境

建议在一个隔离的虚拟环境中安装依赖，避免污染系统 Python：

```bash
# 在 tools 目录下创建 venv
python3 -m venv venv
# 激活（Linux/macOS）
source venv/bin/activate
# 更新 pip
pip3 install --upgrade pip
```

如果你不希望使用虚拟环境，可以在命令中添加 `--user` 选项以局部安装依赖。

## 3. 使用 pip3 安装依赖

项目可以使用 `requirements.txt` 来管理依赖。若仓库中没有 `requirements.txt`，你可以在 `tools` 目录新建一个，示例内容如下：

```
# requirements.txt 示例（根据项目需要调整）
numpy
opencv-python
scipy
requests
# ... 其它依赖
```

安装依赖的标准命令：

```bash
# 在虚拟环境已激活的前提下
pip3 install -r requirements.txt
```

## 4. 使用阿里云镜像（加速安装）

在国内网络环境下，推荐使用阿里云 PyPI 镜像加速安装。两种常见方式：

- 临时使用镜像（推荐）

```bash
pip3 install -r requirements.txt -i https://mirrors.aliyun.com/pypi/simple/
# 或者安装单个包
pip3 install <package-name> -i https://mirrors.aliyun.com/pypi/simple/
```

- 全局（临时会话）修改 pip 配置（只对当前用户有效）

```bash
# 在 ~/.pip/pip.conf 或 %APPDATA%/pip/pip.ini 中添加：
# [global]
# index-url = https://mirrors.aliyun.com/pypi/simple/

mkdir -p ~/.pip
cat > ~/.pip/pip.conf <<'EOF'
[global]
index-url = https://mirrors.aliyun.com/pypi/simple/
EOF
```

注意：如果采用全局配置，确保你了解该配置可能影响其他 Python 项目的安装源。

## 5. 关于 rknn_toolkit2

你提到系统中已经安装了 `rknn_toolkit2`。下面是一些常见的检查和验证命令：

```bash
# 查看 pip 是否能识别包（可能包名在 pip 中不同，请据实际情况替换）
pip3 show rknn_toolkit2 || pip3 show rknn-toolkit2

# 在 Python 中导入并打印版本
python3 -c "import rknn_toolkit2 as rk; print(getattr(rk, '__version__', rk))" || python3 -c "import rknn_toolkit2; print('imported rknn_toolkit2')"
```

如果 `import` 失败，请确认是在同一个 Python 解释器/虚拟环境下执行导入（即激活了包含 `rknn_toolkit2` 的环境），或者重新安装：

```bash
pip3 install rknn_toolkit2 -i https://mirrors.aliyun.com/pypi/simple/
```

（如果官方包名不同，请以实际可用的包名为准。）

## 6. 常见问题与排查

- 安装失败或超时：使用阿里云镜像 `-i https://mirrors.aliyun.com/pypi/simple/`。
- 权限问题：加上 `--user` 或使用虚拟环境。
- 模块导入错误：确认运行时使用的 Python 与安装包所在环境一致（检查 `which python3` / `which pip3`）。

示例：

```bash
which python3
which pip3
python3 -m pip list | grep rknn
```

## 7. 额外建议

- 若项目需要编译本地 C/C++ 扩展，请安装相应的系统依赖（例如 `build-essential`、`python3-dev`、`cmake` 等）。
- 若希望我帮你生成一个初始的 `requirements.txt`（根据仓库中的 Python 文件/导入推断），可以让我扫描项目并创建建议的依赖列表。

---

如果你希望 README 中包含更具体的依赖（例如项目已知的包名和版本），告诉我需要包括哪些文件或要扫描的路径，我可以自动生成一个更精确的 `requirements.txt` 并更新 README。
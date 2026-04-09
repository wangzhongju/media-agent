问题说明
-----------
项目自带的 `libsndfile.a` 在某些平台（如 aarch64）不是用位置无关代码（-fPIC）编译的。
当把该静态库链接到共享库（例如本项目将所有代码打包为 `libedgeDeploy.so`）时，会出现类似错误：

```
/usr/bin/ld: ../3rdparty/libsndfile/Linux/aarch64/libsndfile.a(...): relocation R_AARCH64_ADR_PREL_PG_HI21 against symbol `stderr@@GLIBC_2.17' ... recompile with -fPIC
```

原因是静态归档中包含了非 PIC 的对象文件，不能安全地被链接进共享对象。解决方法是使用源码重新构建 `libsndfile`，并确保编译时启用 `-fPIC`，或者使用系统提供的 `libsndfile.so`。

建议的解决办法（CMake 构建，推荐）
----------------------------------
以下步骤在目标机器上执行，会生成同时包含 `libsndfile.a`（带 -fPIC）和 `libsndfile.so`：

```bash
# 在源码目录（假设已把 libsndfile 源码放在此目录）
cd /userdata/public/libsndfile

# 1) 配置并为静态库构建（强制位置无关代码）
cmake -S . -B build-static \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/userdata/public/libsndfile/install \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_PROGRAMS=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DENABLE_EXTERNAL_LIBS=OFF \
  -DENABLE_MPEG=OFF \
  -DENABLE_CPACK=OFF

# 2) 配置并为共享库构建
cmake -S . -B build-shared \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/userdata/public/libsndfile/install \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_TESTING=OFF \
  -DBUILD_PROGRAMS=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DENABLE_EXTERNAL_LIBS=OFF \
  -DENABLE_MPEG=OFF \
  -DENABLE_CPACK=OFF

# 3) 编译并安装（或复制产物）
cmake --build build-static -j$(nproc)
cmake --build build-shared -j$(nproc)

# 把产物复制到安装目录（已在上面指定）
mkdir -p install/lib
cp build-static/libsndfile.a install/lib/libsndfile.a
cp build-shared/libsndfile.so* install/lib/ || true

# 验证（可选）
file install/lib/libsndfile.a
file install/lib/libsndfile.so
readelf -d install/lib/libsndfile.so | grep SONAME || true
```

如果你偏好 autotools（configure）流程，可用下面方式（部分项目缺少 autotools 生成文件时可能需要先运行 `autoreconf -fi`）：

```bash
./configure CFLAGS="-fPIC" CXXFLAGS="-fPIC" --prefix=/tmp/libsndfile-install
make -j$(nproc)
make install
# 把生成的库复制回项目 3rdparty 路径
cp /tmp/libsndfile-install/lib/libsndfile.a /userdata/workspace/edgeDeploy/3rdparty/libsndfile/Linux/aarch64/libsndfile.a
cp /tmp/libsndfile-install/lib/libsndfile.so /userdata/workspace/edgeDeploy/3rdparty/libsndfile/Linux/aarch64/ 2>/dev/null || true
```

将产物替换回项目
------------------
构建完成后，可把新库复制（或链接）回项目的 `3rdparty` 目录：

```bash
# 例：把生成的 libsndfile 复制到项目预期位置
mkdir -p /userdata/workspace/edgeDeploy/3rdparty/libsndfile/Linux/aarch64
cp /userdata/public/libsndfile/install/lib/libsndfile.a /userdata/workspace/edgeDeploy/3rdparty/libsndfile/Linux/aarch64/libsndfile.a
cp /userdata/public/libsndfile/install/lib/libsndfile.so* /userdata/workspace/edgeDeploy/3rdparty/libsndfile/Linux/aarch64/ || true
```

验证替换
--------
在 `edgeDeploy` 项目根重新配置并编译，确认不再出现 `recompile with -fPIC` 错误：

```bash
cd /userdata/workspace/edgeDeploy
rm -rf build && mkdir build && cd build
cmake .. && make -j$(nproc)
```

注意事项
--------
- 请确保 `/userdata`（目标复制目录）在系统重启后仍然挂载可用；若是临时挂载（如网络盘），请将库复制到一个持久位置。
- 若使用交叉编译（不同主机/目标），请在目标架构或交叉工具链下构建，并保证 CMake/Autotools 接收正确的 `--host`/工具链文件参数。
- 若不想在仓库内替换文件，也可以把新构建的 `libsndfile.so` 放到系统库路径或在构建 `edgeDeploy` 时指定 `-DLIBSNDFILE=...` 指向该 .so 文件。

如需我替你把编译好的产物复制回 `/userdata/workspace/edgeDeploy/3rdparty/libsndfile/Linux/aarch64/` 并触发一次全量构建，回复“替换并构建”，我会继续执行。

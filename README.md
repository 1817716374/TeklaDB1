# TeklaFormats / TeklaDB1

离线读取 Tekla Structures / Xsteel 文件的 C++17 互操作库。方向是 **单文件解析 + 完整模型目录关联**。现有仓库名称和 `tekla::db1` API 保留兼容；新的工程入口为 `tekla::readProject`，CMake 同时支持 `TeklaFormats::core` 与 `TeklaDB1::core`。

不需要启动 Tekla，也不依赖其 SDK。核心依赖 zlib；Open CASCADE 几何后端可选。

## 当前支持范围

| 内容 | 接口与状态 |
|---|---|
| 主模型 DB1 | 构件、坐标、GUID、属性、装配、组件、螺栓、焊缝、切割/布尔关系；按已验证布局读取 |
| xslib.db1 | 组件对象图、参数定义、公式原文与组件定义 |
| profdb / pgdb | 型材目录、固定截面、Sketch Solver 轮廓 |
| matdb / screwdb / assdb | 材料、螺栓与螺栓组件目录 |
| profitab / CLB | 规则与语句读取；不等于完整执行所有参数化生成器 |
| Shapes XML / TEZ | Shape 定义和 Polymesh 点、面、内环、边 |
| DB2 | 原始容器读取、按文件名关联 DB1；**尚无稳定的编号字段语义 API** |
| environment.db / options_*.db | DBV 与旧式容器原始读取；选项键值尚未稳定命名 |
| DG 图纸 | 工程目录发现与状态报告；**尚未实现图纸语义解析** |
| history.db | 标准 SQLite，交由 SQLite 工具读取 |

文件扩展名 `.db` 并不代表统一格式，目录文件、DBV 和 SQLite 使用不同入口。`readProject` 会记录每个文件是语义已读取、仅原始读取、未读取还是失败，不能把发现文件当成解析完成。

| DB1 存储版本 | 语义支持证据 |
|---|---|
| 7.30 | 保留旧实现；历史私有语料验证，本轮没有重新取得样本 |
| 7.82 | 新公开语料可原始读取；语义入口明确拒绝，避免套错布局 |
| 8.44 | 保留历史支持；字段描述符仅做结构检查，完整签名仍待独立语料验证 |
| 8.95 | 本轮公开语料回归：26 份主库、26 份组件库 |
| 9.52 | 本轮公开语料回归：1 份主库、1 份组件库；主库为空模型 |
| 9.60 | 本轮公开语料回归：1 份非空主库、1 份组件库 |
| 9.65 / 9.66 | 保留历史布局；使用角色签名检查，本轮未重新取得样本 |
| 其他版本 | 明确拒绝语义解码；可尝试 `parseRawDatabase` 保留原始内容 |

不是所有对象类型都已恢复：钢筋专用语义、复杂曲面构件、部分现代路径轮廓仍需进一步验证。成功返回表示该 API 完成其支持范围内的读取，**不代表所有 Tekla 语义或精确几何均已覆盖**。诊断与未构建对象不能忽略。

## 构建和测试

需要 CMake 3.20+、C++17 编译器和 zlib 开发包。不要只克隆源码而省略系统依赖。

Linux（Debian/Ubuntu）：

```sh
sudo apt-get install cmake ninja-build g++ zlib1g-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/installed"
```

Windows（MSVC，已通过 vcpkg 安装 zlib）：

```powershell
vcpkg install zlib:x64-windows
cmake -S . -B E:/CodexData/TeklaFormats/build `
  -DCMAKE_TOOLCHAIN_FILE=E:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build E:/CodexData/TeklaFormats/build --config Release
ctest --test-dir E:/CodexData/TeklaFormats/build -C Release --output-on-failure
cmake --install E:/CodexData/TeklaFormats/build --config Release --prefix E:/CodexData/TeklaFormats/installed
```

将示例中的 vcpkg 路径替换为实际安装路径。如果已有独立 zlib，可改用 `-DZLIB_ROOT=...`。MinGW 的检查工具已配置 Unicode 入口链接选项；C++20 的 `char8_t` 路径转换也兼容。`TEKLADB1_BUILD_TESTS=OFF` 可关闭测试与检查工具。

默认 CTest 使用现场生成的小型数据，不下载第三方文件，不需要 Tekla。测试数据写入 CMake 构建目录下的 `test-data`。

## 独立文件与完整工程

单文件入口明确指定主库，不要求模型文件名等于目录名：

```cpp
#include <tekla/db1/Parser.hpp>
tekla::db1::Model model;
std::string error;
if (!tekla::db1::parseModelFile("models/example.db1", model, error)) {
    // 显示 error。
}
```

`parseModelDirectory` 仍然可用。如果目录有多份主 DB1，返回歧义错误，不会擅自选择体积最大的文件。

工程入口：

```cpp
#include <tekla/Project.hpp>
tekla::Project project;
tekla::ProjectOptions options;
options.resourceDirectories = {"/path/to/project-resources", "/path/to/environment"};
std::string error;
if (!tekla::readProject("/path/to/model", project, error, options)) {
    // 主模型读取失败；error 提供原因。
}
// project.model、componentLibrary、materials、bolts、boltAssemblies、shapes
// project.files：逐文件读取层级与错误
// project.rawCompanions：DB2、DBV 的原始数据
// project.associations：主库、配套编号库、组件库与资源的关联依据
// project.diagnostics：部分失败、未支持或缺失信息
```

资源优先级为模型目录、随后按顺序指定的外部目录。同名型材优先保留较高优先级来源。XML 中的项目/公司/系统搜索路径仅作为元数据保留，不会静默映射到当前计算机的路径。模型中引用 Shape 的几何实例映射仍未全部完成，读取 Shape 目录不等于所有 Shape 构件已经构形。

配套文件失败默认保留主模型并报告 `ReadLevel::Failed`；设置 `strictCompanions=true` 会使已尝试读取的配套文件失败导致整个工程读取失败。该选项不把尚未实现的 DG 解码转换为已支持。

## 原始容器与内存预算

```cpp
tekla::db1::RawDatabase raw;
tekla::db1::RawDatabaseOptions options;
options.retainDecompressedFileImage = true;
options.maxDecodedBytes = 2ULL * 1024 * 1024 * 1024;
tekla::db1::parseRawDatabase("model.db2", raw, error, options);
```

默认单文件解码上限 1 GiB。`ModelReadOptions.maxDecodedBytes` 可调整主模型和组件库预算；工程入口分别配置 `modelOptions` 和 `rawOptions`。目录文件目前使用默认 1 GiB 上限。支持 GZIP 多成员并检查 CRC、截断及尾部垃圾。

原始接口默认不保留完整解压文件，以节省内存；需要逐字节研究/保存旧格式分配器空隙时必须启用 `retainDecompressedFileImage`。工程入口的配套 raw 数据默认启用该选项。DBV 的名称放在 `containerName` 中；其名称长度不再误作版本号。

## 可重复的公开语料回归

`tests/corpus.json` 记录 282 个外部文件的固定提交 URL、大小、SHA-256 和 298 个用例。第三方模型不随仓库分发。需要 Python 3.11+：

```powershell
python -B tools/corpus.py --download `
  --data E:/CodexData/TeklaFormats/corpus `
  --exe E:/CodexData/TeklaFormats/build/Release/tekladb1_validate.exe `
  --report E:/CodexData/TeklaFormats/corpus-results.json
```

Ninja/MinGW 构建的 exe 通常直接位于构建目录，不含 `Release` 子目录；Linux 使用无 `.exe` 的路径。不带 `--download` 时只验证本地文件。下载约 60 MB，完整文件清单以 manifest 为准。

包含 30 份主库、30 份组件库及配套 DB2/DBV/目录。298 个用例中 4 个是 7.82 语义拒绝测试，不计入语义支持成功数。统计和语义指纹用于防止回归；它们不等于 Tekla/IFC 独立几何真值。数据来源包括 AUTRA、BIM-Modeling、公开钢结构培训模型和研究示例，具体来源均在 manifest 中。

GitHub Actions 配置 Linux C++17/20、ASan/UBSan、Windows MSVC、安装后独立消费和 Linux OCCT 构建。运行状态以对应提交的 Actions 结果为准。

## CMake 集成

```cmake
find_package(TeklaFormats 0.3 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE TeklaFormats::core)
```

旧的 `find_package(TeklaDB1)` 和 `TeklaDB1::core` 保持可用。两个包可在同一消费项目中同时加载。可选几何目标也同时提供 `TeklaFormats::occt` / `TeklaDB1::occt`。

## 几何边界

使用 `-DTEKLADB1_WITH_OCCT=ON` 和可选 `-DTEKLADB1_OCCT_ROOT=...` 启用 OCCT。

支持已有截面构形、部分轮廓、切割、布尔、孔和三角化，但螺栓、焊缝、曲线路径及部分型材存在简化。未知现代轮廓不再默认为板。调用者应检查 `unbuiltPartIds` 与诊断；不可将构形成功数量等同于精确度，更不可直接作为制造验收依据。可选测试用解析解检查简单梁/板体积和网格索引。

长期多格式路线和历史记录见 [逆向路线图](docs/REVERSE_ENGINEERING_PLAN.zh-CN.md)。本项目与 Trimble Inc. 没有隶属或背书关系；不包含 Tekla SDK、软件安装包或第三方专有模型。

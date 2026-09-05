# TeklaDB1

TeklaDB1 是一个用 C++17 编写的 Tekla Structures/Xsteel 文件互操作库。它可以在不启动 Tekla Structures 的情况下读取模型数据库、组件库和配套目录，并可选用 Open CASCADE 重建拓扑实体与三角网格。

## 主要能力

- 读取主模型和组件库，恢复对象标识、GUID、所有权、坐标、构件、属性、装配、组件及其引用关系。
- 读取型材、材料、螺栓、螺栓组件和参数化截面目录。
- 读取模型内的 Sketch Solver 型材几何，并与型材目录合并。
- 读取 Shapes 定义及其 Polymesh 几何，包括点、面、内环和边可见性。
- 读取模型名称、产品版本、环境、语言和搜索路径等元数据。
- 提供原始数据库接口，保留表描述、记录负载、分配器元数据及尚未命名的私有数据段。
- 可选 Open CASCADE 后端支持构件构形、切割、布尔运算、螺栓孔、螺栓、焊缝和三角化。

## 已验证的存储版本

| Xsteel 存储版本 | 主模型 | 组件库 |
|---|---:|---:|
| 7.30 | 支持 | 支持 |
| 8.44 | 支持 | 支持 |
| 8.95 | 支持 | 支持 |
| 9.52 | 支持 | 支持 |
| 9.65 | 支持 | 支持 |
| 9.66 | 支持 | 支持 |

解析器根据存储版本和表结构签名选择布局，不依赖单一固定表数量。遇到尚未验证的新布局时，语义接口会返回明确错误；原始数据库接口仍会尽可能保留完整内容。

## 支持的配套文件

| 文件 | 内容 |
|---|---|
| xslib.db1 | 自定义组件定义、参数、公式和对象图 |
| profdb.bin | 参数化型材、固定截面、多环轮廓和倒角 |
| pgdb.bin | Sketch Solver 型材、轮廓点和圆角定义 |
| matdb.bin | 材料及其属性定义 |
| screwdb.db | 螺栓目录 |
| assdb.db | 螺栓组件目录 |
| profitab.inp、CLB | 参数化型材规则与嵌套定义 |
| Shapes XML、TEZ | Shape 元数据与 Polymesh 几何 |
| TeklaStructuresModel.xml | 模型及环境元数据 |
| environment.db、options_*.db | DBV 容器的原始无损读取 |

## 构建

核心库只依赖 zlib：

```powershell
cmake -S . -B build -DTEKLADB1_BUILD_TESTS=OFF
cmake --build build --config Release
cmake --install build --config Release --prefix install
```

启用 Open CASCADE 后端：

```powershell
cmake -S . -B build-occt `
  -DTEKLADB1_BUILD_TESTS=OFF `
  -DTEKLADB1_WITH_OCCT=ON `
  -DTEKLADB1_OCCT_ROOT=D:/path/to/OCCT
cmake --build build-occt --config Release
cmake --install build-occt --config Release --prefix install
```

Linux 或其他 Shell 请将 PowerShell 的续行符替换为反斜杠。

## 在 CMake 项目中使用

```cmake
find_package(TeklaDB1 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE TeklaDB1::core)
```

使用 Open CASCADE 后端时再链接：

```cmake
target_link_libraries(my_target PRIVATE TeklaDB1::occt)
```

## 基本示例

```cpp
#include <tekla/db1/Parser.hpp>

#include <filesystem>
#include <string>

tekla::db1::Model model;
std::string error;
if (!tekla::db1::parseModelDirectory(
        std::filesystem::path("path/to/model"), model, error))
{
    // Handle the diagnostic in error.
}
```

## 验证情况

当前回归语料覆盖 39 份主模型或组件数据库、11 份 Sketch Solver 型材数据库、38 份目录文件、66 份 CLB 文件和 1108 份 TEZ 几何文件。核心库已在 Windows x64、Linux x86_64 和 Linux ARM64 上完成编译验证。

## 已知边界

- 私有文件格式没有公开规范；新的产品版本或对象类型仍需使用真实样本验证。
- 模型缺少其引用的专有自定义型材定义时，参数和对象关系仍会保留，但几何后端不会用猜测的包围盒冒充精确截面。
- history.db 是标准 SQLite 数据库，可由任意 SQLite 实现读取，因此核心库不额外引入 SQLite。
- plotdev.bin 保存绘图设备配置，不属于三维模型语义范围。

本项目与 Trimble Inc. 没有隶属或背书关系。仓库不包含 Tekla 专有二进制样本、SDK 或环境资源。

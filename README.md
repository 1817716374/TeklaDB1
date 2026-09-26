# TeklaFormats / TeklaDB1

离线读取 Tekla Structures / Xsteel 文件的 C++17 互操作库。方向是 **单文件解析 + 完整模型目录关联**。现有仓库名称和 `tekla::db1` API 保留兼容；新的工程入口为 `tekla::readProject`，CMake 同时支持 `TeklaFormats::core` 与 `TeklaDB1::core`。

不需要启动 Tekla，也不依赖其 SDK。核心依赖 zlib；Open CASCADE 几何后端可选。 缺少目录时的UPE/IPE使用明确标注的公称尺寸尖角回退；已修复折线段转角斜接，并用25个真实IFC实体核验，见[截面与斜接证据](docs/NOMINAL_SECTIONS.zh-CN.md)。

## 当前支持范围

| 内容 | 接口与状态 |
|---|---|
| 主模型 DB1 | 7.82表面处理及父构件/变量关联，见 [表面处理证据](docs/SURFACE_TREATMENT.zh-CN.md)；构件、坐标、GUID、属性、装配、组件、螺栓、焊缝、切割/布尔关系；现代独立位置记录与7.82定义内嵌位置块（轴向偏移及深度/平面字段），见 [位置证据](docs/PART_POSITION.zh-CN.md) |
| xslib.db1 | 组件、参数与定义；7.82/现代布局的距离变量与公式引用图，通用变量归属索引；公式求解尚未实现 |
| DB1 钢筋 | 现代kind47实体、定义、父构件、名称/类别/材质/尺寸、半径和已存数组，见[钢筋证据](docs/REINFORCEMENT.zh-CN.md)；另恢复编号前缀/起始号及类型限定的对象引用，见[钢筋编号](docs/REINFORCEMENT_NUMBERING.zh-CN.md)；分配编号、完整模式、保护层/钩和最终中心线未恢复 |
| profdb / pgdb | 型材目录、固定截面、Sketch Solver 轮廓 |
| matdb / screwdb / assdb | 材料、螺栓与螺栓组件目录 |
| profitab / CLB | 规则与语句读取；不等于完整执行所有参数化生成器 |
| Shapes XML / TEZ | Shape 定义和 Polymesh 点、面、内环、边 |
| DB2 | `parseNumberingDatabase`：完整顺序表、编号系列与零件/装配序列计数；7.30内联编号、7.82及现代DB1对象编号记录可关联系列，见[对象编号证据](docs/OBJECT_NUMBERING.zh-CN.md)；**比较快照、8.44对象分配与有效状态尚未恢复** |
| environment.db | `parseEnvironmentDatabase`：属性名称/标签/存储类型、对象类别关联、整数选项列表；**元数据标志和部分定义值尚未解释** |
| options_*.db | `parseOptionsDatabase`：布尔/整数/浮点/字符串键与成对值槽位；**当前值/默认值的优先级尚未验证** |
| DG 图纸 | `parseDrawing`：7.30/7.82/9.54 容器、文本/属性、图幅、主体、视图坐标基/范围；7.82/9.54另有模型引用，7.30其余引用未恢复，见[7.30证据](docs/DG_730.zh-CN.md)；旧版使用无工程范围的数字 ID，新版使用 GUID；**纸面定位、比例/缩短、尺寸与完整绘图图元尚未恢复** |
| history.db | 标准 SQLite，交由 SQLite 工具读取 |

文件扩展名 `.db` 并不代表统一格式，目录文件、DBV 和 SQLite 使用不同入口。`readProject` 会区分 `Semantic`、`PartialSemantic`、`Raw`、`Discovered` 和 `Failed`；DG、DB2 与 DBV 当前为部分语义，不能把发现文件或读取部分字段当成解析完成。

| DB1 存储版本 | 语义支持证据 |
|---|---|
| 7.30 | 已固定公开教学主库和组件库，沿用旧构件/几何路径；恢复对象内联编号，1,467个对象与历史日志核对；另固定34组历史DB1/DB2备份，见[7.30编号证据](docs/INLINE_NUMBERING_730.zh-CN.md) |
| 7.82 | 两份主库、两份组件库：构件/板轮廓、独立螺栓、焊缝、布尔/装配/属性与参数；部分语义，见 [字段证据与边界](docs/DB1_782.zh-CN.md) |
| 8.44 | 保留历史支持；字段描述符仅做结构检查，完整签名仍待独立语料验证 |
| 8.95 | 本轮公开语料回归：27 份主库、27 份组件库；身份类型引用、owner 与构件辅助引用已分开读取，见 [归属证据](docs/OWNERSHIP.zh-CN.md) |
| 9.52 | 本轮公开语料回归：1 份主库、1 份组件库；主库为空模型 |
| 9.60 | 本轮公开语料回归：1 份非空主库、1 份组件库 |
| 9.65 / 9.66 | 保留历史布局；使用角色签名检查，本轮未重新取得样本 |
| 其他版本 | 明确拒绝语义解码；可尝试 `parseRawDatabase` 保留原始内容 |

不是所有对象类型都已恢复：钢筋完整几何与模式、复杂曲面构件、部分现代路径轮廓仍需进一步验证。成功返回表示该 API 完成其支持范围内的读取，**不代表所有 Tekla 语义或精确几何均已覆盖**。诊断与未构建对象不能忽略。

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
// project.environment / optionsDatabases：DBV 属性定义与带类型的存储值
// project.attributeDefinitionAssociations：DB1 属性与定义的精确名称/存储类型匹配
// project.drawings[*].viewsByContext：视图坐标基、范围与属性集名
// project.drawingSubjectAssociations：图纸主体到 DB1 零件/装配的 GUID 关联
// 匹配不推断对象类别适用性，也不填补对象缺失值或计算有效默认值。
// project.numbering / drawings：DB2 与 DG 的部分语义
// project.objectNumberingSeriesAssociations：带DB1/DB2路径范围的对象编号→系列关联
// project.drawingModelAssociations：用已验证 GUID 连接图纸记录和模型对象
// project.associations：主库、配套编号库、组件库与资源的关联依据
// project.diagnostics：部分失败、未支持或缺失信息
```

资源优先级为模型目录、随后按顺序指定的外部目录。同名型材优先保留较高优先级来源。XML 中的项目/公司/系统搜索路径仅作为元数据保留，不会静默映射到当前计算机的路径。模型中引用 Shape 的几何实例映射仍未全部完成，读取 Shape 目录不等于所有 Shape 构件已经构形。

配套文件失败默认保留主模型并报告 `ReadLevel::Failed`；设置 `strictCompanions=true` 会使已尝试读取的配套文件失败导致整个工程读取失败。`readNumbering`、`readDrawings` 可分别关闭新增读取入口。未知 DG 版本在语义入口明确失败；需要研究时可使用 raw API。

## DB2 与 DG 的独立入口

```cpp
#include <tekla/Numbering.hpp>
#include <tekla/Drawing.hpp>
tekla::NumberingDatabase numbering;
tekla::Drawing drawing;
std::string error;
tekla::parseNumberingDatabase("models/model.db2", numbering, error);
tekla::parseDrawing("models/drawings/example.dg", drawing, error);
```

DB2 语义入口当前覆盖 7.30、7.82、8.95、9.52、9.60（包括空组件编号库）。`partCounter` / `assemblyCounter` 是保存的序列计数，不是当前构件数量，也不能单独还原某个对象的编号；其余字段和快照保留在 `raw` 中。现代DB1提供objectNumberingRecords和objectNumberingReferences，工程入口可关联DB2系列；零序号和高位特殊值不猜成确定编号。7.82 DB1另有独立的部分语义映射，其对象编号已按独立布局恢复；无GUID的旧DB1/DB2默认不配对，调用者可显式设置trustLegacyNumberingBasenames，并通过关联的pairingEvidence区分依据。7.30直接把对象编号保存在94/95表，公开inlineObjectId和storedNumber，不伪造引用；仅已验证的起始号1范围提供派生编号，见[内联编号证据](docs/INLINE_NUMBERING_730.zh-CN.md)。

DG 9.54 会校验完整表目录签名，恢复根字符串（包括跨记录的标记 XML）、属性、属性链接、图幅宽高及一类模型 GUID 引用。标记 XML 当前作为文本返回。`grProjectGuid` 相符后，工程入口才用引用 GUID 连接 DB1 identity；无法匹配或有歧义的引用给出诊断。不会通过图纸文件名猜测构件，也不把这些字段称为完整图纸解析。研究记录与独立编号日志验证见 [DB2 / DG 格式证据](docs/RELATED_FORMATS.zh-CN.md)。

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

`tests/corpus.json` 记录 713 个外部文件路径的固定来源 URL、大小、SHA-256 和 1,483 个用例。第三方模型不随仓库分发。需要 Python 3.11+：

```powershell
python -B tools/corpus.py --download `
  --data E:/CodexData/TeklaFormats/corpus `
  --exe E:/CodexData/TeklaFormats/build/Release/tekladb1_validate.exe `
  --report E:/CodexData/TeklaFormats/corpus-results.json
```

Ninja/MinGW 构建的 exe 通常直接位于构建目录，不含 `Release` 子目录；Linux 使用无 `.exe` 的路径。不带 `--download` 时只验证本地文件。下载约 60 MB，完整文件清单以 manifest 为准。

包含 32 份主库（图纸测试工程另复用一份相同主库副本）、32 份组件库、64 份 DB2、335 份 DG、93 份 DBV 及配套目录，另有同一7.30工程的34组历史DB1/DB2备份（不作为34个独立工程）。1,483 个用例包含 7.82 的两份主库、两份组件库与两个工程聚合检查；7.82 仍为部分语义，独立螺栓和未命名实体的限制见支持矩阵。一项以 Tekla 自身编号历史日志独立核对 10 个 DB2 计数，同时核对 28 个 DG→DB1 GUID 引用、118 条 DB1→环境定义名称/类型匹配，并检查官方 OBJECT_LOCKED 示例中的标签次序。另核对 19 个视图、4 个图纸主体、同包 VI 设置中的 6 个范围/深度字段，以及钢板模型轴与视图 X 轴。旧 DG 另有 293 项逐字节重建与 293 项语义回归，59 个主体 ID/类型匹配、1,361 条数字引用匹配；35 条未解析引用、断链文本、退化/歧义视图均明确保留。旧 xslib 另核对 40 条构件定义、660 条归属参数和 2,726 条子对象身份；配套目录及四份对话框验证 28 条具名定义与 69 个参数引用；新增 6,838 条距离变量、10,450 条公式绑定及对象引用图，公式尚不求值，详见 [7.82 证据](docs/DB1_782.zh-CN.md)。另有 60 项主库/组件库归属回归和三份现代对话框的 15 个参数匹配；通用变量作用域保留 kind-60 对象，详见 [归属与现代变量](docs/OWNERSHIP.zh-CN.md)。新增60项位置记录回归（含4份7.82主库/组件库）和现代同工程源码对照（30根梁、36个柱/基础），位置设置不重复应用到已存几何。另有4项7.82表面处理回归和1项实体/材质证据，恢复7个表面、32个轮廓点及7条工程材质关联；颜色、完整裁剪/铺贴等仍未解释。另有56项现代对象编号回归及一项独立日志核对：187个零件、43个装配与Tekla日志一致，249条对象关联到DB2系列，20条特殊起始值保持未配对。第三种44字节记录已确认为钢筋编号系列，恢复前缀/起始号并用两份组件参数核对12项；分配槽及生命周期仍未解释，见[钢筋编号](docs/REINFORCEMENT_NUMBERING.zh-CN.md)。7.82另有4项编号回归及1项图纸证据，685条编号记录和7,410条非零引用完整保留；4个主体编号与配套图纸标记一致，显式确认文件配对后有5,192条DB2系列关联。另有56项现代钢筋回归和2项参数证据检查，1,447个实例、301份定义及父构件关联；两种版本组件库各96项已存参数一致性检查，重复模板不视为独立几何真值。新增Construsoft公开练习模型的24项回归；螺栓零位置引用保留并诊断，非零断链仍拒绝，见[空位置证据](docs/BOLT_POSITIONS.zh-CN.md)。新增7.30公开工程的10项回归及21项编号历史计数核验；空组件DB2可只有文件头，新增73项7.30内联编号/备份回归，1,467个对象编号与日志一致，2个当前零值与历史差异原样保留；显式配对后2,236条系列关联，详见[内联编号证据](docs/INLINE_NUMBERING_730.zh-CN.md)。新增35份7.30 DG的72项回归：91个视图、35个图幅、13条目录文件名交叉核对、1个独立模型主体及2个独立视图轴；图纸已存标记不当作当前模型编号。新增同工程IFC独立证据：1,215个对象身份/属性/端点和536个简单矩形构件的4,288个顶点核验；25个未导出构件及复杂几何仍保留边界，详见[IFC证据](docs/IFC_730_EVIDENCE.zh-CN.md)。默认 CTest 另有 298 项正常/异常输入测试。统计和语义指纹用于防止回归；它们不等于 Tekla/IFC 独立几何真值。数据来源具体记录在 manifest 中。

GitHub Actions 配置 Linux C++17/20、ASan/UBSan、Windows MSVC、安装后独立消费和 Linux OCCT 构建。运行状态以对应提交的 Actions 结果为准。

## CMake 集成

```cmake
find_package(TeklaFormats 0.3 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE TeklaFormats::core)
```

旧的 `find_package(TeklaDB1)` 和 `TeklaDB1::core` 保持可用。两个包可在同一消费项目中同时加载。可选几何目标也同时提供 `TeklaFormats::occt` / `TeklaDB1::occt`。

## 几何边界

使用 `-DTEKLADB1_WITH_OCCT=ON` 和可选 `-DTEKLADB1_OCCT_ROOT=...` 启用 OCCT。

Ubuntu 24.04 的 OCCT 开发包拆分了部分头文件；以下依赖组合已用于 CI：

```sh
sudo apt-get install libocct-modeling-algorithms-dev libocct-modeling-data-dev libocct-visualization-dev libtbb-dev
cmake -S . -B build-occt -G Ninja -DCMAKE_BUILD_TYPE=Release -DTEKLADB1_WITH_OCCT=ON
cmake --build build-occt
ctest --test-dir build-occt --output-on-failure
```

支持已有截面构形、部分轮廓、切割、布尔、孔和三角化，但螺栓、焊缝、曲线路径及部分型材存在简化。未知现代轮廓不再默认为板。调用者应检查 `unbuiltPartIds` 与诊断；不可将构形成功数量等同于精确度，更不可直接作为制造验收依据。可选测试用解析解检查简单梁/板体积和网格索引。

起拱实体支持已有 `PartCambering` 的圆弧扫掠，并用同工程6个对象的336个独立IFC顶点验证；普通 `cambering` UDA不改变几何。起拱叠加加工/轮廓等未验证组合明确报告未构建，详见[起拱证据与边界](docs/CAMBERING.zh-CN.md)。

长期多格式路线和历史记录见 [逆向路线图](docs/REVERSE_ENGINEERING_PLAN.zh-CN.md)。本项目与 Trimble Inc. 没有隶属或背书关系；不包含 Tekla SDK、软件安装包或第三方专有模型。

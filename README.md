# TeklaFormats / TeklaDB1

离线读取 Tekla Structures / Xsteel 文件的 C++17 互操作库。方向是 **单文件解析 + 完整模型目录关联**。现有仓库名称和 `tekla::db1` API 保留兼容；新的工程入口为 `tekla::readProject`，CMake 同时支持 `TeklaFormats::core` 与 `TeklaDB1::core`。

不需要启动 Tekla，也不依赖其 SDK。核心依赖 zlib；Open CASCADE 几何后端可选。 缺少目录时的UPE/IPE使用明确标注的公称尺寸尖角回退；已修复折线段转角斜接，并对25个真实IFC实体做单向顶点距离与体积检查，见[截面与斜接证据](docs/NOMINAL_SECTIONS.zh-CN.md)。后续双向边界审计仍发现4个UPE及2个矩形加工件有残片，尚未解决，见[加工证据与限制](docs/CUT_GEOMETRY_EVIDENCE.zh-CN.md)。

## 当前支持范围

| 内容 | 接口与状态 |
|---|---|
| 主模型 DB1 | 7.30、7.64与7.82采用显式分支；8.30（275节）与8.74（307节）加入固定布局的有限核心投影，类别/所有权/螺栓等未验证语义保留边界，见[相关格式证据](docs/RELATED_FORMATS.zh-CN.md#新增公开旧工程-db1-830--874-与早期-dbv-边界)；8.37采用279节有限投影，螺栓定义/组留原始，见[8.37证据](docs/DB1_837.zh-CN.md)；U52 IFC/快照对照见[7.64证据](docs/DB1_764.zh-CN.md)，7.82表面处理见[表面处理证据](docs/SURFACE_TREATMENT.zh-CN.md)；其他已支持语义含构件、坐标、GUID、属性、装配、组件、螺栓、焊缝、切割/布尔关系及位置，详见各版本证据 |
| xslib.db1 | 7.82/现代布局的组件定义、参数、距离变量与公式引用图；8.37同包样本有限布局投影（247节，螺栓表留 raw，主库/xslib只确认文件配对），见[8.37证据](docs/DB1_837.zh-CN.md)；8.74 xslib 语义布局未支持，`readProject` 在该情形保留 raw 容器且不推测对象；EXCEL 7.82 和两份 9.52 组件库中的 type-39 记录在 owner 可核验时暴露为 `componentPartIds`，不计入独立零件/操作件；公式求解尚未实现 |
| DB1 钢筋 | 现代kind47实体、定义、父构件、名称/类别/材质/尺寸、半径和已存数组，见[钢筋证据](docs/REINFORCEMENT.zh-CN.md)；另恢复编号前缀/起始号及类型限定的对象引用，并在已验证的现代同工程DB1/DB2文件中暴露一项未解释的原始字段精确相等，见[钢筋编号](docs/REINFORCEMENT_NUMBERING.zh-CN.md)；分配编号、完整模式、保护层/钩和最终中心线未恢复 |
| profdb / pgdb | profdb明确支持版本9/10的完整10表签名；版本10由四份真实Tekla 2023目录回归，7.82真实工程中1,558条主库与322条xslib对象通过版本9目录的唯一型材名关联，目录内重复名会公开标记并跳过链接，见[关联证据](docs/RELATED_FORMATS.zh-CN.md#profdb-与-db1xslib-的型材名关联)；pgdb轮廓点暴露已读到的倒角type/X/Y/DZ1/DZ2/flags，拒绝重复/非法索引与非有限几何值。当前固定公开语料没有pgdb.bin，字段布局和几何仍缺真实样本独立复核；倒角 API 字段旁证与其边界见[PGDB说明](docs/RELATED_FORMATS.zh-CN.md#pgdb-与-sketch-solver-倒角字段) |
| 实验性 `.lis` 文本方言 | `parseProfileCatalogLis` 仅读取公开第三方测试夹具采用的 `PROFILE DATABASE EXPORT VERSION = 2` 方言；该夹具来源未证明为 Tekla 原生导出。Trimble 文档记载的 profile catalog `.lis` 使用 `PROFILE CATALOG EXPORT VERSION`、树结构及 profile records，本实现尚不支持。接口保留以便兼容，不能据此宣称型材目录 `.lis` 已支持；见[证据边界](docs/RELATED_FORMATS.zh-CN.md#型材目录-lis-导出) |
| matdb / screwdb / assdb | 材料、螺栓与螺栓组件目录；7.82真实工程按精确唯一名称建立3,148条主库和640条xslib零件→matdb关联，12个未匹配名称不链接；同样本的DB1螺栓参数串与screwdb商品规格名没有精确匹配，因此螺栓目录对象关联仍未建立，见[关联证据](docs/RELATED_FORMATS.zh-CN.md#matdb-与-db1xslib-的材料名关联)及[螺栓边界](docs/RELATED_FORMATS.zh-CN.md#screwdb-assdb-与-db1-螺栓的关联边界) |
| profitab / CLB | `G3-NAME` 的 `libraryId.SectionName` 与唯一 CLB 截面关联；`components.clb` 同目录 Include 也建立链接；保留完整语句树，不执行公式或生成几何。BIM4LCA/Tekla 2023 SP8 的真实 `profitab.inp` 固定验证224条规则及 WQS 的排序/单位/数字范围/宏字段，并有工程级目录与 PEIKKO `DELTA_EXT` 资源发现回归；样本没有精确目录-截面匹配，因此不构成二者关联证据 |
| Shapes XML / TEZ | 结构化读取Shape定义与Polymesh点、面、内环、边，支持XML几何及gzip输入；可选实体/类型字段、原文和歧义诊断，按storage ID关联；工程入口保留来源路径和资源优先级，抑制跨目录歧义，[证据与边界](docs/SHAPES_XML.zh-CN.md)。模型根目录及显式资源根/Shapes一级子目录中的 `ShapeCatalog.Groups.xml`、用户变更、`*.ShapeCatalog.Groups.xml` 和 `*.shapecatalog` 现列入 `Project::files`，标为 `Discovered`；文件内容、层级成员、shape ID关联及导入包仍未解析 |
| 钢筋形状目录 | 独立 API 与工程入口读取 `.rscx` 的 `ShapeCatalog` 分类、形状编号及根级未分类编号，保留原顺序与原文；真实 9.08 官方考试工程覆盖 5 个分类/14 个分类编号/2 个根级编号，[证据与边界](docs/REBAR_SHAPE_CATALOG.zh-CN.md) |
| 钢筋弯曲形状规则 | `rebar_schedule_config.inp` 有限读取官方示例中的 `BEND_TYPE_<类型>[序号]=值` 行，保留文件顺序和公式原文；未识别行也保留，公式不执行。`readProject` 按模型目录及显式资源目录的顺序选择该配置，并标为部分语义。固定语料无原生配置文件，不能据此声称完整支持。根目录 `RebarShapeRules.xml` 现列为 `Catalog/Discovered`，XML规则、目录优先级和首个匹配语义仍未解析，见[关联格式证据](docs/RELATED_FORMATS.zh-CN.md#rebarshaperulesxml-与-rebar_schedule_configinp) |
| `rebar_database.inp` 钢筋目录 | **尚未语义解析**。`readProject` 可将模型根目录中的同名文件标为 `Catalog/Discovered` 并明确报告语义解析未实现。这是钢筋/钢绞线材料定义目录，与 `.rscx` 的钢筋弯曲形状目录不同；应恢复 grade、nominal/actual diameter、bend/hook radius 和版本新增容差，并核验 DB1 钢筋定义/实例的 grade、size、radius 关联。当前固定公开语料没有该文件，见[关联格式缺口](docs/RELATED_FORMATS.zh-CN.md#rebar_databaseinp-钢筋目录) |
| `mesh_database.inp` 钢筋网片目录 | **尚未语义解析**。`readProject` 可将模型根目录中的同名文件标为 `Catalog/Discovered` 并明确报告语义解析未实现。这是与 `rebar_database.inp` 分离的网片规格目录；尚无固定原生样本、字段映射或网片实例关联证据，见[关联格式缺口](docs/RELATED_FORMATS.zh-CN.md#mesh_databaseinp-钢筋网片目录) |
| DB2 | 新增[7.64编号及范围关联](docs/NUMBERING_764.zh-CN.md)，四系列/八计数和103个历史对象核对；显式确认配对后105条系列关联；新增[9.08编号日志与跨文件关联](docs/NUMBERING_908.zh-CN.md)，4,936个历史对象及三项计数独立核对；`parseNumberingDatabase`：完整顺序表、编号系列与零件/装配序列计数；8.30/8.37/8.44/8.74仅接受已验证签名的空快照，非空记录拒绝语义投影；Abri-Moto 的8.44 DB2 空快照与次级同名 DB1 按 GUID 配对；9.50读取编号系列及计数但保留其余表为 raw；7.30内联编号、7.82及现代DB1对象编号记录可关联系列，见[对象编号证据](docs/OBJECT_NUMBERING.zh-CN.md)；**8.30/8.37/8.44/8.74非空系列、比较快照、8.44对象分配与有效状态尚未恢复** |
| environment.db | `parseEnvironmentDatabase`：属性名称/标签/存储类型、对象类别关联、整数选项列表；**元数据标志和部分定义值尚未解释** |
| options_*.db | `parseOptionsDatabase`：布尔/整数/浮点/字符串键与成对值槽位；**当前值/默认值的优先级尚未验证** |
| options.ini | `parseOptionSettingsFile`：模型文本赋值、行号、原始值与重复键；`matchOptionSettings`逐项比较DBV两个槽，工程入口保留文件/记录关联；**不推断运行时展开和最终有效值**，见[文本选项证据](docs/OPTION_SETTINGS.zh-CN.md) |
| DG 图纸 | 新增[9.50 gzip 图纸部分语义](docs/DG_950.zh-CN.md)：严格校验真实 2023 SP4 工程 9 张图纸签名，解码字符串、属性/链接和图幅；6 份 sidecar 按唯一文件名、DG 保存文件名及图幅尺寸建立文件级关联，不推断 DrawingGuid/MainObjectGuid 或 DB1 对象；subject、视图、尺寸、图元仍未恢复，且因 DG `grProjectGuid` 与所选 DB1 GUID 不匹配而不建立对象关联。新增[8.95/9.08容器与数字身份](docs/DG_EXAM.zh-CN.md)，修复目录数46误吞为版本句点；五张图幅/类型有XML对照，9.08主体与DB1的GUID/编号匹配，旧工程身份不强配；新增[7.64 DG有限语义](docs/DG_764.zh-CN.md)：10张同工程图纸严格验证表签名并读取文字、属性、图幅、主体、视图、数字引用及137条共同布局的 subtype-1 尺寸；图元和绘图精度仍未验证，且不与不同版本DB1配对；新增[7.13文字/属性/图幅](docs/DG_713.zh-CN.md)，仅对两个官方样本的34项容器签名解码共同字段；另支持[8.44容器、文字/属性/图幅、一条视图及13条直线尺寸](docs/DG_844.zh-CN.md)；`parseDrawing`：7.30/7.82/9.54容器、文本/属性、图幅、主体、视图坐标基/范围；7.30现在读取subtype 0/1存储尺寸锚点与尺寸链，35个固定文件为313/28条，subtype-1尚无独立打印真值；7.82的62条重复尺寸物理记录按原始偏移独立读取；28条重复尺寸集行中14组完整payload相同并在map中合并，同时保留occurrence；293张图纸仍有111条锚点引用96个不存在的尺寸集ID，保持未链接；DG关联诊断列出尺寸ID和尺寸集ID，context冲突时列出两端context；9.54另保留已检查视图记录中的原始纸面位置与比例值，不据此换算；7.82/9.54另有模型引用，7.30/7.82 type-309数字零件引用已支持显式同版本DB1关联，7.30其余引用未恢复，见[7.30证据](docs/DG_730.zh-CN.md)；7.30/7.82使用无工程范围的数字ID，8.95/9.08仍保存数字ID（仅9.08已验证按工程GUID和同存储版本自动配对），9.54使用GUID；9.54 type-281→type-311 引用提供同图全部物理候选行偏移并校验唯一/重复/悬空情形，见[图元字段证据](docs/DRAWING_ENTITIES.zh-CN.md)；7.30/7.82可由调用者通过默认关闭的`trustLegacyDrawingObjectIds`显式限定同版本工程范围；直线尺寸锚点/尺寸集支持7.30、7.64 subtype 1、7.82 subtype 0/1、8.44和9.54，7.64/7.82/8.44均缺独立尺寸真值，详见[尺寸与纸面边界](docs/DRAWING_DIMENSIONS.zh-CN.md)；**比例换算/缩短、尺寸样式与完整绘图图元尚未恢复** |
| TeklaStructuresModel.xml | 13个具名字段、严格直接节点匹配、UTF-8实体读取及完整原文保留；34份独立XML对照；工程清单将已读取元数据关联到主DB1，见[工程元数据](docs/MODEL_METADATA.zh-CN.md) |
| *.dg.metadata | `parseDrawingVersionMetadata`：图纸/主对象GUID、标题、图幅、类型及保存的整数状态字段；未知直接子字段以有序不透明扩展暴露并保留原文；工程入口校验DG文件名、图幅、主体并关联已验证DB1身份；无效UTF-8时支持Windows-1252回退且保留原始字节，见[图纸版本元数据](docs/DRAWING_METADATA.zh-CN.md)；**日期/标志含义、最新版本选择和当前有效状态仍未解释** |
| `history.db` | 内置 SQLite 只读读取用户、修订、对象和对象修订表；保留 `version_data` 原始字节。对象 identifier 与 DB1 identity GUID 精确匹配时建立关联；历史 type 编号不解释为 DB1 类型。详见[历史库说明](docs/HISTORY.zh-CN.md) |
| *.admodel | `parseAnalysisModelSettings`：具名整数/浮点/字符串与按引擎限定的设计表设置；125字段和3条设置有原生对照，保留Windows-1252原文、行号和重复项；工程入口可独立读取，见[分析预设证据](docs/ANALYSIS_SETTINGS.zh-CN.md)；**枚举含义、最终有效值和DB6关联未恢复** |
| DB6 分析数据 | `parseRawDatabase`：已验证10014头部布局的完整原始表、记录与分配器数据；工程入口发现根目录/Analysis子目录并标记Raw，见[DB6证据](docs/DB6_RAW.zh-CN.md)；**分析属性语义、DB1对象关联及其他DB6布局仍未恢复** |
| `analysis_results.db5` | `readProject` 在根目录或 `Analysis` 子目录发现并标记 `FileRole::AnalysisResults`；官方说明该库保存各荷载组合结果：`XS_AD_MEMBER_RESULT_DIVISION_COUNT` / `DISP_DIVISION_COUNT` 控制杆件结果采样，`MIN_DISTANCE` 控制杆件结果点间距，`GRID_SIZE` 控制板/楼板/面板结果网格；**文件内容未解析**，缺少带非空结果的原生样本与结果/DB6/DB1身份对照，见[DB5证据边界](docs/RELATED_FORMATS.zh-CN.md#db5-分析结果库)
| `*.db3` | `readProject` 在工程根目录或 `Analysis` 子目录识别为 `FileRole::PlanDatabase`；当前只发现文件，不解析内容或关联DB1/DB6。没有固定原生样本和独立字段真值，见[相关格式缺口](docs/RELATED_FORMATS.zh-CN.md#db3-规划数据库) |
| guid.mapper | 按批次读取旧/新GUID与行号，匹配当前身份或明确提供的两份模型；811对历史身份交叉验证，见[身份映射](docs/GUID_MAPPINGS.zh-CN.md)；不自动替换DG引用 |
| db.idrm / xslib.idrm | 读取静态 ID 范围、命名空间 GUID/起始号/数量与范围边界；工程入口在 namespace GUID 与 DB1/xslib 分配前导区精确匹配时建立文件级关联；不执行静态→运行时 ID 转换，见[IDRM说明](docs/IDRM.zh-CN.md) |

文件扩展名 `.db` 并不代表统一格式，目录文件、DBV 和 SQLite 使用不同入口。`readProject` 会区分 `Semantic`、`PartialSemantic`、`Raw`、`Discovered` 和 `Failed`；DG、DB2 与 DBV 当前为部分语义，不能把发现文件或读取部分字段当成解析完成。

| DB1 存储版本 | 语义支持证据 |
|---|---|
| 7.30 | 已固定公开教学主库和组件库，沿用旧构件/几何路径；恢复对象内联编号，1,467个对象与历史日志核对；另固定34组历史DB1/DB2备份，见[7.30编号证据](docs/INLINE_NUMBERING_730.zh-CN.md) |
| 7.64 | 新增U52主库/组件库；66个IFC构件与20个螺栓放置核对，67个旧快照构件逐项比较；GUID迁移未解释，保持部分语义，见[7.64证据](docs/DB1_764.zh-CN.md) |
| 7.82 | 两份主库、两份组件库：构件/板轮廓、18个组件归属 type-39 子构件、独立螺栓、焊缝、布尔/装配/属性与参数；部分语义，见 [字段证据与边界](docs/DB1_782.zh-CN.md) |
| 8.30 | 固定275节主库的有限核心投影；构件/装配/组件/焊缝/属性计数仅作解析基线，类别、所有权、编号、螺栓和表27仍未恢复，见[旧工程证据](docs/RELATED_FORMATS.zh-CN.md#新增公开旧工程-db1-830--874-与早期-dbv-边界) |
| 8.37 | 固定279节主库与247节 xslib 的有限投影；螺栓表留 raw，缺少独立字段真值和跨文件对象归属证据，见[8.37证据](docs/DB1_837.zh-CN.md) |
| 8.44 | 811对身份重映射；[型材IFC对照](docs/PROFILES_844.zh-CN.md)覆盖19个型材、58个名称和原点；[螺栓IFC对照](docs/BOLTS_844.zh-CN.md)覆盖79组/201位置，并修正非零首点丢失；完整签名、生成开关与实体几何仍待验证 |
| 8.95 | 本轮公开语料回归：27 份主库、27 份组件库；身份类型引用、owner 与构件辅助引用已分开读取，见 [归属证据](docs/OWNERSHIP.zh-CN.md) |
| 8.74 | 固定307节主库的有限核心投影；身份类别链接已做闭合检查，螺栓、钢筋、编号、型材语义和未映射表仍保留 raw；同版本 xslib 目前 raw-only，见[旧工程证据](docs/RELATED_FORMATS.zh-CN.md#新增公开旧工程-db1-830--874-与早期-dbv-边界) |
| 9.08 | 官方考试工程的两份历史快照，主库/xslib采用独立混合布局；7月4,237个梁候选与同提交官方代码吻合，49个型材名称由配套目录核对，两份快照各建立1,714条唯一对象→型材目录关联；每份主库的104条 PLATE type-39 记录均归属26个组件实例并单列为 `componentPartIds`；螺栓参数与完整几何保持部分支持，见[9.08证据](docs/DB1_908.zh-CN.md) |
| 9.50 | BIM4LCA 四份 Tekla 2023 SP8 DB1/IFC4 已恢复 table 353 的对象 ID、16 字节 GUID 及经 IFC 聚合边验证的 owner 字段：25,521 个 IFC GlobalId 直连匹配，5,017/5,017 条可配对聚合边的 owner 相符。Abri-Moto SP4 另核验 177 条 part、106 条 assembly、109 条 bolt group 记录，其中 158 个 part、99 个 assembly、108 个 mechanical fastener 的 IFC 身份命中相应 DB1 表。**部分语义**：part 类型仍未知，身份类别、context 和其他原始字段未解释；不代表完整支持。见 [9.50 证据与边界](docs/DB1_950.zh-CN.md) |
| 9.52 | 本轮公开语料回归：2 份主库/xslib/DB2 工程；两份组件库各核验14个组件归属的 type-39 CUTPART/PLATE 记录，另有一份 xslib 不含该类对象 |
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
$work = 'E:/CodexData/TeklaFormats'
New-Item -ItemType Directory -Force -Path "$work/tmp" | Out-Null
$env:TEMP = "$work/tmp"
$env:TMP = "$work/tmp"
vcpkg install zlib:x64-windows
cmake -S . -B "$work/build" `
  -DCMAKE_TOOLCHAIN_FILE=E:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build "$work/build" --config Release
ctest --test-dir "$work/build" -C Release --output-on-failure
cmake --install "$work/build" --config Release --prefix "$work/installed"
```

示例把构建、测试和临时文件放在 E 盘；如果使用其他盘符，请统一修改 `$work`。将 vcpkg 路径替换为实际安装路径，并让 vcpkg 本身及其安装目录也位于 E 盘。如果已有独立 zlib，可改用 `-DZLIB_ROOT=...`。MinGW 的检查工具已配置 Unicode 入口链接选项；C++20 的 `char8_t` 路径转换也兼容。`TEKLADB1_BUILD_TESTS=OFF` 可关闭测试与检查工具。

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

DB2 语义入口当前覆盖 7.30、7.64、7.82、8.95、9.08、9.52、9.60（包括空组件编号库）。`partCounter` / `assemblyCounter` 是保存的序列计数，不是当前构件数量，也不能单独还原某个对象的编号；其余字段和快照保留在 `raw` 中。现代DB1提供objectNumberingRecords和objectNumberingReferences，工程入口可关联DB2系列；零序号和高位特殊值不猜成确定编号。7.82 DB1另有独立的部分语义映射，其对象编号已按独立布局恢复；无GUID的旧DB1/DB2默认不配对，调用者可显式设置trustLegacyNumberingBasenames，并通过关联的pairingEvidence区分依据。7.30直接把对象编号保存在94/95表，公开inlineObjectId和storedNumber，不伪造引用；仅已验证的起始号1范围提供派生编号，见[内联编号证据](docs/INLINE_NUMBERING_730.zh-CN.md)。

DG 9.54 会校验完整表目录签名，恢复根字符串（包括跨记录的标记 XML）、属性、属性链接、图幅宽高、一类模型 GUID 引用，以及已观察的 type-311 XY 线段坐标和 type-281→311 原始引用。固定 Steel Fundamentals 样本中 47/47 个此类目标 ID 闭合；接口不推断 owner/context。根节点为 `Mark` 的 XML 同时提供节点、属性与文本结构，原文保持可访问；模板表达式不会被求值或自动绑定到模型。`grProjectGuid` 相符后，工程入口才用引用 GUID 连接 DB1 identity；无法匹配或有歧义的引用给出诊断。其他 DG 版本的 type-311 也可读取两组已存坐标，但其他图元类型、风格、上下文和渲染语义仍未恢复。不会通过图纸文件名猜测构件，也不把这些字段称为完整图纸解析。边界见 [DG 图元说明](docs/DRAWING_ENTITIES.zh-CN.md)，研究记录与独立编号日志验证见 [DB2 / DG 格式证据](docs/RELATED_FORMATS.zh-CN.md)。

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

`tests/corpus.json` 当前记录 881 个固定文件条目（包含压缩包成员）、18 个来源压缩包和 1,787 个用例；固定文件有来源 URL、大小和 SHA-256。首次下载估算约 322 MiB（其中约 151 MiB 为压缩包，其余为直接文件），展开后的文件约 272 MiB。语料包括单段/两段 IDRM 样本、W3 的主库/xslib namespace 关联和 AUVENT 当前/备份前导区对照，以及 Trimble 官方练习包中两张 7.13 DG 图纸的文本/属性/图幅回归；RABAS 工程10张7.64 DG可逐字节重建，并解析文字、视图、数字引用和137条 subtype-1 尺寸字段（无独立出图尺寸真值；其DB1是7.30，因此不建立对象关联）；还覆盖8.30、8.37、8.74主DB1有限投影、8.37 xslib有限投影与空DB2快照解析。独立检查包括34份XML元数据、811对历史身份映射，以及8.44的IFC型材、螺栓参数与位置对照。公开 Construsoft 9.52 图纸视图练习模型和 Project2 钢结构工程分别提供独立的 DB1/xslib/DB2 项目回归；前者逐对象验证24条 DB1→DB2 编号系列关联，后者校验2,014个主库构件、组件库语义基线及三条精确文件配对。第三方模型不随仓库分发。需要 Python 3.11+：

```powershell
$work = 'E:/CodexData/TeklaFormats'
New-Item -ItemType Directory -Force -Path "$work/tmp" | Out-Null
$env:TEMP = "$work/tmp"
$env:TMP = "$work/tmp"
python -B tools/corpus.py --download `
  --data "$work/corpus" `
  --exe "$work/build/Release/tekladb1_validate.exe" `
  --report "$work/corpus-results.json"
```

Ninja/MinGW 构建的 exe 通常直接位于构建目录，不含 `Release` 子目录；Linux 使用无 `.exe` 的路径。不带 `--download` 时只验证本地文件。首次下载约 322 MiB；展开后语料约 272 MiB，具体文件及散列以 manifest 为准。

默认 CTest 有625项正常/异常输入测试；当前完整语料回归为1,787/1,787，安装消费者回归93/93。`profdb.bin` 现仅接受已验证的版本9/10签名，四份真实Tekla 2023版10目录、目录内重复名称歧义与异常布局均有回归。DG type-304 索引现暴露按类型和ID精确命中的目标物理行偏移，重复候选全部保留；type-311 两组 XY 坐标按行保留，9.54 type-281→311 引用也按原始字段公开；其余绘图实体和语义仍待恢复，见[DG 图元说明](docs/DRAWING_ENTITIES.zh-CN.md)及[相关格式说明](docs/RELATED_FORMATS.zh-CN.md)。新增8.30与8.74主库固定布局的部分语义投影；8.74 xslib尚无语义映射，但工程入口会保留raw容器并明确标为Raw，不猜对象关系。8.74 xslib公开工程回归检查271表、1条记录和1个opaque section；这不等于组件库语义支持。七项8.30/8.74输入回归覆盖布局读取、损坏宽度/字段拒绝，以及8.74 identity-class 与关联类型冲突拒绝；独立脚本在真实8.74样本上核对5,674个类型关联端点的一致性，但不作为Tekla外部对象真值。`rebar_schedule_config.inp` 现有限读取官方示例中 `BEND_TYPE` 映射，工程入口按资源目录优先级读取并标为部分语义，公式只保留原文、不执行；固定语料无原生配置，不能据此声称完整支持。PGDB的合成坏输入回归现在拒绝重复字符串/轮廓/点块键、非有限坐标与解算值；固定公开语料仍无真实 `pgdb.bin`，所以其轮廓字段含义保持未独立核验。统计和语义指纹用于防止回归；它们不等于 Tekla/IFC 独立几何真值。数据来源具体记录在 manifest 中。

图纸版本元数据现有九份 XML 独立校验：五份官方考试工程文档核对22个具名字段，另四份钢结构练习工程文档核对额外整数 `AlwaysNecessary=0`；两个工程共九组 DG 配对及五条 DB1 主体关联。`AlwaysNecessary` 仍无已证实含义，不参与状态或版本判断。详情见[元数据证据](docs/DRAWING_METADATA.zh-CN.md)。

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

等截面ELD支持解析椭圆，并修正斜置截面在折线路径上的额外扭转。真实检查覆盖12个构件、7,872个IFC顶点及独立体积；变截面ELD仍明确报告未构建，详见[椭圆截面证据](docs/ELLIPSE_GEOMETRY.zh-CN.md)。

长期多格式路线和历史记录见 [逆向路线图](docs/REVERSE_ENGINEERING_PLAN.zh-CN.md)。本项目与 Trimble Inc. 没有隶属或背书关系；不包含 Tekla SDK、软件安装包或第三方专有模型。

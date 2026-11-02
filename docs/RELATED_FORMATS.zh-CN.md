# DB2 / DG / DBV 格式证据与未完成项

## 型材目录 `.lis` 导出

Trimble 文档描述的型材目录导出 `.lis` 首行是 `PROFILE CATALOG EXPORT VERSION = n`，其后还有目录树及型材定义；固定型材条目还可能有 `TYPE = 998` 的多截面定义。官方文档没有记载 `PROFILE DATABASE EXPORT VERSION` 这一语法。[公开 `model-maker` 测试仓库](https://github.com/sbabadag/model-maker/blob/43a6b7934696d80d23e5f1915210c8248559b1c6/tests/data/sample_profiles.lis)提供一份固定的 `PROFILE DATABASE EXPORT VERSION = 2` 文件，含两个条目及26个数值属性，但仓库未提供许可证或可核实的 Tekla 导出来源。因此它只能作为第三方公开夹具，不能证明是原生 Tekla 样本。本项目通过清单记录固定提交、URL、大小和SHA-256，原件单独下载到外部语料，不随源码分发。

新增 `parseProfileCatalogLis`，严格读取该夹具的 VERSION 2 记录块：引号包围的 `PROFILE_NAME`、同一行保存的 `TYPE`/`SUB_TYPE`/`COORDINATE`，以及独立属性块中的带引号名称与有限数值。未知数值属性名会原样保留；重复属性、损坏的整数/数值、错误块边界、未知版本、NUL字节、超过64 MiB的文件均失败并清空结果。重复型材名称的所有行仍保留，同时写入 `ambiguousProfileNames`，不按文件顺序静默挑一个。`rawText` 保存完整输入，`rawRecord` 提供单条记录原文。函数名为兼容性 API；它不是 Trimble 文档所述的 profile catalog `.lis` 解析器。

当前解析器不把 `storedCoordinate` 命名成具体空间坐标，也不假定属性单位、解释 `TYPE`/`SUB_TYPE` 枚举、生成截面几何、补全未出现的字段，或把这些夹具记录并入 `profdb.bin`。公开仓库中的 `profiles/european.lis` 虽有123条型材，但没有上述版本头且语法不同。此实现的支持级别是“第三方夹具方言实验性读取”，不是 Tekla 原生 `.lis` 支持。下一步需取得可验证的 Tekla 导出，按官方分节语法实现树、profile 记录、CLB/RULES 跨文件关系，并覆盖版本及多种 item type。Trimble依据：[型材目录导入/导出](https://support.tekla.com/doc/tekla-structures/2023/mod_catalogs_exporting_and_importing_profiles)、[材料目录 `.lis` 交换说明](https://support.tekla.com/doc/tekla-structures/2022/mod_importing_and_exporting_material_grades)。

## screwdb / assdb 与 DB1 螺栓的关联边界

PSDBIM EXCEL 7.82 工程含 718 个 DB1 独立螺栓、2,208 条 `screwdb.db` 商品规格和 36 条 `assdb.db` 组件定义。DB1 独立螺栓的 11 种非空 profile 字段都是 `MM…/…` 参数串，与 screwdb 商品名称没有精确命中；assdb 组件字段保存的是螺栓标准，不能按商品名称比较。`screwdb.db` 的标准字段已从原始记录解码；在同一资源目录中，215 个非空 assdb 组件槽位与 screwdb 标准精确相等，涉及33种标准。每个槽位关联到该标准下全部尺寸规格记录；26种 assdb 标准在该 screwdb 中未找到，保持未关联。

进一步把每种 DB1 profile 开头的 `直径*长度` 数值与 screwdb 的目录直径/长度作精确浮点比较：11 种串中8种分别命中6–10个不同标准（同一尺寸对应多条不同商品名），另3种没有任何尺寸对命中。这个实验只能说明这些数值不能唯一识别螺栓目录项；profile 字段的数值语义本身也未由独立导出证实。因此 DB1 个体螺栓到 screwdb 规格的映射仍未建立，不按相似尺寸、包含关系或前缀推断。下一步需要保留标准/规格标识的 Tekla 导出，或另一份可与螺栓明细逐项核对的公开工程样本。

## matdb 与 DB1/xslib 的材料名关联

公开 PSDBIM EXCEL 7.82 工程同时提供主库、xslib 与 `matdb.bin`。工程入口按零件保存的材料名和目录材料名做大小写敏感的精确匹配，并保留数据库路径、对象 ID、材料名及目录记录索引。语料测试独立重算唯一名称关联集合，确认主库 3,148 个、xslib 640 个对象全部且仅有唯一目录匹配；另有 12 个唯一未解析名称保持未链接。重复目录名也不建立链接。此证据只验证材料目录引用，不能证明密度/强度等属性是分析时有效值，也不把别名自动合并。详见 `tests/corpus.json` 的 `material-resource:PSDBIM main/xslib to matdb`。

## profdb 与 DB1/xslib 的型材名关联

公开 PSDBIM EXCEL 7.82 工程同时提供主 DB1、xslib 和版本9 `profdb.bin`。工程入口以零件实际保存的型材名做区分大小写的精确匹配；语料回归确认1,558条主库与322条xslib对象关联到同一个型材目录，逐条核对源数据库、对象ID和原始型材字符串。另从四个 BIM4LCA/Tekla 2023 工程包固定了四份版本10目录；其首表宽度与版本9不同，解析器现按版本9/10分别验证完整10表payload签名、记录标签及边界。四份目录解析出的名称数为3,760、3,763、3,760、3,761；这是跨工程容器/名称读取回归，不是Open API字段真值，也没有据此建立跨文件型材关联。未匹配、存在多个目录来源或在单个目录内有重复定义的名称不建立链接。`ProfileCatalog::ambiguousProfileNames` 会列出目录内重复名；旧 `profiles` map 仍保留一个兼容性代表项，不能把该项当成唯一记录。真实 v9 PSDBIM 样本有7组重复名称、21条重复行；v10 renatogcruz 样本有14个重复定义名，且记录可能带不同 shape ID。轮廓表只按名称键控，重复定义各自的轮廓归属尚未确定，因此不猜测拆分或关联。已验证的名称引用不验证目录截面几何与对象几何的一致性；详见 `tests/corpus.json` 的 `profile-resource:PSDBIM main/xslib to profdb` 及 BIM4LCA `profile_catalog` 用例。

## pgdb 与 Sketch Solver 倒角字段

当前 `parseProfileGeometryCatalog` 将五个固定宽度表连接为求解型材名称、轮廓点和倒角记录；实际公开语料及本机只读工程目录尚无原生 `pgdb.bin`，所以表偏移、单位、行标记和实际截面几何仍缺真实文件及独立 Tekla 导出核对。已找到一个 Tekla 2020 公共模型目录页，其文件清单同时列出 `pgdb.bin`、`profdb.bin`、DB1、DB2 与 DG，但站点说明下载需注册且使用积分；本轮未尝试注册或绕过限制，故该页只是样本线索，不是可复取样本。合成夹具只用于覆盖边界行为，不能证明这些字节布局适用于 Tekla 文件。

Trimble Open API 的 [`Chamfer` 属性](https://developer.tekla.com/doc/tekla-structures/2025/chamfer-properties-52187)将 `X`、`Y`、`DZ1`、`DZ2` 描述为倒角尺寸/厚度属性；其中 `DZ1` 是偏移上方厚度，`DZ2` 是偏移下方厚度，并注明在 PolyBeam 上不起作用。[`ChamferTypeEnum`](https://developer.tekla.com/doc/tekla-structures/2025/chamfer-chamfer-type-enum-enumeration-52193)列出 0–7：NONE、LINE、ROUNDING、ARC、ARC_POINT、SQUARE、SQUARE_PARALLEL、LINE_AND_ARC。该 API 语义说明轮廓点可以携带这些属性，但它没有说明 `pgdb.bin` 的二进制偏移，也没有证明当前读取的 `type` 字值与该枚举逐项对应；`flags` 仍未解释。因此公开结构继续保留原始数值，解析器不按这些值生成圆弧或最终截面。取得原生 PGDB 后，应以同一 Sketch Solver 型材的 Open API `GetCrossSection` 输出逐点核验名称、点序、倒角类型和尺寸，再决定是否把候选映射升级为支持语义。

## `rebar_database.inp` 钢筋目录

Trimble 将 `rebar_database.inp` 定义为钢筋与钢绞线目录；Open API 单独提供 `RebarItem` 和 `RebarHeaderItem` 访问该文件。它与 `.rscx` 的 `RebarShapeCatalog` 是两类资源：前者描述钢筋等级、直径和弯曲半径等材料/规格属性，后者描述弯曲形状分类及编号。工程入口现在会把模型根目录同名文件识别为 `Catalog/Discovered` 并明确标注尚未语义解析；解析器仍没有读取目录内容，固定公开语料也没有原生文件。

下一步应取得可公开复取的不同环境/版本原生文件，恢复文件头、记录边界与各字段，再使用 Tekla API 导出对照。API 明确列出 `RebarItem` 的 `Code`、`Grade`、`Size`、`Usage`、nominal/actual diameter、截面积、单位长度重量、弯曲半径、90/135/180 度钩长度和半径；2023 又新增 `ExtraPointShortening`、`CurveTolerance`、`MaxRadiusRequiringBending`。官方日文环境 FAQ 另列出传统目录字段 `Min bend`、90/135/180 度各自的角度/半径/余长；应将其视作旧字段名候选，由原生样本和 API 导出逐列确认，而不是直接套用到其他环境/版本。与 DB1 钢筋建立资源关联时，需要按实际保存的 grade/size 等键唯一匹配并比较实例半径；未匹配或多重匹配不选取猜测结果。官方依据：[目录文件说明](https://support.tekla.com/doc/tekla-structures/2022/sys_input_files)、[`RebarItem` API](https://developer.tekla.com/doc/tekla-structures/2025/rebar-item-class-46851)、[2023 API 变更](https://developer.tekla.com/documentation/tekla-structures-2023-open-api-release-notes)、[Japan 环境字段 FAQ](https://support.tekla.com/pt-pt/node/135781)。这些文档提供字段语义线索，不是文件布局或当前实现的证据。Trimble 的[环境安装下载说明](https://support.tekla.com/article/download-tekla-software-installers-service-packs-environments)要求使用 Trimble Identity 登录；官方[目录文件说明](https://support.tekla.com/doc/tekla-structures/2026/sys_catalog_files)将这些 `.inp` 目录放在版本/环境的 `profil` 目录。此次在线检索没有找到可匿名复取的钢筋目录字节，故下一条可验证样本路径是合法环境安装包，或由 Tekla Structures 导出的目录文件；在获得原件前不扩展解析器语义。Trimble 还明确说明可以从 Tekla Structures 导出单项、分组或完整 rebar catalog 为 `.inp`，并允许通过 Tekla Warehouse 分享 catalog 内容；CORENET X 的公开 [IFC-SG 指南](https://info.corenet.gov.sg/docs/default-source/default-document-library/tekla-structures-ifc-sg-how-to-guide.pdf?sfvrsn=b3a693d8_1) 将一份 `rebar_database.inp` 列为 TSEP 扩展的安装内容。继续追踪同一 [Tekla Warehouse 集合](https://warehouse.tekla.com/#/collections/online/ff8ab605-eb15-467c-ab6c-b1fad5aa0e71)的只读 API 元数据后，确认其中有 `D Rebar Grades - AUS` 包（`0fdaad1f-640f-49b6-a6ac-001e28250f33`），版本列出22个 `.rexp` 单项；另有 `Rebar Shape Catalog - AUS` 包（`5a19759e-0ebf-4746-acf5-02db51b75355`），其版本列出 `RebarShapeCatalog.rscx` 和 `RebarShapeRules.xml`。但这些二进制使用受限内容端点，匿名请求分别返回 `CONTENT_ACCESS_MISMATCH` / `ANONYMOUS_ACCESS_NOT_ALLOWED`；目前只取得包元数据，没有取得任何文件字节，故不能固定语料、验证格式或声称已解析。完整模型模板/模型包仍更可能含公开的 `rebar_database.inp`；当前固定语料扫描未发现该文件成员。

## `mesh_database.inp` 钢筋网片目录

Trimble 将 `mesh_database.inp` 单列为钢筋网片定义文件，并明确标准网片不属于 `rebar_database.inp`。2025 Open API 的 `MeshItem` 提供 `Name`、`Grade`、纵横向直径/间距、长宽、两方向左右悬挑和最小/最大搭接属性；API 另有 `.mexp` 单项导出/导入方法。这些属性可作为逐字段校验目标，`.mexp` 不能替代对数据库文件本身的布局验证。工程入口现在把模型根目录同名文件识别为 `Catalog/Discovered`，明确标注尚未语义解析；当前没有固定公开原件、文件语法/字段映射或 DB1 网片实例到目录规格的已验证关联，因此不把钢筋条目录支持外推为网片支持。后续需获得含原生目录及非空网片实例的同工程样本，以 Tekla 网片目录/API 导出核对规格、线径、间距、材质和方向，再实现唯一键关联与异常数据检查。依据：[Tekla 2023 输入文件说明](https://support.tekla.com/doc/tekla-structures/2023/sys_input_files)、[`MeshItem` API](https://developer.tekla.com/doc/tekla-structures/2025/mesh-item-class-46705)、[`MeshItem` 属性](https://developer.tekla.com/doc/tekla-structures/2025/mesh-item-properties-46707)、[网片单项导入方法](https://developer.tekla.com/doc/tekla-structures/2025/import-mesh-items-method-46567)。Trimble 的[环境安装下载说明](https://support.tekla.com/article/download-tekla-software-installers-service-packs-environments)要求使用 Trimble Identity 登录；本轮未找到可匿名复取的 `mesh_database.inp` 原件，因而保持 `Catalog/Discovered` 状态。

## `RebarShapeRules.xml` 与 `rebar_schedule_config.inp`

这两种文件是钢筋形状识别配置，和 `RebarShapeCatalog.rscx` 的分类/形状编号索引、`rebar_database.inp` 的钢筋规格字段都不同。Trimble 文档说明，Rebar Shape Manager 在模型、项目、公司和系统目录中读取有效的 `RebarShapeRules.xml`；模型目录优先，规则顺序有语义，首个匹配的形状规则会决定识别结果。配置包含 shape code、弯曲形状规则、尺寸/角度/半径容差、钩子检查，以及弯曲清单字段；容差是逐规则文件保存的。识别会按弯曲角、扭转角、腿长规范方向，并尝试原始及反转点序。Tekla 还说明识别容差会参考 `rebar_database.inp` 的 extra point shortening、curve tolerance、maximum curve radius requiring bending，以及单个 DB1 钢筋对象上的识别属性。`XS_USE_USER_DEFINED_REBARSHAPERULES=FALSE` 时则改用 `rebar_schedule_config.inp` 中定义的内部弯曲类型及区域代码映射。[形状识别规则说明](https://support.tekla.com/doc/tekla-structures/2023/rei_shape_recognition_tips_rebar_shape_manager)、[规则文件和字段说明](https://support.tekla.com/doc/tekla-structures/2026/det_user_defined_reinforcing_bar_shapes)、[旧配置开关](https://support.tekla.com/doc/tekla-structures/2023/xs_use_user_defined_rebarshaperules)。

`parseRebarScheduleConfig` 现按文件顺序读取 `BEND_TYPE_<内部类型>[序号]=值` 项，保留行号和等号右侧原文；非空未知行留在 `unparsedLines`，重复键和损坏项拒绝。它不解释/执行公式，不证明内部弯曲类型或报表字段含义。`readProject` 可按模型目录与显式资源目录优先级装载该配置，可通过 `ProjectOptions::readRebarScheduleConfig` 关闭，文件级别为 `PartialSemantic`。回归输入取自 Trimble 官方文档公布的 `5_1`、`6_2` 及条件映射示例（[2025 模型创建指南](https://support.tekla.com/dist/sxf/document/TS_MOD_2025_en_Create_models.pdf)）；固定语料仍没有原生 `rebar_schedule_config.inp`，因此当前仅为有限行读取能力，不宣称完整文件支持。模型根目录的 `RebarShapeRules.xml` 会在工程文件清单中标作 `Catalog/Discovered`；XML结构、按规则的顺序和目录优先级均未解析。

## Shape Catalog 分组文件和导出包

Trimble 文档说明模型目录使用 `ShapeCatalog.Groups.xml`，模型内的用户变更写入 `ShapeCatalog.Groups.user.<username>.xml`；项目/公司/系统资源中的 `Shapes` 子目录还可带 `*.ShapeCatalog.Groups.xml`。单独导出组结构时文件后缀为 `.ShapeCatalog.Groups.xml`，导出形状与组时使用 `.shapecatalog`。[导入形状说明](https://support.tekla.com/doc/tekla-structures/2026/mod_importing_a_shape)、[分组说明](https://support.tekla.com/doc/tekla-structures/2026/cus_work_with_shapes_and_groups)

工程入口现在会把模型根目录及显式资源根中的分组 XML/交换包列为 `Catalog/Discovered`；资源根的 `Shapes` 目录扫描本身及其一级子目录，不递归读取更深的任意目录。官方[导入说明](https://support.tekla.com/doc/tekla-structures/2026/mod_importing_a_shape)和[分组说明](https://support.tekla.com/doc/tekla-structures/2026/cus_work_with_shapes_and_groups)确认这些文件用于交换层级分组，shape 定义仍单独保存在 XML/TEZ 中；但本轮没有找到可匿名复取的分组文件原件或格式结构。因此尚未解析 XML、组层级、shape ID成员关系、用户变更合并/优先级或 `.shapecatalog` 包内容，也不创建到 shape 定义/几何的语义关联。此项是文件清单覆盖，不计为格式解析。

`RebarShapeRules.xml` 仍未解析。后续需用原生文件恢复有序规则、条件、容差、钩子及清单字段，再结合对应模型选项、`RebarShapeCatalog`、钢筋规格和 DB1 钢筋实例验证规则生效路径。规则匹配依赖模型/项目/公司/系统目录优先级，跨资源目录的配置合并与冲突仍需要多目录原生样本。现有 DB1 的 `storedShapeCoordinates` 和 `radiusValues` 尚不能单独复现 shape code 识别，也不代表最终钢筋中心线。

## DB1 9.50 身份表：已恢复字段与边界

BIM4LCA 提供的四份 Tekla 2023 SP8 DB1 配对 IFC4 样本确认 table 353 使用 32-byte identity payload。解析器保留四个原始 u32、恢复对象 ID 与 16-byte GUID，并把经跨文件证据验证的第四个 u32 暴露为 owner ID。独立验证累计匹配 25,521 个 IFC GlobalId；5,017 条可定位到 DB1 身份记录的 `IFCRELAGGREGATES` 边均满足 child.owner ID 等于 parent.object ID。零件对象 ID 与 table 274 相符，IFC element assembly GUID 与 table 300 相符。

第一字、第四字和 GUID 之外，不对其他字或身份类别作语义推断。9.50 part 类型保持未知；context、强化筋分类和依赖身份类型的引用仍未恢复。配对 IFC 的匹配率不是完整模型一致性或几何正确性的证明。独立复核命令与样本结果见 [9.50 证据记录](DB1_950.zh-CN.md)。

## profitab.inp 与 CLB 参数化型材

工程读取现在将 `profitab.inp` 的 `G3-NAME` 按唯一点号拆成库 ID 与截面名，并从项目目录及显式传入的资源目录读取 `.clb`。CLB 仍完整保留通用语句树，同时提取唯一顶层 `library_id`、各 `Section_type` 名称、`base_attribute` / `expression` 声明的名字/类型/描述/默认值/公式，以及 geometry 中的 face 索引、点的起止端索引和 y/z 表达式名。`Project::profileRuleSectionAssociations` 只在 `(library_id, section name)` 精确命中一个截面时建立链接；缺失或重复候选留有诊断。公式不执行、点引用不求值、不生成轮廓，也不将参数类型代码推断为完整尺寸语义。

补入 BIM4LCA 2023 SP8 Housing Concrete 工程归档中的原生 `profitab.inp` 作为固定公开语料成员；其文件头逐列说明 `Prefix`、`Type`、`SO`、`Z`、`MI`、`MA`、`G3-NAME`、`Z3-NAME`。固定样本解析为224条规则且无诊断，`WQS` 行逐字段核对 `SO=0`、空 `Z`、`MI=6`、`MA=9`、`G3-NAME=1Gen.WQS` 和名称模式，作为真实目录语法回归。新增项目级回归同时从模型目录读取该目录和显式资源目录下的 PEIKKO `DELTA_EXT` CLB，确认224条目录规则、CLB 库/截面名和零条精确规则链接；这验证了资源发现及“不误配”，并没有证明二者有关联。原包的 CLB 是 PEIKKO `DELTA_EXT`，目录中没有对应条目，因此仍不能以此样本证明 `profitab` 到该 CLB 的实际跨文件关联。

`profitab.inp` 的 sort order、unit、最小/最大数字列按完整十进制整数读取；空列仍映射为零。非整数和超出 `int` 范围的行现在保留行号诊断并跳过，避免坏列被静默当成零后参与 CLB 关联。工程级变异回归进一步确认：即使损坏行的 `G3-NAME` 与某个真实格式 CLB 截面完全相同，也不会生成 `profileRuleSectionAssociation`；该检查同时通过安装包消费者入口。库内与安装消费者回归覆盖正常值、空列、非法文本和溢出；这只验证输入处理，不扩展 `profitab.inp` 的版本或字段语义范围。

字段语法依据 Trimble [Create parametric profiles using .clb files](https://support.tekla.com/doc/tekla-structures/2024/mod_creating_parametric_profile_using_clb_files)：官方示例使用 `library_id "1Gen"`、`Section_type` / `name "RectChamfer"`、`base_attribute` 和 `expression`；同页说明 `profitab.inp` 的 `G3-NAME` 可用点号连接库 ID 与截面名，并说明 `components.clb` 以 `Include` 列出 CLB 文件。工程入口现提取顶层 `components.clb` 的 Include，并只解析同目录下的单一 `.clb` 文件名；路径穿越、缺失、重复大小写匹配或未解析文件均跳过并标记部分语义。

真实样本来自 Nordic Sustainable Construction 发布的 [BIM4LCA STRUCTURAL.zip](https://www.nordicsustainableconstruction.com/knowledge/2024/august/bim4lca-files)，其中包含可由 Tekla Structures 2023 SP8 打开的结构工程。本仓库只记录公开归档和内层工程压缩包的 URL/成员路径、字节数与 SHA-256；运行语料工具时才在语料目录下载/解包，不再分发第三方模型字节。房屋混凝土工程中的 `PEIKKO.DELTA_EXT.clb` 固定校验库 ID `PEIKKO`、截面 `DELTA_EXT`、9 个尺寸、18 个表达式、一个 14 点几何面，以及一个精确公式和首尾点引用。

此 CLB 文件证明解析器能读取真实项目语法和这些字段；它同包的 `profitab.inp` 没有 `PEIKKO.DELTA_EXT` 条目，因此不据此宣称 profile catalog 与 CLB 的项目级关联已经验证。公式不执行、点引用不求值、不生成轮廓，也不将参数类型代码推断为完整尺寸语义；其他来源、版本和 CLB 语法变体仍需真实样本覆盖。

新增一份公开 [Construsoft 图纸视图练习模型](https://dl.construsoft.com/install/2024/Models/Defining%20drawing%20views%20steel.zip) 的 Tekla 9.52 主 DB1、xslib、主 DB2 与空 xslib DB2。主库恢复22个零件、10组螺栓、84条焊缝、2个装配；组件库恢复8个零件、92个参数和4个自定义定义；主 DB2 含13个系列。工程入口从仅这4个固定文件建立3条文件关联，并逐一核对24条对象编号系列关联的数据库GUID范围、对象身份、记录ID及唯一系列索引。归档URL、成员路径、大小和SHA-256及逐文件语义指纹固定在`tests/corpus.json`。这是独立工程的回归覆盖，不提供新的IFC/DG真值，也不扩大9.52未验证字段的语义范围。

Trimble 官方 Open API 图纸练习包另含两张 7.13 图纸。新增严格 34 项顺序容器签名以及文本、属性/链接和图幅读取；两份图纸分别恢复40/41条文字、4/5条属性、4条属性链接和1个图幅。视图、主体、尺寸及图元字段继续保留原始记录，见 [DG 7.13 证据](DG_713.zh-CN.md)。

另固定公开 [Project2_Steel_Detailing](https://github.com/MoamenShehta/Project2_Steel_Detailing/tree/5bf5b4e5dff67b867c6150fae06526c491a75789) 的 9.52 主库、xslib 和两份 DB2。解析回归覆盖主库 2,014 个构件及 xslib 的 498 个构件、3,627 个参数定义和 325 个自定义定义；其中14条 type-39 记录均有 GUID、定义及指向已解析组件实例的 owner，名称为12条 CUTPART、2条 PLATE。它们保留在 `parts`，并在 `componentPartIds` 中单独列出，不计作独立零件或操作件。另一份独立发布的 9.52 xslib 也逐项通过相同检查；Construsoft 练习的 xslib 没有 type-39 记录。工程校验器检查主 DB1→xslib、主 DB1→同名 DB2、xslib→xslib DB2 三条路径关联；此样本两个 DB2 都没有系列。计数是解析回归基线，不是独立几何真值；type-39 的分类仅适用于已有直接 owner 证据的7.82/9.52组件库样本和9.08主库快照。

EXCEL 7.82 xslib 的18条 type-39 记录也已通过直接身份 owner 关联验证：18条都属于8个已解析组件实例，CUTPART 12条、PLATE 6条；MOHSEN 7.82 xslib 没有此类记录。组件库 API 将其列入 `componentPartIds`，不计入独立构件/操作件。此结论限于固定 EXCEL 样本，不回推到其他旧布局。

9.08 官方考试工程的两份主库快照各有104条 type-39 `PLATE` 记录，定义 subtype 均为2。它们的身份 owner 全部命中解析后的组件实例，context 与 owner 相同；共有26个 owner、每个4条。现列入主库的 `componentPartIds`，保留在 `parts` 而不计作独立构件/操作件。配套两份 xslib 没有 type-39 记录。该结论限于这两份同工程快照。

新增7.64 DB2：精确12字节头、76字节系列记录、空组件库及显式限定范围的DB1关联，四条系列与编号历史独立核对，见[7.64证据](NUMBERING_764.zh-CN.md)。

目标仍是多格式完整解析，以下是 2026-09-26 恢复的局部语义。`PartialSemantic` 不等于完整实现。

## 样本与可复验依据

- 已固定的64份公开DB2包括7.30、7.82、8.95、9.52、9.60，32份主编号库和32份空组件编号库；另固定7.30同工程34份历史备份DB2及其配对DB1。实际编号系列出现在7.30、7.82、8.95和9.60样本中；7.30教学工程含20个系列，Construsoft练习的8.95主DB2含12个系列。不能把空库通过视为丰富语义覆盖。
- 7 份 DG 9.54 来自同一培训模型；另有 293 份 DG 7.82 来自 PSDBIM 工程。另有35份公开教学DG 7.30，其91个视图、文字、图幅及主体已恢复，详见[7.30字段与证据](DG_730.zh-CN.md)。另有1份8.44图纸支持可变记录尾部、文字/属性、已存图幅、一条视图及13条subtype-1直线尺寸与5个尺寸集，见[8.44证据](DG_844.zh-CN.md)。四种布局分别校验，不能推断其他版本兼容性。
- 培训模型来自 [letstekla 仓库固定提交](https://github.com/letstekla/Tekla-Structures-Drawing-Automation-Through-Grasshopper-in-Rhinoceros-3D/tree/efb24b30a722de4afc9052331e2e24b69abba6cf)。模型及 `numberinghistory.txt` 位于同一 ZIP；下载地址、文件大小及 SHA-256 见 `tests/corpus.json`。
- [Trimble 文件说明](https://support.tekla.com/doc/tekla-structures/2023/sys_files_and_file_extensions)确认 DB2 用于编号、environment.db 用于用户属性定义；这些官方描述不提供下述二进制字段偏移。偏移来自样本分析。

## DB2 顺序表

Trimble官方Open API图纸练习包里的8.37主DB2有10张顺序表但均无记录，表22声明80字节系列行；同包xslib.db2仅含带GUID的8.37文件头。解析器只接受这类空8.37快照，遇到任何非空表记录即拒绝语义解析并提示回到原始层，不猜测系列内容。原文件及固定散列加入语料。该包的主DB1和xslib.db1现各有一条有限语义路径，但缺少独立编号/图纸/组件归属真值，故不从同目录文件推导跨文件对象关联；详见[DB1 8.37边界](DB1_837.zh-CN.md)。

新增真实旧工程空快照边界：Cattel Shed uk 的8.30主DB2无GUID、weirwelding.16 的8.74主DB2有GUID；两者均恰有10张与8.37相同的空表签名（包含表22的80字节行宽声明）。解析器只对该精确签名且所有表都无记录的8.30/8.37/8.74文件返回空语义。8.37和8.74的xslib.db2则是只有GUID头、没有表的组件库快照；这两种真实边界也会返回空系列。工程级回归另外验证 Cattel Shed uk 的单个主 DB2、weirwelding.16 的主库与xslib两份 DB2 都进入 `Project::numbering` 并标为 `PartialSemantic`；主 DB1/同名 DB2 唯一文件配对各建立一条。合成非空变异会拒绝，不能把这些空文件当作已恢复编号系列语义。四份真实文件以原路径和散列固定在语料清单中。8.30/8.37/8.74非空 DB2、对象分配及比较快照仍未解析。

9.08另有两份官方工程历史快照，主DB2表22的80字节系列和空组件库已恢复。独立日志核对4,936个对象与三项计数，包含起始号1001；9月两条历史身份已不在模型中，保留差异，详见[9.08编号](NUMBERING_908.zh-CN.md)。

7.30使用精确的12字节`Xsteel  7.30`头，无GUID；后续仍为带表ID、行数、尺寸、记录标签和表尾的顺序表。主库有9表，表22为76字节系列记录，字段偏移与已验证的7.82一致。组件编号库可以只有12字节头，不应误报截断。未知旧头与7.30错误系列尺寸仍拒绝。

公开来源为[btscm教学工程目录](https://btscm.fr/dicocm/R/realisations/Lapechelire/Modele%20TEKLA/pechelirejulV1/pechelirejulV1/)，模型元数据Tekla17.0。固定下载7份文件及SHA-256，新增10项回归。`numbering.history`和`numberingresults`中的历史最大分配号与20个系列的21个非零计数逐项一致；全部起始号为1，非1起始号另用合成输入验证读取。历史日志不能证明当前对象仍存在或编号仍有效。系列字符串保留原字节，法国环境中的编码不擅自转成UTF-8。7.30逐对象内联编号现已恢复，1,467个对象与日志吻合；默认不自动认定无GUID的文件配对，显式确认来源后可关联DB2系列，见[内联编号证据](INLINE_NUMBERING_730.zh-CN.md)。

7.82及较新版本的头为 `Xsteel`、字节 `0x80`、空格及 ASCII 版本。7.82 样本头后没有数据库 GUID；较新样本继续保存空格及 36 字符 GUID。空 xslib.db2 仅有文件头。

每表依次为 little-endian `u32 tableId, count, payloadSize`，随后是 `count` 行的 `tag + payload`，最后为 `4f 61 bc 00`。因此 magic 是表尾，不是第一张表的开始。表 ID 可以超过一千万，按 `ordinal <= 1000` 扫描会漏表。新入口严格顺序读取、检查行长度/表尾/重复 ID，并保留所有表。

本轮样本观察到行标签 4 和 5（7.82 的 EXCEL-1246A 编号系列）；标签原样保留，不凭低位推断删除状态。

系列表 ID 为 22，7.30/7.82 的 payload 为 76 字节，其余已验证版本为 80 字节：

| payload 偏移 | 当前恢复 |
|---|---|
| 0..51 | 系列键，例如 `P/100`；最后一个 `/` 分隔前缀与起始编号 |
| 52..55 | 零件序列计数 |
| 56..59 | 装配序列计数 |
| 60..末尾 | 未命名整数，原样公开为 additionalFields |

独立验证：培训模型日志记录零件 `P/100` 的最大分配号为 112，DB2 计数为 13；空前缀 `/1001` 的最大号为 1018，计数为 18；装配 `C/1` 最大号为 8，计数为 8。总共 10 个非零计数均满足 `start + counter - 1 == 日志最大号`。这是该样本的交叉证据，不代表计数在删除、重编号或所有配置下等于当前对象数或当前最大号。解析器只报告保存值。

7.30内联编号、现代及7.82 DB1中的零件/装配编号记录及其对象引用现已恢复，并可匹配DB2系列，详见[对象编号证据](OBJECT_NUMBERING.zh-CN.md)。尚未恢复：DB2比较快照、8.44逐对象位置号、钢筋分配编号、有效状态及其余计数/标志含义。不可从系列计数猜测对象编号。

## DB6 分析容器

分析预设 `.admodel` 现有独立API和工程入口，恢复原生Windows-1252文件的125个具名标量及3条设计表设置；保留原文、顺序、重复键和路径，不因ModelName/引擎一致就配对DB6，见[分析预设](ANALYSIS_SETTINGS.zh-CN.md)。这是文本设置证据，不能用来直接命名DB6二进制偏移。

## DB5 分析结果库

`analysis_results.db5` 已在工程根目录和 `Analysis` 子目录中列为 `AnalysisResults`，目前只做文件发现，不解析DB5内容或生成跨文件关联；现有语料及只读样本目录没有原生 DB5。Trimble官方[分析结果说明](https://support.tekla.com/doc/tekla-structures/2025/ana_saving_analysis_results)指出它保存各荷载组合结果；[杆件分段数](https://support.tekla.com/doc/tekla-structures/2025/xs_ad_member_result_division_count)与[位移分段数](https://support.tekla.com/doc/tekla-structures/2025/xs_ad_member_result_disp_division_count)控制杆件结果采样，[最小间距](https://support.tekla.com/doc/tekla-structures/2025/xs_ad_member_result_min_distance)限制杆件结果点间距，而[网格尺寸](https://support.tekla.com/dist/sxf/document/TS_ADV_2025_en_Advanced_options.pdf)控制板、楼板及面板结果网格；最小间距和网格尺寸以毫米计。后续样本应包含非空结果及不同组合/构件类别，并与DB6或分析结果导出核对；未取得前，DB5表布局、数值字段和 DB6/DB1 对象映射仍全属缺口。DB3规划库也尚无固定样本和语义解析。

## DB3 规划数据库

`readProject` 现将工程根目录及 `Analysis` 子目录中的 `.db3` 文件识别为 `PlanDatabase/Discovered`，并给出“语义解析尚未实现”的诊断；不尝试按 DB1、DB2、DB5 或 DB6 布局读取，也不自动建立对象关联。此项仅是格式清单和安全边界，**不是 DB3 内容解析**。本地固定语料与只读样本目录没有原生 DB3；下一步需要可公开复取的真实文件、独立字段真值，以及与计划数据导出/API和配套 DB1 的身份对照，才能建立可验证的解析器。

## DG 8.95 / 9.08 新增证据

配套 `.dg.metadata` 的22个字段现有独立接口，工程入口保存实际来源路径并校验与DG的文件名、图幅、主体类型及已建立的DB1身份关联；不会通过XML绕过DG工程范围，也不推断图纸最新版本。五份XML有独立ElementTree对照，见[图纸版本元数据](DRAWING_METADATA.zh-CN.md)。

四张8.95顺序图纸的目录数量46曾被误当作版本尾部句点，现已按固定版本字段修复；一张9.08使用带可变记录尾部的分节布局。五张均有独立XML图幅/类型对照，9.08主体ID3040545与配套DB1和XML的GUID/编号M/92吻合。工程入口只对已验证9.08的相同工程GUID与存储版本配对数字ID，旧工程图纸保留而不强配。视图、尺寸与完整图元仍未恢复，见[字段与证据](DG_EXAM.zh-CN.md)。

## DG 7.64 有限语义

7.64 DG 严格校验47张表的签名，并解码文本、属性/链接、图幅、主体、视图坐标基/范围、数字模型引用及共同布局的 subtype-1 尺寸锚点/尺寸集；图元和纸面标注精度仍未验证。可变前导区长度为 `40 + 28 × count`，分节记录尾部为8字节。10张同工程真实图纸有137条尺寸、71个集合的结构回归，不能视为10个独立工程或独立出图真值。无工程GUID范围的数字身份不会自动关联到 DB1。完整证据见 [DG 7.64](DG_764.zh-CN.md)。

## DG 7.82 分节表

293 份文件来自 [PSDBIM/EXCEL-1246A-BLDG-B](https://github.com/PSDBIM/EXCEL-1246A-BLDG-B)，固定提交、URL、压缩大小和 SHA-256 见 `tests/corpus.json`，数据在独立 `legacy-dg782` 目录中下载，不与现代培训图纸混淆。总压缩大小 81,229,750 字节。这些文件可能包含不同修订、复制和历史记录，不把其数量称为独立设计项目数。

头为 `Xsteel! 7.82`，76 字节前导整体保留。偏移 16 有 `bc c0 ce db`，不能据此把后续当成旧 DB1 定长表。偏移 76 起，47 张表分别使用 `66 c0 ce db`、payload 尺寸、字段数和字段描述符。记录为 `tag + payload + 40 字节元数据`，表尾为一个零字节。元数据原样放入 `RawRecord::allocatorMetadata`，其内部对象标识/生命周期含义尚未命名。解析器按记录步长严格前进，不搜索文本或元数据内的 magic；所有文件均从公开的前导、表头、记录和尾标记逐字节重建，与解压原文完全相同。

首表 payload 为 4，保存类型目录。类型项可能重复，最大代表样本有 460 项，但物理实体表仍为 46 张；按首次出现的类型逆序对应物理表，不丢弃重复的原始目录记录。所有文件的物理表尺寸、字段数和签名一致；字段描述符首项 1，其余为 0。完整签名列在 `RelatedFormats.cpp` 的独立 7.82 映射中，禁止套用 9.54。

已恢复的 payload 字段：

| 类型 | 尺寸 | 字段与差异 |
|---|---:|---|
| 293 | 45 | ID@0、后继@4、文本@16，28 字节 |
| 295 / 297 | 12 | 属性关系 ID@0、属性 ID@4、owner/context@8 |
| 296 | 37 | 数值属性 ID@0、double@8、名称@16，21 字节 |
| 298 | 110 | 字符串属性 ID@0、类型@4、名称@8、值@29 |
| 266 | 52 | 图幅 ID@4、宽/高 double@16/24 |
| 269 | 4888 | 主体记录 ID@4、类型@12、模型数字 ID@16；类型 1/2/3 对应零件/装配/总布置图 |
| 260 | 3328 | 视图 ID@4、context@8；坐标系端点块@40/160、范围/深度块@112/232 |
| 256 | 1400 | 直线尺寸记录 ID@4、context@8、尺寸集 ID@12、子类型@16；已验证 subtype 0/1 的锚点字段沿用 7.30/9.54 对应布局，8.44仅观测到 subtype 1 |
| 257 | 136 | 直线尺寸集记录 ID@4、context@8；尺寸记录通过尺寸集 ID 与 context 双重核对 |
| 322 | 112 | 引用记录 ID@0、context@4、模型数字 ID@8；这里不是现代 GUID |

跨文件证据：5 个零件图与 54 个装配图的主体 ID 均存在于同仓库 DB1，且对象类型相符；234 个总布置图不假造单一模型主体。1,396 条 type-322 引用中，1,361 条指向 DB1 零件，另外 35 条指向 DB1 装配；此前把“非零件引用”当成未匹配，实际 ID 与装配身份完全相符。数字 ID 不包含工程范围，默认的 `readProject` 不据此关联任意 DB1，也不从文件名或所在目录伪造 `projectGuid` / `storedFileName`。type-309 另含数字零件 ID：7.82 的293张图纸中759行均命中配套DB1实际零件；同版明确授权范围下，这些行加入模型关联列表，记录ID@0，context未知按0保留。

`ProjectOptions::trustLegacyDrawingObjectIds` 提供显式工程范围：只有调用者将同一 `readProject` 根目录下的 7.30/7.82 图纸与同版本 DB1 视为同一工程、且双方均无 GUID 时，才按数字主体/引用 ID 建立关联。主体仍检查零件/装配类型；总布置图不关联单一对象；未匹配 ID 保留诊断。7.82真实工程启用后恢复59条主体关联、2,155条模型引用关联（1,396条type-322加759条type-309），其中35条type-322指向装配；7.30固定工程中两张零件图都指向DB1对象311724。默认不开启。每条主体和引用关联标记 `DrawingPairEvidence::ExplicitLegacyNumericScope`，GUID关联保留默认 `ProjectGuidAndObjectIdentity`。该选项不能越过版本差异或已有GUID；启用后也不能证明历史图纸与当前模型状态一致。

全语料读取结果：50,514 条根文本、2,458 条属性、3,871 条属性关联、293 个图幅、2,688 个有效坐标视图、50 个退化视图记录。正交基由存储的原点与轴端点相减得到；范围块按与现代布局对应的六值结构读取。未取得与旧 DG 完全一致的独立纸面/几何导出；同仓库 46 份 `.vi` 设置未找到六字段整体匹配，因此不把这些设置用作已验证真值。

尺寸扩展：293 份真实 DG 共含 22,844 条 type-256 记录，subtype 0/1 的 1,400 字节布局分别与已有 7.30/9.54 锚点布局一致。成功恢复 22,782 条唯一 ID 的端点、方向和已存偏移；其中 22,657 条与唯一尺寸集按 ID/context 相连。14 组重复尺寸集 ID 的 28 条记录、31 组重复尺寸 ID 的 62 条记录不选取“当前”版本；125 条已解码尺寸没有唯一尺寸集，几何锚点仍保留而不伪造链关联。投影长度仅在单位 XY 方向、端点共面时计算。该公开工程没有独立 7.82 出图真值，因此跨版本一致布局和内部引用一致性不能证明绘制结果精确。

必须处理的局部缺口：

- 73 份图纸各有一条断开的文本链。`DrawingString::complete=false`，`missingContinuationId` 保存缺块 ID，`text` 只含可读取的前缀；不能当成完整 XML。循环引用仍拒绝，避免错误遍历。
- 视图 context 可以重复。`viewsByRecordId` 保存全部成功解码的视图；兼容接口 `viewsByContext` 仅保存无歧义项，现有样本共 2,520 项。不得用 context 数量当作视图总数。
- 39 份图纸中的 50 个视图，两组坐标端点分别都退化为同一个点。原始记录和 `unhandledViewRecordIds` 保留这些 ID，不构造伪坐标基。
- 属性集冲突时，`storedPropertySetNames` 保留候选，`propertySetName` 留空并诊断；不选择“最新”或“默认”。非有限坐标、非退化但无效的基、坏签名或截断记录仍会明确失败。

纸面位置、比例/缩短、尺寸格式化/样式、标记/其他图元、旧元数据身份和生命周期仍未恢复；此版本为部分语义，不等于完整重绘。

## DG 9.54 顺序表

头为 `Xsteel  9.54`。后续每表是 little-endian `u32 count, payloadSize` 和 `count` 行的 `tag + payload`，没有 DB1 section magic，也没有 DB2 表尾。第一表保存 47 个类型 ID；顺序是后续 47 张表的倒序。解析器核对整个文件边界、目录 ID 唯一性及版本对应的全部类型/尺寸签名。

已命名的 payload 字段：

| 类型 | payload 尺寸 | 当前恢复 |
|---|---:|---|
| 293 | 45 | ID@0、后继块 ID@4、文本@16（28 字节）；连链恢复根字符串 |
| 298 | 110 | ID@0、字符串属性类型@4、名称@8（21 字节）、值@29（81 字节） |
| 296 | 37 | ID@0、数值@8（double）、名称@16（21 字节） |
| 295 / 297 | 12 | 记录 ID@0、属性 ID@4、所有者 ID@8；分别对应数值/字符串属性 |
| 266 | 52 | 类型@0、ID@4、图幅宽@16/高@24（double） |
| 322 | 144 | 记录 ID@0、图纸上下文 ID@4、模型 GUID@8（16 字节，文本顺序） |

7 份 DG 的 `grProjectGuid` 均与同目录 DB1 的数据库 GUID 相符；`grFileName` 与原文件名相符。读取改名后的 DG 仍返回其内部属性，不从路径合成这些值。所有 28 条 type-322 GUID 均对应主库 identity，其中既有构件，也有其他模型类型，因此 API 命名为模型对象引用。

字符串检查缺块、重复 ID 和循环引用；仅合成根字符串以免把每个 XML 后缀重复输出。完整且根节点为 `Mark` 的 XML 另解析为 `DrawingString::markTemplate`：节点名、解码后的属性、文本和子节点可查询，原文仍保存在 `DrawingString::text`。`Element` 中的 `PART_POS`、`PROFILE` 等值只是模板字段，不会求值、绑定到模型对象或推断文字最终排版。畸形/不支持的 Mark XML 保留原文并记诊断。C/2 样本的 `PART_POS=1001/1018` 与独立论文打印页上对应标注一致；这只验证两个可见字段，不代表全套标记渲染语义。

尚未恢复：其他引用类型、图纸头全部字段、纸面定位、比例与缩短、尺寸链、符号、文字布局、投影边与遮挡等完整绘制语义。图幅、GUID、坐标基与字符串读取通过不能证明图纸能准确重绘。接下来需补充独立模型、更多版本和图纸输出真值。

### DG type-304 通用记录索引

固定语料中的 506 份 DG 覆盖 7.13、7.30、7.64、7.82、8.44、8.95、9.08 和 9.54；这些样本的 type-304 payload 均为 24 字节。解析器保留前三个 little-endian u32 和末尾 12 字节：前两项在已核对记录中指向目标表类型码及目标记录 ID，可用于查找候选记录；这是一份通用索引，不会自动把目标解释为图元。第三项仅对部分已知类型等于目标记录 payload 的偏移 8，在 type-311 等表不成立，所以 API 只称 `relatedId` 并保留原值。末尾 12 字节尚未命名。索引项缺少目标或目标表布局未知时仍保留；调用者需自行检查候选目标，不能据此认定 owner、context 或 DB1 对象关系。全语料和安装消费者回归见路线图最新记录。

### DG 视图与图纸主体

本轮 7 份 DG 中只有 4 份有视图，共 19 条 type-260；另 3 份仅有零类型占位头，不能把它们当成已覆盖完整图元的图纸。type-269 的非占位头共 4 条，主体 GUID 对应 1 个 DB1 钢板和 3 个装配引用（其中两个图纸主体相同）。类型码 1/2 分别据此命名 SinglePart/Assembly，其他码保留 Unknown。

| 类型 | 尺寸 | 本轮恢复字段 |
|---|---:|---|
| 269 | 6196 | 类型标记@0，记录 ID@4，图纸类别码@12，主体 GUID@16 |
| 260 | 4788 | 类型标记@0，记录 ID@4，context@8，模型主体 GUID@24 |
| 260 | 同上 | 视图坐标基的三个点@48/72/96；显示坐标基的三个点@168/192/216 |
| 260 | 同上 | 第一范围/深度组@120；第二存储属性组@240；均为 6 个 double |

每个坐标基保存原点、X 方向端点、Y 方向端点。API 将后两点减去原点得到轴向量，并由叉积计算 Z 轴。19 条视图的两组基均为单位正交基；一个 3D 视图的显示基发生旋转而视图基不变。另以 DB1 钢板的已恢复轴向核对其视图 X 轴。这些是跨表/跨文件证据，尚无 Tekla API 导出的完整坐标变换真值。

[官方 View 构造函数](https://developer.tekla.com/doc/tekla-structures/2025/view-constructor-container-view-coordinate-system-coordinate-system-array-list-50438)区分范围坐标系与显示坐标系，为字段命名提供语义背景；官方资料不提供上述二进制偏移。模型到纸面的最终映射还涉及定位、缩短、比例等，当前不提供猜测的完整投影函数。

范围组依次为 minX/maxX/minY/maxY/depthNegative/depthPositive；保留有符号深度，不附加未经证实的正值要求。同包固定散列的 `attributes/new_FAB-PLATE_Without_Dimensions.vi` 对应图纸 D2ba7aa95 的 context 12，其 6 个值与第二存储组全部相符。第二组与第一组在旧视图上可能不同甚至平面范围为零，因此公开为 storedAttributeVolume，不当成自动尺寸调整后的有效裁剪范围。

视图属性集名由 `gr_cl_view_prop` 的 property link 按 context 连接。28 条 type-322 引用的 context 均对应某个视图；Project 的 drawingModelAssociations 现保留该 context。图纸主体单独置于 drawingSubjectAssociations，只有工程 GUID 相同、主体 GUID 唯一且已知主体类型与 DB1 类型相符才建立关联。未解析的 context/主体不会凭文件名猜测。

## DBV 环境与选项

31 份 environment.db 覆盖 9 表与 15 表签名；62 份 options_model.db/options_drawings.db 共享 9 表签名。具名容器分别为 Environment、EnvModelOptions、EnvDrawingOptions；6 份早期 DBV 无名称。名称长度不是版本。语义入口核对容器头、全部字段描述符和记录尺寸，未知布局仍可尝试原始接口。

DBV 表沿用 `66 c0 ce db` 节头，记录为 `tag + payload + 8 字节 allocator`，本轮全部表以单字节零终止，包括最后一表。真实记录标签存在 1、4；兼容原始层已有的 12。此前只接受 4/12 导致部分非空表成为 opaque，本轮修复并更新 90 个 raw 回归基线；完整文件字节指纹保持不变。标签不解释为删除状态。

环境表（以下偏移均相对 payload，表序号从 0 开始）：

| 表 | 尺寸 | 恢复字段 |
|---|---|---|
| 0 | 25 | 类别 ID@0，名称@4（21 字节） |
| 1 | 12 | 关联 ID@0，类别 ID@4，属性定义 ID@8 |
| 2/3/4 | 72/84/144 | 整数/浮点/字符串定义，ID@0、名称@4（21）、标签@25（31）、末尾 4 字节元数据 ID |
| 9/10/11 | 104/116/172 | 扩展定义，同前但标签宽 61 字节 |
| 5 | 32 | 元数据 ID@0，其后 7 个未命名 u32 |
| 6/12 | 47/77 | 整数选项 ID@0、定义 ID@4、序号@8、整数值@12、标签@16（31/61） |
| 7/8/13/14 | 55/124/85/154 | 当前语料均空；遇到非空记录保留 raw 并诊断，不推测浮点/字符串选项布局 |

整数定义的三个数值槽从 56（旧）或 88（扩展）开始，步长 4；浮点步长 8，均要求有限值。首槽作为 storedValue，其余保留为 additionalNumericFields，不命名为最大值/最小值或当前值。字符串定义没有非空真实证据，storedValue 留空（nullopt），原始载荷保留，避免把猜测偏移当成已恢复字段。

30 份环境库共恢复 1,255 个类别、8,761 个定义、65,068 条类别关联和 7,631 个整数选项。每条类别/定义/元数据/整数选项引用均在其库内核对，无悬空引用。标签如 `j_Locked` 是原始消息键，库不进行翻译或猜测代码页。

语义背景由 [Trimble 的 environment.db 说明](https://support.tekla.com/doc/tekla-structures/2022/sys_environment_database_file)和 [objects.inp 属性说明](https://support.tekla.com/doc/tekla-structures/2025/sys_objects_inp_properties)支撑；后者的 OBJECT_LOCKED 示例给出名称、标签及空/No/Yes 的次序，回归用例与培训模型核对该部分。二进制偏移是样本推导，官方文档不证明这些偏移，也未证明元数据标志的对应关系。培训模型还含 Organization 选项，不能把官方简化示例视为完整枚举真值。

选项库前四表分别为 bool/int32/double/string，尺寸 86/98/114/2130：ID@0、名称@4（64 字节）、flags@68，两个槽从 72 开始，步长为 1/4/8/1024。其余五表（80/88/332/72/12）本轮均为空，非空时仅保留 raw 和诊断。bool 只接受 0/1，double 拒绝非有限值。60 份库共恢复 46,027 条选项，其中 10,105 条的两个槽不同，不能省略第二槽。

早期 EXCEL-1246A 样本的 XS_DRAWING_CHANGE_HIGHLIGHT_COLOR 同时有整数和字符串记录；API 以记录 ID 保存两者并诊断，不按键名静默覆盖。相同存储类型下的重复键、重复 ID 则拒绝。flags 与两个槽的当前/默认含义、环境优先级尚未通过受控差分证实。

新增模型目录`options.ini`文本入口与逐值关联：保留有序赋值、原始字节、空值、重复键和未知行，按DBV存储类型分别比较两个槽；不选择有效值或执行初始化命令。固定29份配套文本的56条赋值，5种键/值证据，其中大量文件复用同一模板。该证据支持所列字面值的对应关系，不能概括flags和运行时优先级；接口与完整限制见[文本选项证据](OPTION_SETTINGS.zh-CN.md)。

工程入口公开 environment、optionsDatabases，并通过精确名称与存储类型生成 attributeDefinitionAssociations；培训模型有 118 条匹配。关联不表示已判断对象类别适用性，不填补缺失值，不把环境定义中的值写入模型对象。可通过 readEnvironment/readOptions 单独关闭语义读取；rawCompanions 继续由 readRawCompanions 控制。

9.54直线尺寸新增具名端点、方向、已存偏移、视图与尺寸链关联；144条记录及57条链已有回归，另用同作者论文核对19个图上尺寸值，其中新增六项来自C/2正视图。7.30另恢复313条子类型0尺寸及187条链，以同工程DWG的五条尺寸线精确几何核对；28条其他子类型仍保留原始记录，见[旧版尺寸证据](DRAWING_DIMENSIONS_730.zh-CN.md)。7.82布局、样式、格式化标注和完整重绘仍未恢复。比例候选字段存在正倒数差异，详见[尺寸与纸面边界](DRAWING_DIMENSIONS.zh-CN.md)。
# IDRM 容器

工程根目录的 `db.idrm` 和 `xslib.idrm` 现可通过独立 `parseIdRangeMapperFile` 读取。解析器识别 `IdRangeMapper` 标记、固定 magic、记录数和 28 字节记录边界；静态范围字段已核对，记录及不明尾随字节保留。项目入口仅在 namespace GUID 字节与主库/xslib allocation preamble 唯一匹配时关联对应 DB1 文件。W3 工程分别验证主库和组件库配对；AUVENT 两段 namespace 在当前主 DB1 匹配、在旧备份不匹配。该关联不转换静态/运行时 ID，也不把范围值当作对象 ID。字段边界与限制见[IDRM说明](IDRM.zh-CN.md)。

## 新增公开旧工程：DB1 8.30 / 8.74 与早期 DBV 边界

GitHub 仓库 [devsachinborse/tekla-structures-projects](https://github.com/devsachinborse/tekla-structures-projects) 的固定提交 `ba2b921eba863163897d1389286ed4163bdd4412` 提供 `Cattel Shed uk` 与 `weirwelding.16` 两个公开工程目录。Cattel 当前 DB1 是 8.30（275 节、119,998 条记录）；weirwelding 当前 DB1 是 8.74（307 节、295,988 条记录），其 xslib 是 8.74（271 节、1 条记录）。另发现同工程 `weirwelding.16.db1.bak` 为 8.30 旧快照，275 节、500,837 条记录、1 个 opaque 节；它的有限模型投影读取6,046个部件、2,908个可操作部件、2,306个装配、1,075个组件、612条焊缝和13,779个属性。该备份与8.74当前库的这些计数相同而模型指纹不同，只能作为大规模真实存储回归，不能据计数建立跨版本对象映射。Cattel 8.30 主库有限投影读取1,772个部件、1,115个装配、339个组件、1,309条焊缝和3,881个属性；对象分类、所有权、编号和螺栓含义还没有独立真值，不能将这些计数视作Tekla对象清单。8.74 主库的共同核心表签名与8.95样本一致，但 identity-class 表位于表114且多4字节；其类别引用均能在该表找到。身份类别、对象所有者和被解析实体之间的闭合检查通过真实8.74主库。螺栓、钢筋、编号、型材语义和未映射表仍保留在 raw 层；两个主库的表27均为 opaque 节，未猜测布局。8.74 xslib 尚无语义投影；工程入口现会在语义解析不支持但容器有效时把它保留为 `ComponentLibrary/Raw`，该样本的271表、1条记录及1个opaque section均可读取，不建立组件对象或对象关联。固定语料直接调用 `parseComponentLibrary` 仍明确报告该 schema 不支持。样本按固定提交、字节数和 SHA-256 加入可选公开语料，第三方二进制不会提交进源码库。

用 `python -B tools/test_db1_874_evidence.py --corpus <语料目录>` 独立重读8.74原始表：表114的30个类别、表293的39,553个身份和表274的8,954个部件引用闭合；部件身份类别计数为kind 2的6,046条、kind 11的2,680条和kind 38的228条。表191/192中六类有身份端点的关系共5,674条，关系类型4/9/11/12/13/38分别为204/1,217/2,680/121/1,224/228，端点类别码全部相符。解析器现只对8.74执行这项关系-类别冲突拒绝检查。它是固定样本内的交叉表一致性，不是独立Tekla对象清单或导出真值；其他版本不外推。

同工程 8.74 的 DB2 可由 raw 入口读取为空编号快照；`options_model.db` 和 `options_drawings.db` 能通过现有 DBV 选项语义入口。配套 `environment.db` 的 GZIP 解压头以 little-endian 整数 2 起始，后续 15 张表的宽度和字段描述符与已知环境签名一致。raw 解析现只在其余固定头字也吻合时识别为 `DBV-legacy-v2`，并按固定行长保留记录标签 1/4/5/6 和零字节表尾；没有将偏移 0 的 2 命名为 Tekla 产品版本。环境语义入口恢复42个类别、830个属性、39组元数据、4,633条类别链接与858条整数选项记录。相同属性/索引的选项可能以不同 row tag 重复，`AttributeChoice::allocationTag` 保留原标签并使这些记录可区分；目前不解释标签5/6、旧值生命周期、元数据flags和字符串定义值，不能将它们当作唯一有效值。固定语料及合成边界回归覆盖此路径。来源仓库未声明许可证，因此语料只保存公开 URL/哈希并在显式运行时下载，不把第三方二进制提交进仓库。


### Tekla 8.44 DB2 空编号快照

Abri-Moto-VCCF25 工程附带一个 gzip `Nouveau modèle.db2`。解压后头部为 `Xsteel\x80 8.44`，携带 GUID `5f4eda13-411b-43a0-b3aa-9572a944571d`，并有十张表，表序号/行宽严格符合已验证的十表空快照签名；每张表记录数均为零。相邻 `Nouveau modèle.db1` 的 `databaseGuid` 与该 DB2 GUID 完全相等，二进制内容固定在 `tests/corpus.json`。因此编号读取器现将 8.44 加入**精确签名的空快照**白名单；不据此推断非空 8.44 系列格式，也不恢复对象编号分配、快照比较或有效状态。`numbering_844_evidence` 回归同时检查空表签名及 DB1/DB2 GUID 相等。`readProject` 在选择主模型之外也会检查同目录其他同名 DB1/DB2：只在 DB1 raw 头部 GUID 与 DB2 GUID 精确相等时建立 `secondary DB1/DB2 basename and database GUID match` 关联，不把次级模型折叠进主模型，也不因其 GUID 不同于主模型而误报冲突。

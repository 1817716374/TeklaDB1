# 构件位置设置与已存几何

现代 DB1 的构件 payload@8 引用位置表（主库245 / xslib215）。表宽52字节、14个描述符，前两项为1，其余为0。`Part::auxiliaryReferenceId` 现在可索引 `Model::partPositions`，该表同时保留暂未被构件引用的记录。

| payload偏移 | API字段 | 存储类型与含义 |
|---|---|---|
| 0 | id | u32，共享记录ID |
| 4 | startAxialOffset | float，起点沿参考轴的偏移 |
| 16 | endAxialOffset | float，终点沿参考轴的偏移 |
| 28 / 32 | depthCode / depthOffset | u32 / float，深度位置代码及偏移 |
| 44 / 48 | planeCode / planeOffset | u32 / float，平面位置代码及偏移 |
| 8,12,20,24,36,40 | rawFields[0..5] | 六个未解释的原始u32，按此顺序保留 |

56份8.95/9.52/9.60主库和组件库中共776条记录，两个位置代码均只出现0/1/2。API保留存储代码；未知代码也保留并产生诊断，不猜成默认位置。四个已命名浮点数必须有限。重复ID或非零构件引用缺失会拒绝解析，失败时不返回部分模型。

## 独立源码与几何一致性证据

[AUTRA固定提交的建模源码](https://github.com/MohamedHasan94/AUTRA/blob/8b96f8b30a0b132ea22fe4d19c96842c9b317063/TeklaAPI/ExtensionsLibrary/Model.cs)中，CreateBeam设置平面为MIDDLE并接收深度参数；[调用入口](https://github.com/MohamedHasan94/AUTRA/blob/8b96f8b30a0b132ea22fe4d19c96842c9b317063/TeklaAPI/AUTRATekla.cs)为主、次梁传入BEHIND。配套ITIFinal02_35主库中30根具名梁全部为depthCode=2、planeCode=0；36个柱和基础为0/0，与对应创建方法的MIDDLE设置一致。两份源码按固定提交和SHA-256下载，`position_source_evidence`核对这66个对象。此证据不证明运行该源码的完整历史，也不证明后续没有编辑。

官方[深度枚举](https://developer.tekla.com/doc/tekla-structures/2025/position-depth-enum-enumeration-53248)和[平面枚举](https://developer.tekla.com/doc/tekla-structures/2025/position-plane-enum-enumeration-53249)说明API概念；文档本身不是数据库偏移证据。数据库中两个槽位的顺序不能根据界面顺序猜测。尤其@36/@40的776个样本全为零，不能因此称为旋转枚举/角度。

另以15,940条无轮廓引用构件检查轴向投影和长度，其中405条含非零轴向偏移。几何原点的轴向位移与startAxialOffset一致，参考线长度加endAxialOffset减startAxialOffset与已存长度近似一致。两个renato库中的同一对象1126602有约0.0076的长度残差，未据此重算或覆盖已存几何，残差原因仍待验证。这是数据库内部一致性证据，不是独立Tekla几何真值。

例如ITIFinal02_35构件21371的参考线长300、起止轴向偏移-25/+25，已存长度为350，原点X为20175。构件41223的planeOffset约4.3，已存原点Y为4009.3。测试固定这些值，防止把偏移再次应用；原有全部几何指纹保持不变。

## 7.82：定义中的内嵌位置

7.82主库207 / 组件库177表的构件定义宽380字节、31个描述符（第一项为1，其余为0）。位置块在payload@20..67，直接属于定义。`Model::partDefinitionPositions.at(part.definitionId)`提供查询；其中`PartPosition::id`是定义ID，与现代`partPositions`的记录ID属于不同命名空间。7.82的`auxiliaryReferenceId`仍为零，不伪造独立引用。未被构件引用的定义和共享定义均保留。

| 定义payload偏移 | 字段 |
|---|---|
| 20 / 32 | startAxialOffset / endAxialOffset |
| 44 / 48 | depthCode / depthOffset |
| 60 / 64 | planeCode / planeOffset |
| 24,28,36,40,52,56 | rawFields[0..5] |

两个独立工程的四份主库/组件库共有2,722份定义（2,249 / 413 / 60 / 0）。该48字节块与现代位置块对应，代码分布均为0/1/2；深度/平面字段名称来自跨版本布局对应，尚无7.82独立界面或导出真值。轴向字段另有3,326条无轮廓构件的几何一致性证据，其中99条含非零偏移。原点轴向投影按float设置精度核对：例如对象28390投影为-304.8，而单精度设置为-304.79998779296875；验证允许一次float精度误差及1e-6坐标相减误差，不把双精度几何覆盖成单精度值。

更严格的投影检查发现组件库对象29686仍有约1.335e-6的轴向残差，超出上述float精度阈值；其方向为(0,-0.998425644,-0.056091302)，残差原因尚未确认。另有7条零轴向偏移构件的长度差为约0.006至0.071，原因未明。测试固定两类残差的对象ID，既不静默放宽阈值，也不覆盖坐标。四项真实文件回归核对全部位置字段、六个原始位模式、定义连接及几何证据；原有几何基线不变。合成测试覆盖主库/组件库、共享/未引用定义、未知代码、四个已命名浮点字段及未引用定义中的非有限值、重复调用失败清空。

旧版未知槽位存在非零值（包括90/180/360/540等候选角度），但仍不足以确认坐标顺序、旋转枚举或应用规则，继续保留原始位模式。表面处理定义的布局不同，不能套用此位置块。

## 通用使用边界

- `Part::origin`、`length`及方向是已存几何数据。位置设置用于查询与解释，不能直接再平移这些数据。
- 现代记录@8/@12/@20/@24在现有样本中仅有正负零，不能确认横向端部偏移的顺序、坐标空间和作用规则，暂存原始位模式；@36/@40同样未命名。
- 旋转、横向偏移、完整截面定位、构件变形及求解规则尚未恢复；没有实现完整位置重算。
- 7.82使用partDefinitionPositions，现代布局使用partPositions；8.44未经独立验证，不产生这两类位置记录。9.65/9.66沿用既有现代布局入口，本轮没有新增其独立真实语料。

56项`position_model`/`position_library`回归检查所有位置字段、原始位模式及构件引用；统计与指纹只是回归证据，不能把局部位置解码宣称为完整几何逆向。

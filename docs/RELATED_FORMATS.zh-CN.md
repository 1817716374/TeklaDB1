# DB2 / DG / DBV 格式证据与未完成项

目标仍是多格式完整解析，以下是 2026-09-26 恢复的局部语义。`PartialSemantic` 不等于完整实现。

## 样本与可复验依据

- 已固定的 60 份公开 DB2 包括 7.82、8.95、9.52、9.60，30 份主编号库和 30 份空组件编号库。实际编号系列出现在 7.82 和 9.60 样本中，不能把空库通过视为丰富语义覆盖。
- 7 份 DG 均为存储版本 9.54，来自同一培训模型，不能推断其他版本兼容性。
- 培训模型来自 [letstekla 仓库固定提交](https://github.com/letstekla/Tekla-Structures-Drawing-Automation-Through-Grasshopper-in-Rhinoceros-3D/tree/efb24b30a722de4afc9052331e2e24b69abba6cf)。模型及 `numberinghistory.txt` 位于同一 ZIP；下载地址、文件大小及 SHA-256 见 `tests/corpus.json`。
- [Trimble 文件说明](https://support.tekla.com/doc/tekla-structures/2023/sys_files_and_file_extensions)确认 DB2 用于编号、environment.db 用于用户属性定义；这些官方描述不提供下述二进制字段偏移。偏移来自样本分析。

## DB2 顺序表

头为 `Xsteel`、字节 `0x80`、空格及 ASCII 版本。7.82 样本头后没有数据库 GUID；较新样本继续保存空格及 36 字符 GUID。空 xslib.db2 仅有文件头。

每表依次为 little-endian `u32 tableId, count, payloadSize`，随后是 `count` 行的 `tag + payload`，最后为 `4f 61 bc 00`。因此 magic 是表尾，不是第一张表的开始。表 ID 可以超过一千万，按 `ordinal <= 1000` 扫描会漏表。新入口严格顺序读取、检查行长度/表尾/重复 ID，并保留所有表。

本轮样本观察到行标签 4 和 5（7.82 的 EXCEL-1246A 编号系列）；标签原样保留，不凭低位推断删除状态。

系列表 ID 为 22，7.82 的 payload 为 76 字节，其余已验证版本为 80 字节：

| payload 偏移 | 当前恢复 |
|---|---|
| 0..51 | 系列键，例如 `P/100`；最后一个 `/` 分隔前缀与起始编号 |
| 52..55 | 零件序列计数 |
| 56..59 | 装配序列计数 |
| 60..末尾 | 未命名整数，原样公开为 additionalFields |

独立验证：培训模型日志记录零件 `P/100` 的最大分配号为 112，DB2 计数为 13；空前缀 `/1001` 的最大号为 1018，计数为 18；装配 `C/1` 最大号为 8，计数为 8。总共 10 个非零计数均满足 `start + counter - 1 == 日志最大号`。这是该样本的交叉证据，不代表计数在删除、重编号或所有配置下等于当前对象数或当前最大号。解析器只报告保存值。

尚未恢复：比较快照内容、逐对象位置号分配、其余计数/标志含义。不可从系列计数猜测对象编号。

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

字符串检查缺块、重复 ID 和循环引用；仅合成根字符串以免把每个 XML 后缀重复输出。XML 以原文提供，尚未转成语义标记树。

尚未恢复：其他引用类型、图纸头全部字段、视图坐标系与变换、尺寸链、符号、文字布局、投影边与遮挡等完整绘制语义。图幅、GUID 与字符串读取通过不能证明图纸能准确重绘。接下来需补充独立模型、更多版本和图纸输出真值。

## DBV 环境与选项

30 份 environment.db 覆盖 9 表与 15 表签名；60 份 options_model.db/options_drawings.db 共享 9 表签名。具名容器分别为 Environment、EnvModelOptions、EnvDrawingOptions；6 份早期 DBV 无名称。名称长度不是版本。语义入口核对容器头、全部字段描述符和记录尺寸，未知布局仍可尝试原始接口。

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

工程入口公开 environment、optionsDatabases，并通过精确名称与存储类型生成 attributeDefinitionAssociations；培训模型有 118 条匹配。关联不表示已判断对象类别适用性，不填补缺失值，不把环境定义中的值写入模型对象。可通过 readEnvironment/readOptions 单独关闭语义读取；rawCompanions 继续由 readRawCompanions 控制。

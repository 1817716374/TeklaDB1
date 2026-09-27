# DB6 分析数据库：原始容器证据与边界

当前 `parseRawDatabase` 支持一个实测 DB6 布局，返回 `DatabaseKind::Analysis`、`DatabaseLayout::ModernSections` 和 `storageVersion="DB6-10014"`。10014 是文件中观察到的首个整数，**不是已验证的 Tekla 产品版本号**。此接口保留数据供后续逆向；分析构件属性、约束、荷载、求解参数和对象关系尚未解读，没有新增 DB6 语义 API。

官方说明：修改构件的分析属性而尚未创建分析模型时，Tekla 将默认分析属性保存在模型目录的 `AnalysisPartDefaults.db6` 中。这解释了文件用途，不能证明任何二进制偏移的字段含义。见 [Trimble 分析构件属性说明](https://support.tekla.com/doc/tekla-structures/2026/ana_defining_and_modifying_analysis_part_properties)。

## 公开原生样本

来源为 [Construsoft drawing views 教程](https://dl.construsoft.com/files/en/STD_Defining_drawing_views.pdf) 对应的[公开模型 ZIP](https://dl.construsoft.com/install/2024/Models/Defining%20drawing%20views%20steel.zip)。文件只在用户指定的外部语料目录下载，不随库重新分发。

| 对象 | 字节数 | SHA-256 |
|---|---:|---|
| ZIP | 580037 | `a4cf636d45b4ff0dbd2679adbb64ea0ab07c1713cfe631c62a6c940d83025139` |
| `Defining drawing views steel/AnalysisPartDefaults.db6` | 2293 | `160d632251d4c0bd49f4d3d3c416b9d6710dc5046b1c9046fdb635c14319c068` |
| 同包主 DB1 | 184181 | `e4c84217cfb5f18a75112f288fd8e9c4feec94754728c852a97942806cbf9195` |

DB6 为 GZIP，解压后 57113 字节。主 DB1 的存储版本为 9.52，包含22个实际构件、10组螺栓、84条焊缝、2个装配。两个固定文件新增五项语料检查：DB6原始重建、主DB1回归、工程正常读取、关闭原始伴随文件读取及严格模式。

## 容器布局

所有整数按小端读取。前导区完整保留为 `RawDatabase::preamble`，不将其中的 GUID 或数组自动解释成模型身份。

| 位置/顺序 | 实测结构 |
|---|---|
| 0..23 | 六个 u32：10014、1、`0xdbcec0bc`、0、1、13 |
| 24、28 | 上下文数量 C、一个未解释整数 |
| 32起 | C−1 个24字节项：连续编号、16字节GUID数据、4字节未解释值 |
| 注册表后 | u32值5、数组项数N，随后两个各N项的32位数组 |
| 数组后 | 20字节尾部，第一个u32与C一致，其余原样保留 |
| 后续每表 | `0xdbcec066`、payload宽度、描述符数量、0/1描述符数组 |
| 记录 | 标签4、payload、8字节分配器数据 |
| 表尾 | 单字节0 |

第一个表由这些长度计算得出；本样本恰为偏移47004。随后86个表、79条记录恰好走到文件末尾。实现不扫描魔数，不把47004或86写死为通用规则。payload及分配器中出现相同魔数不会生成假表。其他标签及头部变体明确拒绝，不从DB1/DG借用未经验证的规则。

解析检查头部、数量溢出、数组边界、描述符、记录长度及终止符；失败清空输出。读取预算沿用 `RawDatabaseOptions::maxDecodedBytes`，对普通和GZIP输入均有效。可选 `retainDecompressedFileImage` 保存完整解压数据；关闭时仍保留全部可重建的原始字段。

**完整性限制：**目前没有识别全局表数量或完整性校验字段。如果文件恰好在某个完整表之后被截短，仅凭此原始布局无法区分“较小的有效模式”和“缺失后续整表”。固定真实样本通过哈希及86表/79记录基线检测此变化；通用接口不冒称能够检测所有损坏。

## 工程入口与关联

`readProject` 发现模型根目录及 `Analysis` 直接子目录中的 `.db6`；目录名和扩展名按大小写不敏感识别。默认由 `readRawCompanions` 控制读取，成功状态为 `ReadLevel::Raw`，结果在 `rawCompanions` 中按实际路径保存。关闭读取则仅为 `Discovered`。损坏或不支持的DB6为 `Failed`；`strictCompanions=true` 时工程读取失败。

同包DB1与DB6共享部分分配上下文GUID，但DB6观察到的记录ID和候选物理对象ID未匹配此DB1当前对象。没有恢复 IDRM 编号转换，也没有证据证明保存的默认属性属于当前可见构件。故不根据同目录、文件名或裸数字ID建立模型对象关联，不把上下文GUID当作工程GUID。此处的未匹配观察不推广为所有DB6均与DB1无关。

## 验证

- 六组C++测试：变长前导区、零/多数组项、含魔数payload/分配器、逐字节重建、全部非完整表边界截断、非法头部/计数/描述符/标签/尾部、读取预算及失败输出清空。
- 安装后消费者独立链接公共API并运行重建与前导区变体检查。
- `tools/test_analysis_raw_evidence.py` 用Python独立走读真实GZIP；校验长度、86表/79记录及全文指纹，再由C++结果重建全部字节。28项检查覆盖普通/GZIP输入、改扩展名、嵌套目录、开关/严格模式、14种原生输入损坏及伪装成DB6的DB1。
- 原有语料文件定义和期望保持不变；结构计数和重建是原始格式证据，**不等于分析属性、对象关联或几何独立真值**。

后续仍需更多原生DB6版本、分析属性文本/导出对照、可验证的IDRM映射、DB3规划库及DB5结果库样本。

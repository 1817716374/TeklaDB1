# IDRM ID 范围映射文件

`parseIdRangeMapperFile` 读取 `db.idrm` 和 `xslib.idrm` 的已观察容器结构，可处理普通文件与 gzip 封装。`readProject` 会发现工程根目录下这两个文件，并将解码结果保存在 `Project::idRangeMappers`；每条记录原样保留为 28 字节，文件尾随字节也原样保留并附诊断。工程入口把 IDRM 中的 namespace GUID 与主 DB1/xslib 已解析分配前导区做精确字节匹配，只有唯一目标时才添加 `Project::associations` 文件级关联，并标记为 `PartialSemantic`。

当前已验证结构：

| 偏移 | 字节数 | 当前处理 |
|---:|---:|---|
| 0 | 13 | 精确签名 `IdRangeMapper` |
| 13 | 4 | 保存为兼容字段 `headerValueAt13`，并公开为 `staticIdBoundary`；当前四个固定样本中是静态 ID 范围的排他上界 |
| 17 | 4 | 固定标记 `0xdbcec0e1` |
| 21 | 4 | 记录数；用于边界检查 |
| 25 | 4 | `staticFirstId` |
| 29 | 16 | 原样保存的 `namespaceGuidBytes` |
| 45 | 4 | `namespaceFirstId` |
| 49 | 4 | `idCount`；记录表示半开区间 `[staticFirstId, staticFirstId + idCount)` |

解析器检查签名、固定标记、最小头长和记录数组边界，并遵守解压大小限制。四个固定样本（W3 7.82、AUVENT 8.44）逐字段核对上述范围字段；各段首尾连续，最后一段的排他上界等于 `staticIdBoundary`。这一连续性以 `staticRangesMatchBoundary` 报告，不满足时保留原始记录并给出诊断。尾随字节会被保留并诊断。

真实语料包括 [Construsoft W3 工程归档](https://dl.construsoft.com/files/nl/Wapening.zip) 中的 `db.idrm` / `xslib.idrm`，以及 [AUVENT CCF 工程目录](https://btscm.fr/dicocm/C/CCF/CCF_BE_2019/AUVENT_CCF_BE_AMCR1_2019_final/) 中两份两段记录文件。路径、来源、字节数和 SHA-256 固定在 `tests/corpus.json`。W3 主库和 xslib 的 IDRM namespace GUID 分别在对应 DB1 的 allocation preamble 中精确出现；AUVENT 的两个范围 GUID 在当前主库前导区匹配，而在可取得的旧 DB1 备份中均不匹配。固定回归独立重读原始 DB1 前导区核对这些字节，并验证工程关联目标。

真实 AUVENT 范围 GUID 可与 DB1 分配命名空间头中的 GUID 对应，但这只把静态范围关联到命名空间，不能推出静态 ID 到运行时对象 ID 的转换公式。对 AUVENT 的一张 8.44 DG 尝试做对象级核对时，图纸中的候选数字 ID 及一个未证实的转换候选，都未出现在同工程可取得的六份 DB1 当前/备份快照中。此图纸不能证明转换公式；文件可能是过期或未配对图纸，也可能指向另一类对象，这些情况尚未区分。相关范围核对保存在外部研究工作目录 `IDRM-RESEARCH.md` 和 `output/idrm-auvent-history-snapshot-audit.txt`。仍需同一保存点的图纸与 DB1 运行时身份真值。

当前支持包含静态 ID 范围、唯一 DB1/xslib allocation namespace 文件关联，但尚不是图纸引用的完整映射。静态 ID 到运行时 ID 的转换、图纸引用转换、多个 IDRM 文件之间的选择策略、版本升级历史和生命周期仍未验证。关联依据是 namespace GUID 与已解析 DB1 分配前导区内的 16 字节完全一致；它不把裸 ID 当作对象 ID，也不宣称范围映射公式已恢复。完整研究记录见 `IDRM-RESEARCH.md`（该文件位于研究工作目录，不随仓库发布）。

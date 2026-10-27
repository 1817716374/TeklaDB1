# guid.mapper：历史身份映射

`parseGuidMappingFile` 读取纯文本或gzip封装的 `guid.mapper`。默认解压上限64 MiB，
保留完整原文、顺序、批次头中的模型名称和每条记录的行号。GUID规范化为小写UUID，不含可选的ID前缀。

公开文件包含多批 `!Guid mapping for <模型名称>`，后续每行是一对源GUID和目标GUID。
批次不能折叠成一个丢失历史顺序的全局字典；重复源或目标保留为候选并诊断，不替用户选取一条。
不完整GUID、额外列、缺少批次头、未知控制行、二进制文本及解压超限均报错并清空输出。
模型名称保留原字节；不猜测代码页，也不将名称当路径打开。

`matchGuidMappingTargets` 把每条目标GUID与主模型中唯一身份匹配，返回批次/记录索引和对象ID。
`matchGuidMappingObjects` 接受调用方明确提供的两份模型，只在源、目标GUID都唯一对应时返回两侧对象ID。
空GUID、零GUID和模型中重复的GUID不能提供唯一匹配。两个函数不修改模型，不沿历史链自动传递替换。

`readProject` 默认读取模型根目录中的文件，并提供 `guidMappings` 和 `guidMappingTargets`。
`ProjectOptions::readGuidMappings=false` 关闭读取；`strictCompanions` 控制伴随文件错误是否导致工程失败。
工程不会因为批次头出现另一个文件名而读取外部模型，也不会自动将DG引用改成当前GUID。

## 独立来源与验收

公开教学工程目录：[AUVENT CCF BE 2019](https://btscm.fr/dicocm/C/CCF/CCF_BE_2019/AUVENT_CCF_BE_AMCR1_2019_final/)。
已下载并审计其主库、六份历史备份和映射文件；固定在主语料中的最小证据集是映射文件、当前8.44主库、前一份备份。
文件来源、压缩大小和SHA-256均在 `tests/corpus.json`，文件本体不随库分发。

mapper共6批、41,712条，各批条数为33,899、3,112、1,688、1,007、1,003、1,003。
最后一批的811个源GUID匹配 `AUVENT_CCF_BE_AMCR1_2019.db1.bak` 的全部身份记录，
811个目标GUID匹配 `AUVENT_CCF_BE_AMCR1_2019_final.db1` 的全部身份记录。
两边811个对象的数值ID全部不同；例如源GUID `7998c610-11ca-4333-8d41-1fc4375b943e`、ID254825405
映射到目标GUID `9db7f2eb-37a0-4656-b18a-dd7240715047`、ID509681756。
该对照从两份原始209身份表独立提取，再核对公开API结果；不能用数值ID相等来替代GUID映射。

同名备份可能早于某批映射保存，例如corrige备份仍匹配前一批目标，不能用文件名判断映射已生效。
历史记录数量也不代表当前存活对象数量。单文件回归覆盖全部批次与记录指纹，跨文件回归覆盖811对身份；
`tools/test_guid_mapping_evidence.py` 独立检查批次条数/文本语法，并确认交换方向、清空一个真实目标及删除最后一批均使证据检查失败。

IDRM是另一种二进制范围映射格式，参与静态ID和运行时ID的转换，见[Trimble说明](https://support.tekla.com/article/cannot-load-selected-drawing)；它与本文件的GUID重命名不可混同。
新找到的两段IDRM排除了把末字段当作结束ID的猜测，转换规则仍在验证。
此次身份对照也不证明8.44的全部几何、加工或DG语义已经完成。

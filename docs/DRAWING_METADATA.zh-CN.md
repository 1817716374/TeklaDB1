# 图纸版本元数据与 DG / DB1 关联

`parseDrawingVersionMetadata` 读取 `.dg.metadata` 中的 `DrawingVersionMetadata` XML。`readProject` 会发现工程根目录和 `drawings` 目录中的这些文件，保存每份文档并检查 DG 配对。固定语料现覆盖[Trimble 官方考试工程](https://github.com/TrimbleSolutionsCorporation/TSOpenAPI_Model_Exam/tree/7345e5f14c86bf94f9da073705c56526d6895c6d)的五份 XML，以及同一公开钢结构练习工程中的四份 XML；ZIP成员、大小和 SHA-256 均记录在语料清单。

这是保存的图纸版本信息。解析器不据此选择最新图纸、不推断当前有效状态、不从文件名生成图纸 GUID，也不会用 XML 主对象 GUID 修复 DG 中缺失或错误的对象引用。

## 公开字段

```cpp
#include <tekla/DrawingMetadata.hpp>

tekla::DrawingVersionMetadata metadata;
std::string error;
if (!tekla::parseDrawingVersionMetadata("drawing.dg.metadata", metadata, error)) {
    // error 描述失败；metadata 已清空。
}
```

| XML 字段 | API |
|---|---|
| DrawingGuid / MainObjectGuid | `drawingGuid` / `mainObjectGuid`，验证格式并规范为小写 GUID；空值不补全 |
| Author / Mark / Name / Title1 / Title2 / Title3 | 对应具名字符串；仅去除首尾 XML 空白，实体只解码一次 |
| Width / Height | `optional<double>`，存在时必须为有限正数 |
| DrawingType | `optional<uint32_t>`，保留代码，不自行扩展类型枚举 |
| IssueDate / NextDate / IssueId / Flag / Revision / PlotDate / ModifyDate / CreateDate / PartsNo / UpToDate / Lock / AlwaysNecessary | `storedIntegers` 中按原字段名保存的有符号 64 位整数；保留零值和字段是否存在，不解释时间单位、位标志及状态优先级。`AlwaysNecessary` 只在第二份公开工程的四个 XML 中观察到，四值均为0，含义未证实 |

`rawXml` 保留完整原文，包括未知扩展、原 GUID 大小写和字段是否存在。未知字段留在原文并产生诊断；缺失的数字字段不会变成零。重复已知字段、嵌套伪装的标量、错误数字、无效 UTF-8/实体、未知命名空间及不支持的声明会失败。默认解码大小上限为 16 MiB，调用方可传第四个参数调整。

## 工程入口

`Project::drawingMetadata` 以实际文件路径保存结果。`ProjectOptions::readDrawingMetadata` 单独控制读取，默认开启；关闭后仍列出文件但标为 `Discovered`。`readDrawings=false` 时可以独立读取 XML，但不会创建 DG 配对。成功读取标为 `PartialSemantic`；错误文档按既有 `strictCompanions` 策略处理，宽松模式保留主模型及失败文件诊断。

`drawingMetadataAssociations` 只在以下条件成立时建立：

1. 同一目录中，DG 文件名与 `.metadata` 前面的文件名唯一匹配；大小写歧义不挑选第一项。
2. DG 内保存的文件名与实际 DG 文件名一致。
3. XML 有图纸 GUID、宽、高、类型，DG 有唯一已解读图幅和主体；宽、高、类型逐项相同。
4. XML 主对象 GUID 与已通过 DG 工程范围检查的主体 GUID 一致时才记录 `modelObjectId`。主对象 GUID 为空或不匹配时，文件级配对仍保留，但不创建对象链接并输出诊断。

在 Abri-Moto 9.50 样本中，六份 metadata 的 `DrawingGuid` 均与所选 DB1 的一个唯一 identity GUID 精确匹配；`DrawingMetadataAssociation::drawingGuidIdentityId` 暴露对应的 DB1 identity ID。这个身份记录没有被分类为零件或装配，也不据此称为图纸对象。该关系目前只在有真实证据的 9.50 schema 启用；它不证明 GUID 在 DG 文件内部的含义，也不证明两个文件代表当前最新状态。相同 DrawingGuid 的不同文件仍按路径保留，不做版本去重或优先级选择。文件名、sheet 等配对条件冲突会阻止文件级关联；对象 GUID 冲突只阻止对象级关联并产生诊断，已读取文档及已有 DG / DB1 结果不会被改写。

真实样本恢复九组 XML→DG 配对。官方考试工程的五张图中，四张旧工程总布置图没有主对象关联；9.08 单零件图匹配 DB1 对象 `3040545`，GUID 为 `02f6a87b-e776-431e-a2db-9f8f1865e98c`。钢结构练习工程另有四组带主对象 GUID 的图纸，均通过已有的 DG→DB1 范围关联核对。XML 独立保存的 DrawingGuid（例如 `6b38a239-b7b0-4bb2-ad69-21cebb58a2fa`）与文件名不同，不能混淆。

这四份钢结构工程 XML 额外保存 `AlwaysNecessary`，四个文件中的文本值目前都是 `0`。解析器将其严格按有符号64位整数读入 `storedIntegers`，原始 XML 仍保留空白、字节和字段存在性；不推断该值是布尔量、必需性标记或任何状态语义。其他未知直接子字段仍按原顺序放入 `DrawingVersionMetadata::extensions`，嵌套结构不压平成文本。

## 验证与边界

```sh
python -B tools/test_drawing_metadata_evidence.py --data /path/to/corpus --download --exe /path/to/tekladb1_validate --work /path/to/separate-work
```

Python ElementTree 独立核对九份文档的最多23个具名字段和完整原文字节；`AlwaysNecessary` 只验证存储整数与字节，不赋予业务语义。16 种变异覆盖尺寸/类型、错误或缺失 GUID、保存状态值、缺失/嵌套字段、严格/宽松解析错误、错误 DG 工程 GUID、错误 DG 数字主体、重命名配对和孤立元数据；大小写敏感文件系统还运行第 17 种重复文件名歧义测试。两个工程入口分别验证九组 DG 配对及五条已有 DB1 主体关联；关闭读取的控制路径也有验收。

新增七组正常/异常 CTest、两项安装后消费测试和五项固定语料用例；元数据相关用例共 11 项。新增四份钢结构工程 XML 样本，现有文件定义为 797 份、语料用例为 1,683 项。图纸日期和标志的生命周期解释、`AlwaysNecessary` 及其他扩展的字段语义、更多 XML 版本/命名空间、DG 自身版本身份及完整图元仍是缺口。

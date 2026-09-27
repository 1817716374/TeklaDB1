# 图纸版本元数据与 DG / DB1 关联

`parseDrawingVersionMetadata` 读取 `.dg.metadata` 中的 `DrawingVersionMetadata` XML。`readProject` 会发现工程根目录和 `drawings` 目录中的这些文件，保存每份文档并检查 DG 配对。当前真实证据为[Trimble 官方考试工程固定提交](https://github.com/TrimbleSolutionsCorporation/TSOpenAPI_Model_Exam/tree/7345e5f14c86bf94f9da073705c56526d6895c6d)中的五份 XML，已在语料清单固定 ZIP 成员、大小和 SHA-256。

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
| IssueDate / NextDate / IssueId / Flag / Revision / PlotDate / ModifyDate / CreateDate / PartsNo / UpToDate / Lock | `storedIntegers` 中按原字段名保存的有符号 64 位整数；保留零值和字段是否存在，不解释时间单位、位标志及状态优先级 |

`rawXml` 保留完整原文，包括未知扩展、原 GUID 大小写和字段是否存在。未知字段留在原文并产生诊断；缺失的数字字段不会变成零。重复已知字段、嵌套伪装的标量、错误数字、无效 UTF-8/实体、未知命名空间及不支持的声明会失败。默认解码大小上限为 16 MiB，调用方可传第四个参数调整。

## 工程入口

`Project::drawingMetadata` 以实际文件路径保存结果。`ProjectOptions::readDrawingMetadata` 单独控制读取，默认开启；关闭后仍列出文件但标为 `Discovered`。`readDrawings=false` 时可以独立读取 XML，但不会创建 DG 配对。成功读取标为 `PartialSemantic`；错误文档按既有 `strictCompanions` 策略处理，宽松模式保留主模型及失败文件诊断。

`drawingMetadataAssociations` 只在以下条件成立时建立：

1. 同一目录中，DG 文件名与 `.metadata` 前面的文件名唯一匹配；大小写歧义不挑选第一项。
2. DG 内保存的文件名与实际 DG 文件名一致。
3. XML 有图纸 GUID、宽、高、类型，DG 有唯一已解读图幅和主体；宽、高、类型逐项相同。
4. XML 主对象 GUID 非空时，必须与已通过 DG 工程范围检查的 DB1 主体关联一致。此时记录 `modelObjectId`；XML 主对象 GUID 为空但 DG 主体不为空时，拒绝配对。

文件配对不证明 XML 的 DrawingGuid 等于 DG 内某个尚未恢复的 GUID；也不证明两个文件代表当前最新状态。相同 DrawingGuid 的不同文件仍按路径保留，不做版本去重或优先级选择。冲突会阻止这项关联并产生诊断，已读取文档及已有 DG / DB1 结果不会被改写。

真实样本恢复五组 XML→DG 配对，其中四张为旧工程总布置图，无主对象关联；9.08 单零件图进一步匹配 DB1 对象 `3040545`，GUID 为 `02f6a87b-e776-431e-a2db-9f8f1865e98c`。XML 中独立保存的图纸 GUID 为 `6b38a239-b7b0-4bb2-ad69-21cebb58a2fa`，与文件名不同，不能混淆。

## 验证与边界

```sh
python -B tools/test_drawing_metadata_evidence.py --data /path/to/corpus --download --exe /path/to/tekladb1_validate --work /path/to/separate-work
```

Python ElementTree 独立核对五份文档的全部 22 个字段，另核对原文字节。15 种变异覆盖尺寸/类型、错误或缺失 GUID、保存状态值、缺失/嵌套字段、严格/宽松解析错误、错误 DG 工程 GUID、错误 DG 数字主体、重命名配对和孤立元数据；大小写敏感文件系统还运行第 16 种重复文件名歧义测试。关闭读取的两个控制路径也有验收。

新增七组正常/异常 CTest、两项安装后消费测试和六项固定语料用例；复用已有五份 XML 及工程，不增加样本文件数。原 779 份文件定义及 1,642 项期望保持不变。图纸日期和标志的生命周期解释、更多 XML 版本/命名空间、DG 自身版本身份及完整图元仍是缺口。

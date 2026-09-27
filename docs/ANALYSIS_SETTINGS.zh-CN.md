# 分析模型预设 `.admodel`

`parseAnalysisModelSettings` 读取分析模型预设中的具名存储值，支持已验证的 `encoding 1252` 文本布局。该格式保存分析引擎、模型名称、方法、迭代、模态、地震、输出和工程信息等设置；它不是 DB6 的对象数据库，也不能证明某组设置已被应用。属性用途可参阅 [Trimble 分析模型属性说明](https://support.tekla.com/doc/tekla-structures/2023/ana_analysis_model_properties)，枚举数值与二进制DB6偏移仍须分别验证。

## 来源与原生证据

固定来源为 [TrimbleSolutionsCorporation/TSOpenAPI_Model_Exam](https://github.com/TrimbleSolutionsCorporation/TSOpenAPI_Model_Exam/tree/7345e5f14c86bf94f9da073705c56526d6895c6d)，复用清单中已有的 `api-exam908-july` ZIP：

- 成员 `API_Developer_Exam_01/attributes/Click here to see A_D wire frame.admodel`。
- 2960字节，SHA-256 `4cc755c564b997cef434c08194a1f881cf87e494e03449c562376e39ebcb5496`。
- 125条具名标量，3条按分析引擎限定的设计表设置；声明Windows-1252，实际换行包含CRCRLF。
- 原生例值：`ModelName="Model 1"`、`AnalysisEngine="NeutralFileIOLib"`、`AccuracyOfIteration=0.001000`、`ModeCount=6`。钢、混凝土和木材设计表各有一条 `code=0`，不把0解释为某个设计规范。

清单另外复用同包主DB1至隔离的 `analysis-settings` 目录，用于工程入口检查。这不是新增独立工程，也不表示目录中的预设处于生效状态。第三方文件不随库分发。

## API与数据保留

```cpp
#include <tekla/AnalysisSettings.hpp>

tekla::AnalysisModelSettings settings;
std::string error;
if (!tekla::parseAnalysisModelSettings(path, settings, error)) {
    // Report error; settings has been cleared.
}
```

`settings.settings` 按出现顺序保存标量的名称、值、原值文本和行号。`AnalysisSettingValue` 为 `variant<int64_t, double, string>`：整数及小数/指数按字面语法区分，字符串去掉外层引号并转为UTF-8。整数0/1不会自动变成布尔，空字符串与一个空格保持区别，Windows路径中的反斜杠不执行转义。

`designSettings` 保存分号记录的表名、分析引擎、字段名、值、原值文本及行号。两个记录集合各保留原顺序；跨集合顺序可由行号还原。`rawText` 和 `rawValue` 保存原始文件字节，**不是转码后的UTF-8**；原文用于精确保留编码、空行、换行及数值拼写。

重复标量键和重复设计表键原样保留并诊断，不选第一项/最后一项，不计算运行时优先级。未知但符合已支持语法的键同样保留。没有执行引擎、打开OutFile/InFile路径或修改模型的行为。

## 工程入口

`readProject` 发现根目录及 `attributes` 直接子目录的 `.admodel`，目录名和扩展名按大小写不敏感识别。新角色 `FileRole::AnalysisSettings` 和 `ProjectOptions::readAnalysisSettings` 均追加以保留原有枚举值与选项顺序；默认读取，结果在 `Project::analysisSettings` 中按文件路径保存。

成功状态为 `PartialSemantic`；关闭独立开关则仅为 `Discovered`。`readRawCompanions=false` 不会关闭预设读取。损坏文件标记 `Failed`，严格伴随文件模式令工程读取失败。

**不生成自动关联：**文件名、ModelName、AnalysisEngine一致不足以证明某个DB6采用了该预设。反例测试人为使这三者都匹配另一工程的AnalysisPartDefaults.db6，仍然不产生文件或对象关联。预设不能替代DB6分析属性或解决IDRM转换。

## 输入范围与验证

支持平面 `name value` 和 `table;engine;field;value` 记录、十进制整数、有限浮点、单行引号字符串及Windows-1252编码。其他编码、BOM、未配对/内部引号、非有限值、溢出/下溢、无效控制字节、缺失记录字段等明确报错；不声称支持其他属性文件布局、注释或未知引号转义。默认大小上限4MiB，可显式调整；读取失败清空输出。

Windows-1252扩展字符转换由合成测试覆盖；当前原生文件的正文为ASCII子集，不能将人工字符变体称为新的原生编码样本。

七组C++测试覆盖存储值、位置、引号/路径、整数与浮点边界、编码、设计表、重复项、头部及大小预算。安装后消费者链接公共API读取字段与编码。`tools/test_analysis_settings_evidence.py` 独立逐行读取原生文件，核对全部125字段、3条设计表、原值文本、行号和完整原字节，并执行26项解析/工程检查。语料清单只保存紧凑指纹，不嵌入第三方文件全文。

尚未完成：其他 `.admodel` 版本/编码、具名枚举代码字典、已应用/当前有效预设判断、DB6字段对应与DB1对象关系，以及其他分析属性文件的语法和语义。

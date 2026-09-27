# options.ini 与 DBV 的对应关系

## 接口与边界

`parseOptionSettingsFile`读取模型目录的文本选项，`matchOptionSettings`将每条赋值与DBV记录比较。两者位于`tekla/Environment.hpp`，核心库即可使用。原始文件、字节值、行号、顺序、重复键和空值都保留；不将最后一条赋值静默替代之前的记录。

```cpp
tekla::OptionSettingsFile settings;
tekla::OptionsDatabase database;
std::string error;
if (tekla::parseOptionSettingsFile("options.ini", settings, error) &&
    tekla::parseOptionsDatabase("options_model.db", database, error)) {
    for (const auto& match : tekla::matchOptionSettings(settings, database)) {
        const auto& source = settings.settings.at(match.settingIndex);
        // source.lineNumber/name/value、match.optionId 定位两侧记录。
        // valueParsed 区分类型无法比较与确实不同；matchingSlots 保留两个槽。
    }
}
```

解析器识别`NAME=value`、可选SET前缀和REM整行注释；关键词以空白分隔，`REMARK=value`仍是赋值。名称支持ASCII字母、数字及下划线，首字符不能是数字。值为等号后完整字节，不去引号、不删除行内注释样式文本、不展开`%变量%`。没有等号、节标题和其他未知语法保留在`rawText`及`uninterpretedLines`并诊断，不执行任何初始化命令。

支持LF、CRLF、CR行结束及UTF-8 BOM；不猜测旧代码页，非ASCII值原样保存。NUL及UTF-16/32输入明确失败。默认上限4 MiB，可通过显式参数调整；失败清空输出，成功清空旧错误。

匹配严格区分键名大小写，保留跨存储类型的同名DBV记录。布尔比较接受TRUE/FALSE；整数严格检查32位范围；浮点按固定locale读取并要求有限值和完整数值；字符串逐字节比较。数值比较允许前后空格，字符串比较不丢弃空格。不匹配任何键的文本赋值仍保留在源文件中；这不表示其在Tekla中无效。

## 工程关联

`readProject`只读取模型目录中的`options.ini`，受现有`readOptions`控制。`Project::optionSettings`保存文本，`optionSettingAssociations`保存配套DBV路径、赋值索引、记录ID和两个槽的比较结果。INI不进入二进制`rawCompanions`，关闭原始伴随库读取也能建立关联。大小上限同时受`rawOptions.maxDecodedBytes`限制；异常文件遵循`strictCompanions`，同目录大小写重名不任意选取。

工程读取级别为`PartialSemantic`：已恢复赋值语义与存储值对应，未知语法及运行时优先级尚未恢复。不读取文本值指向的外部目录，不向模型几何自动应用选项。

## 真实证据

本轮固定28个新增文件，并复用已有W3文件，共29份INI、56条赋值；另外两个工程用例检查路径与记录关联。26个GitHub文件与原DBV处于相同固定提交，下载时核对Git blob、大小及SHA-256；两个培训模型文件沿用原公开源/固定归档。来源、大小和SHA-256均在`tests/corpus.json`，第三方文件不打包进源码。

独立字面证据覆盖：

| 名称 | 文本值 | DBV两槽 | 原始flags |
| --- | --- | --- | --- |
| XS_CONSIDER_REBAR_HOOK_LOCATION_IN_CAST_UNIT_NUMBERING | TRUE | true / false | 0 |
| XS_SMALL_TUBE_ROUND_SEGMENTS | 16 | 16 / 32 | 1 |
| XS_ROUND_SEGMENTS | 40 | 40 / 40 | 1 |
| XS_SHORTENING_SYMBOL_COLOR | 160 | 160 / 0 | 1 |
| XS_CONNECT_UPLOAD_MODEL_FOLDER | Structural\Tekla models | 同一字符串 / 空字符串 | 0 |

这些样本中第一槽都与INI一致；26份新增GitHub文本只有3种内容，重复模板不能当成50个独立修改实验。两个已有DBV备份只改变记录ID，没有flags/值变化，也不提供受控修改证据。浮点文本比较目前以合成用例验证，尚无独立的非整数INI赋值样本。

官方[设置数据库说明](https://support.tekla.com/doc/tekla-structures/2026/sys_settings_databases)及[SYSTEM/MODEL(SYSTEM)说明](https://support.tekla.com/article/system-versus-modelsystem)表明，选项类型和所在目录影响持久化及读取行为；[模型目录赋值示例](https://support.tekla.com/cs/doc/tekla-structures/2023/xs_solid_use_higher_accuracy)确认文本入口。它们不证明二进制偏移、flags含义或两个槽的通用当前/默认关系。因此API保留槽位与比较证据，不命名为最终有效值。

# 7.30 对象内联编号与 DB2 关联

7.30 主模型与组件库在顺序表中直接保存对象编号，与[7.82及现代共享编号记录](OBJECT_NUMBERING.zh-CN.md)不同。公开教学工程及其34组历史备份固定于 `tests/corpus.json`；重复历史对象不算新的独立对象真值。

## 已验证布局

偏移均从payload起点计，不包含行标签。当前主库、组件库及34份DB1备份共36份文件保持以下布局：

| 表 | payload宽度 | 对象范围 | 字段 |
|---|---|---|---|
| 94 | 60 | 10表全部零件对象 | objectId@0、startNumber@4、storedNumber@8、prefix@16..59 |
| 95 | 68 | 2表全部装配对象 | objectId@0、startNumber@4、storedNumber@8、prefix@24..67 |

对象集合包含保存的操作用构件和螺栓等记录，不能仅用最终几何实体集合校验。所有编号对象必须有身份，ID非零、唯一且与对应对象表完整匹配；跨类型重复、缺失记录、错误行标签或宽度拒绝解析，失败清空结果。装配对象类型须为15。@12以及装配@16/20的未知槽位连同完整payload原样保留。

`Model::objectNumberingRecords`继续按记录ID索引；此布局的记录ID就是对象ID。新增 `inlineObjectId` 明确直接对象来源，`storedNumber` 保留原始数值槽。`sequence` 保持零，`objectNumberingReferences` 保持空，因为原文件没有后续版本那种引用表；不伪造上下文或共享引用。

全部真实样本只出现起始号0或1。只有 `startNumber == 1` 且 `0 < storedNumber < 0x80000000` 时提供 `positionNumber = storedNumber`；零值不生成已分配号。非1起始号下该槽究竟是绝对编号还是相对序号，尚无独立证据，保留原值并诊断，不套用后续版本的计算公式。合成输入对非1、高位及零值的测试只验证保留行为。

```cpp
const auto& n = model.objectNumberingRecords.at(objectId);
if (n.inlineObjectId && n.positionNumber) {
    // n.prefix 与 *n.positionNumber 是已保存记录的编号信息。
    // 不据此断言编号当前有效或对象生命周期。
}
```

前缀保留文件原字节。旧7.30入口把94表前缀公开成 `ModelGroup` 属性；该名称不准确，为兼容现有调用保留其原UTF-8转换行为，新代码应使用明确的编号字段。不要把这个兼容属性当成独立模型分组。

## 独立证据与文件范围

来源为 [btscm公开教学工程](https://btscm.fr/dicocm/R/realisations/Lapechelire/Modele%20TEKLA/pechelirejulV1/pechelirejulV1/)。当前主库2,908条、组件库537条编号记录均逐字节核对；同目录 `numbering.history` 与 `numberingresults` 的最后记录独立确认759个零件、708个装配的前缀、系列及编号。另有零件74351和装配74352当前保存零、历史有号；结果保留当前零值，不拿日志覆盖，也不推断差异原因。

无GUID的7.30默认不自动认定DB1/DB2属于同一工程。调用者确认文件来源后可设置 `ProjectOptions::trustLegacyNumberingBasenames = true`，按同目录、同基名、相同版本、精确前缀及数值起始号唯一匹配。结果标记 `NumberingPairEvidence::ExplicitLegacyBasename`。当前主模型产生2,236条系列关联；组件库使用自己的空DB2，不借用主库系列。缺库、缺系列或歧义不抹除DB1已存编号。

34组 `.db1.bak` / `.db2.bak` 显式配对检查共36,285条内联记录，其中22,640条非零，22,581条有匹配系列；剩余59条全部为组件装配记录，其配套DB2为空。361个使用中的系列/类型组合里，58个DB2计数大于本快照对象最大编号。这进一步说明DB2计数不是当前对象数，也不保证等于当前最大编号。历史副本间存在大量重复对象，这些比较不验证完整删除/重编号生命周期。

备份通过单文件接口读取；工程扫描不自动混入历史备份。新增73项外部语料检查及22项正常/异常回归，既有645路径/1,408用例内容保持不变，合计713路径/1,481用例。编号快照、生命周期、未知槽位、非1起始号语义和其他未验证版本仍需继续逆向。

# DB1 对象编号与 DB2 系列关联

DB2 的系列计数不能回答“某个构件的编号是多少”。已核对的现代 DB1 将对象、编号记录、系列信息分开保存。读取接口现在保留这条关联链，并在工程入口与同工程 DB2 的系列匹配。

7.30采用对象内联布局，新增 `inlineObjectId` 和 `storedNumber`，不生成后续版本的共享引用，详见[7.30内联编号证据](INLINE_NUMBERING_730.zh-CN.md)。下文的sequence公式只适用于已验证的现代/7.82布局。

## 存储证据与 API

56份8.95/9.52/9.60主库和组件库保持以下现代布局。9.65/9.66沿用已有现代入口，但没有新增其独立真实样本；7.82采用下文独立布局，8.44尚未验证。

| 主库 / 组件库表 | payload宽度 | 引用描述符 | 当前解释 |
|---|---|---|---|
| 211 / 181 | 12 | 4项，全1 | objectId@0、rawContext@4、numberingRecordId@8 |
| 322 / 286 | 68 | 9项，前三项1，其余0 | 零件编号记录 |
| 323 / 287 | 76 | 11项，前三项1，其余0 | 装配编号记录 |
| 324 / 288 | 44 | 7项，前三项1，其余0 | 未解释的第三种记录，保留原始payload |

零件/装配记录共1,982条，均有id@0、startNumber@8、sequence@12。零件prefix在@20..67，装配prefix在@28..75。@4及编号与前缀之间的槽位含义未验证，连同其他字节保存为`rawPayload`。第三种记录不根据看起来相似的整数或文本生成前缀、起始号或编号。

`Model::objectNumberingRecords`按记录ID索引，包含共享及未引用记录；`objectNumberingReferences`按对象身份ID索引，保留零引用和原始上下文。上下文值出现0、585288、588603，含义尚未确定，不据此推断编号批次或生命周期。27,303条引用指向零件/装配编号记录；另外1,447条非零引用指向未解释的44字节记录，仍完整保留。

`ObjectNumberingRecord::kind`区分`Part`、`Assembly`和`Unverified`。`positionNumber`是可选派生值：只有起始号、序号均大于零，起始号及`startNumber+sequence-1`落在普通正有符号32位范围时才提供。计算使用64位避免溢出。序号零、特殊高位起始值或越界结果保留存储值，optional为空，不能把它们自动当成编号0、默认号或已删除对象。

```cpp
const auto& ref = model.objectNumberingReferences.at(objectId);
if (ref.numberingRecordId != 0) {
    const auto& record = model.objectNumberingRecords.at(ref.numberingRecordId);
    // prefix + positionNumber 是存储记录的可验证派生信息。
    // optional为空或kind为Unverified时，不生成确定编号。
}
```

严格检查表宽/签名、重复记录ID（包含跨种类冲突）、重复对象引用、缺失对象身份及非零目标断链。失败清空结果。原始44字节记录产生未解释语义诊断，不因它们存在而丢掉整个模型。

## 独立编号日志与工程关联

[固定培训模型](https://github.com/letstekla/Tekla-Structures-Drawing-Automation-Through-Grasshopper-in-Rhinoceros-3D/tree/efb24b30a722de4afc9052331e2e24b69abba6cf)的DB1、DB2和`numberinghistory.txt`来自同一压缩包，文件SHA-256已固定在`tests/corpus.json`。日志的最后一条对应GUID记录与187个零件、43个装配的前缀、起始号和派生位置号完全一致。这里验证的是保存记录与日志的对应，不证明所有历史状态、编号有效性或后续编辑。

该模型有269条零件/装配记录引用。`Project::objectNumberingSeriesAssociations`提供249条到DB2系列的关联；其中包含上述全部230个日志对象。另20条引用使用起始值`0x80000001`、序号零，没有对应DB2系列，保留记录并诊断，不强配到空前缀的普通系列。

每条工程关联包含源DB1路径、目标DB2路径、objectId、numberingRecordId及seriesIndex，后者索引`Project::numbering.at(numberingDatabase).series`。配对要求相同存储版本、同工程目录、同文件基名、双方非空且相同的数据库GUID（忽略GUID字母大小写）；主库和组件库各用自己的GUID。系列按精确前缀与数值起始号唯一匹配，大小写不折叠。若`P/100`与`P/0100`同时存在，数值系列有歧义，不任选一条。缺库、未读取DB2、GUID缺失/冲突或系列缺失/歧义都不生成关联。显式关闭readNumbering时不执行这一步。

现代配对的`pairingEvidence`为`NumberingPairEvidence::DatabaseGuid`。没有GUID的7.30/7.82文件默认不生成上述确定关联；调用者可按下节显式确认其配对。

系列关联可以表示尚未分配序号的记录所引用的系列，不能以关联存在代表编号已生效。DB2计数不用于重算或限制对象编号；真实对象数、历史分配次数与当前最大编号未必相同。

56项逐文件回归覆盖字段、原始payload、空引用、未解释种类和对象引用；一项独立日志/工程配对验证上述230个对象及249条关联。合成测试覆盖主库/组件库/8.95、未引用记录、异常签名/宽度、重复/断链、零/特殊/溢出范围、失败清空及跨数据库配对。

## 7.82 独立布局与配套图纸证据

两份主库及两份组件库共有685条编号记录、43,473条对象引用，其中7,410条非零引用全部连接零件或装配编号记录。其余为空引用，也保留。已存未知上下文不解释为时间戳或批次。

| 主库 / 组件库表 | 宽度 / 描述符数 | 字段 |
|---|---|---|
| 211 / 181 | 12 / 4 | objectId@0、rawContext@4、numberingRecordId@8 |
| 215 / 185 | 64 / 8 | id@0、startNumber@4、sequence@8、prefix@16..63 |
| 216 / 186 | 72 / 10 | id@0、startNumber@4、sequence@8、prefix@24..71 |

这些表的描述符都是第一项1、其余0。未知槽位随rawPayload完整保留；派生号的范围规则与现代记录一致。主217/组件187实际宽96且在四份样本中全空，不能套用现代第三种44字节记录的解释。非零目标无法连接上述两类记录时拒绝语义解析，可另用原始数据库入口检查。

同一固定PSDBIM工程的DG标记文字提供以下对照，数字ID只用于这组已明确选定的文件，不推断其他工程归属：

| 图纸主体ID | 类型 | 起始号 / 序号 | 标记文字 | 额外核对 |
|---|---|---|---|---|
| 1547355 | 零件 | 1 / 5 | ps5 | 完整PART_POS标记字符串 |
| 1547325 | 零件 | 1 / 4 | ps4 | 完整PART_POS标记字符串 |
| 1545718 | 装配 | 2000 / 1 | AR2000 | 主零件自身编号为rod1 |
| 1541434 | 装配 | 2000 / 1 | AR2000 | 主零件自身编号为rod1 |

总共13份图纸包含上述文字，但仅涉及4个独立主体，测试选取每个主体一份图纸。文字元素的存储名虽为PART_POS，装配例子与主零件的编号不同；不能凭元素名称把两种对象混淆。此证据支持该组保存记录与图纸文字一致，不证明全部图纸标记归属、当前性或完整编号生命周期。

若调用者已确认旧版文件属于同一工程，可显式开启同目录同基名配对：

```cpp
tekla::ProjectOptions options;
options.trustLegacyNumberingBasenames = true;
tekla::readProject(directory, project, error, options);
// 检查每条关联的 pairingEvidence，旧版为 ExplicitLegacyBasename。
```

该选项只适用于双方均为7.82且均无GUID的文件，不绕过现代GUID冲突、旧版已有GUID冲突或版本不匹配。来源依据随每条关联保留为`NumberingPairEvidence::ExplicitLegacyBasename`，不伪装成GUID匹配。PSDBIM主库在显式确认后有5,192条系列关联；未匹配的340条非零引用保留并诊断。原有DG自动工程配对逻辑不受此选项影响。

四项7.82逐文件回归验证全部编号字段与引用，一项固定工程证据核对四个主体、图纸文字、DB2系列、默认无关联和显式配对行为。17项额外合成测试覆盖旧布局与错误输入，并检查显式配对不会掩盖冲突。

## 尚未完成

- 第三种44字节记录、原始上下文和未知标志的语义。
- 8.44编号布局、旧版第三种记录、生命周期、变更与有效编号状态。
- DB2比较快照。现有60份DB2仅两份有非空记录，且只含22号系列表；空表回归不能证明快照已解析。
- 全版本和全部实体类型的编号真值。现有独立编号日志来自一个工程，不能外推全部Tekla版本。

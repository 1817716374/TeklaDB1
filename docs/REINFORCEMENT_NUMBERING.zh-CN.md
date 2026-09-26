# 钢筋编号系列字段

现代DB1第三种44字节编号记录现标记为 `ObjectNumberingKind::Reinforcement`，可读取保存的前缀、起始号及对象引用。**分配编号、有效状态及DB2钢筋计数尚未恢复**。

9.65/9.66沿用已有现代布局签名检查，未新增其真实样本；7.30/7.82/8.44未启用这一钢筋字段映射。

## 字段与来源

当前58份8.95/9.52/9.60主库和组件库中，22份包含此类记录，共222条；1,447条非零引用的来源身份类型全部为47。主库324表、组件库288表的payload宽44，7项描述符的前三项为1、其余为0。偏移不包含行标签：

| 偏移 | 当前解释 |
|---|---|
| 0 | 记录ID，与对象ID分开 |
| 4 | 未解释整数，保留于rawPayload |
| 8 | 未解释整数槽位，保留于rawPayload |
| 12 | startNumber |
| 16..43 | prefix，至零字节或完整28字节边界 |

此排列与零件/装配记录不同，不能套用其起始号与序号偏移。`sequence`维持未解码的零，`positionNumber`为空；这不表示钢筋没有编号，也不表示编号为零。`inlineObjectId`和`storedNumber`仍只用于已定义的7.30布局，不拿它们伪装此槽位的语义。全部payload保留。

主211/组件181表继续提供真实对象引用及原始上下文；对象ID、编号记录ID可能不同，多对象可以共享记录，也允许未引用记录。引用钢筋编号时，身份类型必须为47；8.95通过类别记录取recordKind。零目标引用原样保留。记录ID零、重复、断链、错误宽度/签名及错误身份类型拒绝解析，失败清空。新增枚举值追加在既有值之后，原Unverified/Part/Assembly的枚举数值不变。

## 独立参数核对

证据来自 [BIM-Modeling固定提交](https://github.com/renatogcruz/BIM-Modeling/tree/5cd771708b4f5b4f5cdfd3c92201d8e187f11f0f/tekla) 的两份组件库，文件来源/大小/SHA-256已在 `tests/corpus.json` 固定。`proSERIE`、`proSTARTNUMBER`公式通过同owner的参数名称取得字面默认值，与以下对象连接的编号记录核对：

| 对象ID | 编号记录ID | 前缀 | 起始号 |
|---|---|---|---|
| 1127972 | 1127978 | 0.00 | 0 |
| 1136014 | 1136019 | VG | 1 |
| 1136281 | 1136285 | G | 1 |

每份文件核对3个前缀和3个起始号。两份模板复用相同对象，12项检查不等于12个独立设计，更不验证最终位置号。验证不执行公式，仅核对无歧义的同owner字面参数；其他作用域中的同名参数不混用。

222条记录中，未知@8有220个零值，另有值4和47的两条记录，位于Construsoft W3主库，记录ID447441、448175。该模型无钢筋实例，两条记录均无引用，不能把它们绑定到任意构件或当成当前分配号的证据。

## 使用与关联边界

```cpp
const auto it = model.objectNumberingReferences.find(reinforcement.id);
if (it != model.objectNumberingReferences.end() && it->second.numberingRecordId) {
    const auto& n = model.objectNumberingRecords.at(it->second.numberingRecordId);
    // n.kind == Reinforcement：n.prefix / n.startNumber 是已保存系列设置。
    // n.positionNumber 当前为空；n.sequence 的零不是分配状态。
}
```

DB2当前已验证的系列计数只涉及零件和装配。即使存在相同前缀/起始号，`readProject`也不会生成钢筋到DB2的系列关联；对象到DB1编号记录的原始关联不受影响。

回归覆盖58份现代数据库的记录、payload、类型和对象引用，另有两项参数证据；20项合成检查覆盖主库/组件库/间接身份、28字节及非ASCII前缀、零和高位值、共享/未引用记录、错误引用及工程隔离。合成非1起始号仅证明字段保存，不证明最终编号公式。既有编号语料中获得新kind/字段的22项语义指纹据此更新，其余语料定义不变。

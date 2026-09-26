# DB1 钢筋字段与验证边界

`Model::reinforcementDefinitions`、`reinforcements` 和 `reinforcementIdsByFather` 恢复现代布局中的 kind47 钢筋实体及父构件关系。定义名称、类别、材质、尺寸、弯曲半径、已存形状坐标、原始间距/数量数组、属性和组件变量可读取。它们尚不等于最终钢筋中心线或可制造几何。

56份固定现代主库/组件库中21份非空，共1,447个实例、301份定义。多个导出文件复用同一组件库，不能把这些总数视为独立设计数量。7.82现有四份样本没有非空实例，8.44缺独立验证，均未启用此语义解码。9.65/9.66沿用角色签名检查，仍未重新取得公开非空样本。

## 布局

偏移相对于payload，不含行标签；整数与浮点均为little-endian。

| 角色 | 主库 / 组件库表 | payload字节 | 字段描述符为1的位置（从0计） |
|---|---|---|---|
| 定义 | 183 / 153 | 32 | 9项中的0,1,2,4,5,6,7 |
| 实例 | 184 / 154 | 56 | 12项中的0至8 |
| double数组块 | 205 / 175 | 120 | 18项中的0,1,2 |
| integer数组块 | 206 / 176 | 60 | 16项中的0,1,2 |

定义为id@0、模式整数数组引用@4、class@8、name字符串引用@12、grade引用@16、size引用@20、钩参数数组引用@24。末尾@28未命名。字符串通过已有的全局字符串链解析；不能把尺寸文本强制转成直径。`modeValues`和`hookValues`保留完整链式值，但不推定枚举及参数排列。`rawPayload`保留完整定义字节。

实例为id@0、definitionId@4；四个数组引用分别位于@16、20、24、28，对应`storedShapeCoordinates`、`radiusValues`、`spacingValues`、`storedDistributionValues`。形状值按三个double组成坐标，但多polygon分段、坐标框架、保护层、钩和分布求值仍未确认；不能直接把它们当作最终中心线或重复应用构件变换。间距数组可保存数量，例如3根，也可保存目标间距，例如75；必须结合尚未完全解码的模式才可解释。实例未知字段保留于`rawPayload`，不将看似浮点的尾部字节强制验证为double。

数组块头为id@0、nextId@4、当前块元素数@8。double元素从@24开始，每块最多12个；int32元素从@20开始，每块最多10个。@12、@16及double块@20仍未命名。零引用表示空数组。检查所有块的重复ID、计数、后继和环；只对有效double元素检查有限性，未使用槽位保持不解释。`ModelReadOptions::maxReinforcementArrayValues`限制总元素展开及块遍历次数，默认16,777,216，可按内存预算调整；避免共享长尾导致无界扩张，也不把文件大小误当成合法展开量上限。块元数据可通过`parseRawDatabase`读取。

主191/组件161的type47关系：fatherPartId在@8，钢筋ID在@12。每个实例要求唯一父构件；反向索引有序。placement来自主64/组件43：frame引用@4、origin@8、storedLength@32。8.95身份通过`IdentityClass::recordKind`识别，不能把class引用当作owner，也不改写已有`Identity::type`推断结果。其他现代布局直接核对身份类型47。

## 参数证据

固定AUTRA的`ITIFinal02_35/xslib.db1`（8.95）和`steel-detailing/xslib.db1`（9.52），各有96项公式→同owner参数默认值→钢筋字段的一致性检查：

- 名称、类别、材质、尺寸各16项。
- 弯曲半径16项，例如T460、20尺寸的Vert使用60，10尺寸的Stirrups使用20。
- `proNUMBER_OF_BARS`13项、`proTARGET_SPACING`3项分别对应原始数组值。

测试只读取已存字面量，不执行公式。这些文件含重复模板，上述证据是字段交叉佐证，不是192份独立工程或Tekla几何导出的真值。[官方RebarGroup属性说明](https://developer.tekla.com/doc/tekla-structures/2025/rebar-group-properties-53406)解释名称、材质、尺寸、半径以及按模式解释的间距概念，但不提供本库的二进制偏移。

新增56项逐文件回归和2项参数证据检查。合成异常测试覆盖缺失/重复记录、父关系和placement、数组断链/环/错误计数、非有限数值、描述符/宽度、共享/未引用定义、空引用、展开预算及失败清空。既有1,243项语料对象未改动。

## 使用

```cpp
for (const auto& [id, reinforcement] : model.reinforcements) {
    const auto& definition = model.reinforcementDefinitions.at(reinforcement.definitionId);
    const auto& father = model.parts.at(reinforcement.fatherPartId);
    // definition.name / grade / size / classNumber
    // reinforcement.radiusValues / spacingValues / storedShapeCoordinates
    // model.objectNumberingReferences uses this same database-local id.
}
```

第三种44字节编号记录的对象引用与这些钢筋ID相连，但编号字段和有效状态仍未知。完整钢筋种类、模式枚举、钩/保护层、变截面、分布、最终弯曲几何和受控修改真值继续列为未完成项。

# 7.82 表面处理对象

PSDBIM 的固定公开组件库中有7个表面处理对象、6份共享定义。这些对象原来只保留在raw表和身份/变量引用中，现在由 `Model::surfaceTreatments`、`surfaceTreatmentDefinitions` 和 `surfaceTreatmentIdsByFather` 返回。它们不加入结构构件 `parts`，也不作为额外钢板自动生成实体。

## 布局与关联

偏移均相对payload，不含行标签。7.82表描述符均为首项1、其余0。

| 表 | 主库 / xslib | 宽度 / 描述符数 |
|---|---|---|
| 表面实例 | 145 / 117 | 78 / 12 |
| 表面定义 | 146 / 118 | 292 / 29 |

实例的ID@0、定义ID@4、起点@8、终点@12、轮廓@16、方向@20、原点double[3]@24、已存长度double@48。最后22字节未解释，完整保存在 `rawTail`。点、方向和轮廓分别引用既有表；轮廓保留局部坐标、倒角参数及微小的非零Z值，不强行压平。GUID与owner来自同ID身份记录。坐标和长度按存储值返回，不重新应用厚度或位置设置。

父构件关系来自主库191 / xslib161的type73关联，方向是父构件→表面对象。实测7条均唯一，父对象均存在于 `Model::parts`；缺失父对象、缺失表面目标或重复父关系会拒绝解析。`ownerId`表示归属作用域，与 `fatherPartId` 分开保存。反向索引按ID排序。

定义字段：

| 偏移 | API字段 | 长度/类型 |
|---|---|---|
| 0 | id | u32 |
| 68 | classNumber | 文本22字节 |
| 90 | name | 文本22字节 |
| 112 | profile | 文本62字节 |
| 174 | material | 文本32字节 |
| 206 | typeName | 文本62字节 |
| 268 | typeCode | u32 |

@4..64的16个u32保存在 `rawHeader`，@272..288的5个u32保存在 `rawTail`。不根据零值或单一样本给未知字段命名。

`thicknessFromProfile`只在profile严格匹配 `PL数值` 时给出派生厚度。例如STEP定义的PL1.587对应1.587；不匹配的文本保留，optional为空。这不是直接读取的独立厚度字段，也不等于已恢复全部表面实体几何。

## 独立说明与配套文件证据

6份定义均保存 `TS1 - Tile surface 1` 和typeCode=3。官方[CODE说明](https://support.tekla.com/doc/tekla-structures/2026/code)确认TS1是该表面处理子类型，官方[SurfaceTypeEnum](https://developer.tekla.com/doc/tekla-structures/2025/surface-treatment-surface-type-enum-enumeration-53895)列出瓦片表面处理的代码3。[SurfaceTreatment API](https://developer.tekla.com/doc/tekla-structures/2025/surface-treatment-class-53873)明确有父构件、区域多边形、材质、厚度、起终点与位置等概念；这些文档用于实体含义核对，本身不证明二进制偏移。

所有7个实例合计32个轮廓点、132个距离变量引用和34个以表面为目标的公式绑定。`distanceParameterIds`和`formulaBindingIds`提供对应索引；公式保持原文，不执行。例：表面20871的父构件为20859、定义为20881，名称STEP、材质Zero_Density。公式21385作用于它的 `proPOSITION_AT_DEPTH`，表达式为2；此公式不用于推断未验证的位置枚举转换或重新计算当前值。

同工程matdb.bin包含6份定义所用的全部材质名称（Zero_Density和3000）。`readProject`新增 `surfaceMaterialAssociations`：每条关联包含源DB1路径、表面对象ID及 `materials->materials` 中的索引。只接受精确且唯一的名称；缺失目录、名称缺失或重名产生诊断，保留表面对象，不随意选择材质。公开语料核对7条组件库→材质目录关联，源路径避免主库/组件库数字ID混淆。

## 验证与剩余范围

两份7.82主库和两份组件库均有回归，其中只有PSDBIM组件库包含非空表面记录。另有配套材质/变量/实体证据检查。合成测试覆盖主库和组件库入口、错误签名/宽度、重复记录、身份/定义/点/方向/轮廓断链、父关系缺失/重复、非有限位置及活动倒角、未知型材、原始字段保留和缺失材质目录。

56份8.95/9.52/9.60文件在相同表位有对应宽度和稳定现代签名，但均为空。本轮语义实现仅启用于7.82；现代非空表面处理仍待样本验证。

颜色、布尔裁剪标志、铺贴图案、旋转/位置完整规则、最终裁剪区域、面积/体积/重量、生命周期尚未恢复。轮廓与材质关联不构成独立几何真值，也不能宣称表面处理或DB1全语义已经完成。

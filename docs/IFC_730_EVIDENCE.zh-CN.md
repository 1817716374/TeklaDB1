# 7.30 DB1：同工程 IFC 独立证据

此项核对直接读取 Tekla 导出的 IFC，与本库的 `Model` 输出逐对象比较。它增强已存属性及简单几何的证据，不代表整个模型的最终几何已经正确，也不新增通用 IFC 读取 API。

## 固定来源

[公开教学工程 IFC 包](https://btscm.fr/dicocm/R/realisations/Lapechelire/Modele%20TEKLA/pechelirejulV1/pechelirejulV1/out.ifcZIP)来自此前已固定的 `pechelirejulV1.db1` 同目录。IFC2X3 文件记载导出时间 `2012-07-10T10:29:28`，导出程序为 Tekla Structures Next Build 5712 / IFC Export Version 136。时间戳仅说明文件保存的信息，不证明两份文件之间没有历史差异。

| 对象 | 字节数 | SHA-256 |
|---|---:|---|
| `out.ifcZIP` | 2,420,513 | `c582a87234285fe885199183dbe65da93ef1b90b41743416a3ddf8e0323efefa` |
| 成员 `out.ifc` | 13,124,990 | `d02db80ded529334096fa911f2194978f96b4bc3a0720deee31247992cc295a3` |

下载器独立验证压缩包与成员，第三方模型不随源码分发。`tests/corpus.json` 新增一个文件路径与一项 `ifc730_evidence` 用例，之前644个文件及1,407项用例对象保持不变。

## 身份、属性与坐标

IFC 中的957个梁、91个板、167个柱，共1,215个构件均带 `TS_<DB1 ID>` 标签。压缩GUID解码后也全部与DB1 GUID相等，因此配对同时使用数字ID和GUID，不凭几何相似猜测对象身份。

以原始 `IfcRelDefinesByProperties` 连接到 `Tekla_General` / `Tekla_Quantities` 的已存属性值作为独立期待值；共享属性集完整展开，冲突值拒绝。该关联的标准定义见 [buildingSMART IFC2X3](https://standards.buildingsmart.org/IFC/RELEASE/IFC2x3/TC1/HTML/ifckernel/lexical/ifcreldefinesbyproperties.htm)。

- 1,215个名称、材质、类别逐项一致；名称另与IFC产品名称交叉核对。
- 2,430个起终点，与 `OrigineX/Y/Z`、`ExtrémitéX/Y/Z` 对应；每坐标容差0.002毫米。
- 1,215个存储原点，与产品placement原点对应，同样使用0.002毫米容差。
- IFC长度单位必须为毫米；全部父级placement逐级检查为单位变换，不把任意局部坐标当成世界坐标。
- DB1有1,240个实际构件，另25个未出现在该IFC中。验证器固定检查缺失ID集合，既不丢弃DB1对象，也不据此声称这些对象被删除。

IFC只包含该模型的导出子集；组件库、螺栓、焊缝、钢筋等不因此获得独立几何验证。

## 八顶点验证

对所有满足以下结构条件的已配对对象进行检查：DB1型材严格为两个数字相乘的矩形、没有轮廓，且没有已解析布尔、切割平面或拟合操作。选择条件不依赖误差或验证结果，共536个对象、16种型材。

从IFC产品的 `Body/Brep` 引用遍历 `IfcFacetedBrep → IfcClosedShell → IfcFace → IfcFaceOuterBound/IfcFaceBound → IfcPolyLoop → IfcCartesianPoint`，读取独立保存的顶点。先按IFC placement变为世界坐标，再投影到DB1坐标基。每个对象必须有八个不同顶点，且必须逐一占据以下八个角，不能只比较包围盒：

```text
x ∈ {0, stored length}
y ∈ {-profile first dimension / 2, +profile first dimension / 2}
z ∈ {-profile second dimension / 2, +profile second dimension / 2}
```

4,288个顶点全部通过，每坐标容差0.01毫米，用于容纳导出文本和数据库方向向量的舍入。方向按 [IfcAxis2Placement3D](https://standards.buildingsmart.org/IFC/RELEASE/IFC2x3/TC1/HTML/ifcgeometryresource/lexical/ifcaxis2placement3d.htm) 的规则归一化并正交投影；`RefDirection`本来就可以不严格垂直于`Axis`，不能将其误拒绝。

此项证明这536个对象的解析原点、坐标基、截面和存储长度能恢复IFC八顶点。它没有执行本库OCCT后端，也没有证明面朝向、拓扑、布尔运算或其余679个已导出复杂构件的最终实体。

## 差异必须保留

独立审计发现837个导出型材字符串与DB1存储文本不同，例如 `125*30 → 30*125`、`80*6 → PLAT6*80`、轮廓板的 `PL10 → PL10*152.3`。这种导出表示不能拿来覆盖用户存储的型材名称，也不能只比较字符串就断言几何错误。

IFC最终数量 `Longueur` 与DB1 `Part::length` 在491个对象上不同。后者是存储位置/挤出长度；多段轮廓、切割和最终数量的长度定义可能不同。不能直接改写、再次施加偏移，或把所有差异解释为已解决。

额外180个带操作的简单矩形中，81个最终包围尺寸仍与未裁剪尺寸相同，99个不同。这些对象不进入八顶点验证集合；下一步应对完整OCCT结果与IFC实体逐对象比较，继续恢复切割、复杂截面和轮廓语义。

## 运行

常规外部语料命令会自动执行该项，也可在下载后单独运行：

```text
tekladb1_validate ifc730_evidence <corpus-root>/btscm-ifc730
```

验证器从同一固定语料根目录的 `btscm-pechelire/pechelirejulV1.db1` 读取配套DB1。测试专用IFC读取器只接受本项证据用到的布局，不应被应用程序当作通用IFC解析器。

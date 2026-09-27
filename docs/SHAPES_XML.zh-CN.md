# Shapes XML、Polymesh 与文件关联

`parseShapeDefinition` 按 `ImportPart/Info` 结构读取定义；`parseShapeGeometry` 按 `Polymesh` 结构读取点、面、内环和边。`parseShapeCatalog` 同时识别普通 `.xml` 几何和 gzip 压缩输入（通常为 `.tez`），不再把所有XML文件都当作定义。定义的 `BrepStorageId` 与几何文件名主干 `storageId` 对应。Trimble的[形状目录说明](https://support.tekla.com/cs/doc/tekla-structures/2020/mod_shapes)也明确区分 Shapes 下的定义XML和 ShapeGeometries 下的XML/TEZ几何。

## 真实缺陷与独立证据

公开[AUVENT工程](https://btscm.fr/dicocm/C/CCF/CCF_BE_2019/AUVENT_CCF_BE_AMCR1_2019_final/)中的定义 `712cacb7-119f-4da3-8738-9c5dca170300.xml` 引用几何 `a1d452b2-d351-4a0a-9376-d4e4cfad751e.xml`。定义没有 `BrepGeomType`，但有 `IsSolid=False`；旧入口把缺少类型视为错误，目录入口又忽略XML几何，最后得到0个定义和0个几何。

修复后恢复1个定义及其几何，60个点、58个面、117条边全部保留。独立Python ElementTree读取器逐项比较定义字段、所有坐标、面/内环索引、边端点和类型；几何计算的六个包围盒极值与另一份定义文件完全吻合。全部点位于Y=0平面，与保存的非实体声明一致，但这不等于完成了拓扑/流形检测。

两份公开原件固定在 `tests/corpus.json`，只有URL、大小及SHA-256随库发布；第三方原件不随源码分发。测试另对公开XML插入注释、嵌套同名字段、字符实体，仍要求与ElementTree一致；非法数字、索引和重复GUID字段必须失败。将原XML压成gzip验证压缩/未压缩入口等价，这个压缩副本明确属于测试变体，不能冒充额外Tekla原生TEZ样本。当前主语料此前没有固定的Shapes/TEZ文件；本轮增加这对实际XML，不能由历史目录功能描述推断已有广泛TEZ版本覆盖。

## API与边界

保留原有三参数入口，同时提供带 `ShapeReadOptions` 的四参数重载。默认每个文件解压后上限128 MiB，调用方可显式调整。所有失败入口清空结果并返回诊断；目录入口保留其他有效项并记录逐文件失败。

`ShapeDefinition` 在原字段后追加：

- `hasGeometryType`：区分实际保存的 `BrepGeomType=0` 与未保存类型；缺失时旧 `geometryType` 保持默认0。
- `isSolid`：可选的已存声明；不由点数、面数或默认值猜测，也不代表已验证实体拓扑。
- `rawXml`：完整解压后的原文，包含BOM、空白和未知扩展。

`ShapeGeometry` 同样追加 `rawXml`。未知边类型保留在既有 `rawType`，枚举为Unknown。原始坐标与索引不重排，不将平面网格包装成闭合BRep。

结构读取只采用相应父节点的直接子元素。注释和扩展节点里的同名字段不能覆盖已知字段；重复已知字段、非标量内容、错误根、多个根、非UTF-8字符、未知实体、DTD及外部实体声明均拒绝。数字采用与区域设置无关的十进制解析，坐标必须有限，包围盒不能反向，索引必须为32位非负整数且落在点数组内；外环和每个内环至少3个索引。XML转义只解码一次，CDATA按原文本读取。

目录依据实际根元素识别定义和几何，合法的其他根类型跳过。一个目录扫描范围内重复定义GUID或几何storage ID会移出可选结果并报告歧义，避免依赖文件遍历顺序挑选对象。`readProject` 仍按原有资源目录优先级合并 Shapes/ShapeGeometries 后再检查缺失引用；本轮不改写跨资源目录的覆盖规则。

当前验证的定义版本为1.0、几何为无默认命名空间的Polymesh。未知扩展保留原文，未宣称理解其语义。其他版本/命名空间、Tekla原生TEZ变体、大型复杂面/内环、曲面BRep及完整拓扑合法性仍需要真实样本与独立出图或导出验证。

# 钢筋形状目录（RSCX）

`parseRebarShapeCatalog` 读取 `.rscx` XML 中 `ShapeCatalog` 下的分类、每个分类的 `Shape` 编号，以及直接放在根节点下的未分类编号。分类和编号按源文件顺序保留；解析结果包含源路径和完整原文。解析器不会推断编号对应的几何、弯折参数或钢筋实例。

## 真实样本

固定样本来自 Trimble 官方公开的 [TSOpenAPI Model Exam](https://github.com/TrimbleSolutionsCorporation/TSOpenAPI_Model_Exam) 9.08 工程归档，成员为 `API_Developer_Exam_01/attributes/RebarShapeCatalog.rscx`。归档本身已在 `tests/corpus.json` 固定 URL、字节数与 SHA-256；目录成员另固定解压后字节数 736 和 SHA-256。原件不随源码发布。真实文件包含 5 个分类、14 个分类编号和 2 个根级编号。该计数用于回归，不代表已验证钢筋形状几何。

## 接口边界

结果提供有序的 `categories`、`uncategorizedShapeIds`、`sourcePath` 和 `rawXml`。工程入口会在模型资源搜索目录（包括 `attributes`）发现 `RebarShapeCatalog.rscx`，作为语义目录资源暴露；目前不把目录编号与 DB1 钢筋对象自动配对。缺失/空分类名、空编号、错误根、未知直接子元素和类别内非 `Shape` 子元素会失败并清空结果。结构回归覆盖 XML 实体解码、顺序、根级条目、原文保留和上述错误形态，工程回归验证资源发现与清单分类。

## 与 DB1 钢筋的关联检查

官方 9.08 配套工程的 DB1 解析出2,834个钢筋实例、93个定义；同包 RSCX 有16个目录编号（14个分类编号、2个根级编号）。定义公开的 `name`、`grade`、`size` 与这些目录编号没有精确命中（按定义名比较为0）；钢筋实例可见属性也没有目录编号字段。因而工程入口没有建立钢筋→RSCX 编号链接。DB1 的 mode/hook 数组含义尚未独立解释，不用数值相似或数组内容猜 shape ID。该检查只证明当前解析字段不足以形成精确键，不排除其他版本通过未解析字段或计算规则保存目录引用。

这个入口仅解析目录索引。形状编号定义、各版本形状语义、间距/弯折参数与实际 DB1 钢筋对象的关系仍需其它真实文件和独立真值验证。

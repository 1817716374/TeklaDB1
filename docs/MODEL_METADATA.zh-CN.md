# 工程元数据 TeklaStructuresModel.xml

`tekla::db1::parseModelMetadata` 读取 `TeklaStructuresModels` 根下唯一的直接 `Model`。
模型目录和工程入口沿用这个读取器，解析错误不会留下部分填写的结果。

现有13个字段继续可用：Name、Designer、Description、Version、ProductVersion、Language、Template、Environment、IsTemplate、XS_PROJECT、XS_FIRM、XS_SYSTEM、ConnectedId。
字符串保留首尾空白，按XML 1.0处理换行；只解码一遍五种预定义实体和十进制/十六进制字符引用。
注释不会提供字段值，同名嵌套节点也不会覆盖直接子节点。CDATA保持文本语义。
IsTemplate单独去除XML空白，接受大小写不敏感的TRUE/FALSE、1/0；缺失或空值沿用false，其他值报错。

新增 `ModelMetadata::rawXml` 保存解压后的完整源字节，包括BOM、原始换行和未知扩展。
2025公开样本含区域设置、工程属性及共享信息，本轮没有把它们全部解读成具名API；调用方可从原文追溯。
搜索路径仅为存储文本，不展开变量，不读取路径所指目录。

输入支持UTF-8及可选BOM，解压后限制16 MiB。支持样本中出现的无子集声明 `<!DOCTYPE TeklaStructuresModels>`；
带内部子集、外部DTD、实体声明以及其他编码不在支持范围内，直接报错。读取过程不请求外部资源。
错误根、多根、多Model、重复已知字段、嵌套在标量字段内的元素、无效字符引用和不完整结构均拒绝。
底层私有内置[TinyXML2 11.0.0](https://github.com/leethomason/tinyxml2/tree/9148bdf719e997d1f474be6bcc7943881046dba1)，
命名空间隔离，并明确记录了空白节点保留和孤立闭合标签拒绝两处本地修改；源码、许可证和散列位于 `third_party/tinyxml2`；使用者无需另外安装XML依赖，安装目录包含其许可证。
此入口不是通用XML Schema/DTD验证器；Shapes XML及DG内嵌XML仍有各自的支持边界。

## 独立验收

主语料的32份固定来源元数据均为一个直接Model，31份带上述DOCTYPE，3份字段带换行。
`tools/test_metadata_evidence.py` 先检查来源文件散列，再用Python标准库ElementTree独立读取13个字段，
按固定顺序与C++输出对照，同时核对完整原文字节。期望值固定在 `tests/corpus.json`，不会在测试时用生产解析器重写。
可复现命令（数据目录使用仓库外路径）：

```sh
python -B tools/test_metadata_evidence.py --data /external/tekla-corpus --download --exe /external/build/tekladb1_validate
```

合成回归另覆盖评论/嵌套遮蔽、双重实体、数字字符引用、非BMP字符、CDATA、混合文本、
布尔空白、错误结构、声明、编码、大小和深度限制；安装消费测试直接调用公开API。
本轮不改变既有模型/DG/DB2语料的期望值。

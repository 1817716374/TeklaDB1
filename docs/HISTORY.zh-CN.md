# Tekla `history.db`

`parseHistoryDatabase` 以只读方式读取 SQLite `history.db`，不需要用户安装 SQLite。核心内部静态编入官方 SQLite amalgamation 3.54.1；SQLite 源码属于公有领域，版本来源和文件说明见 [`third_party/sqlite/README.md`](../third_party/sqlite/README.md)。

目前读取 `user`、`revision`、`model_item` 和 `model_item_revision` 表。文本、数值、BLOB 和 16 字节 GUID 均保留为原始值；`version_data` 与 `revision_hash` 不解码。查询受最大解码字节数及总行数限制。数据库打不开、表结构不兼容或 GUID 字段宽度错误时返回失败，不留下部分结果。

`readProject` 默认读取模型根目录的 `history.db`，将其以 `PartialSemantic` 放入 `Project::historyDatabases`。仅 `category=0` 的历史对象会参加 DB1 匹配：历史 `identifier` 的16字节按 DB1 identity GUID 的字节顺序格式化，且必须与 DB1 中唯一 GUID 精确相等，才加入 `Project::historyObjectAssociations`。不使用数字行 ID 直接配对，不用文件名猜测。

三个固定公开工程样本验证了该字节映射：`steel-detailing` 15/15 条对象与15个DB1 identity匹配；`Steel Fundamentals Modeling Assignment` 750/4,823条对象匹配；`oefening W3` 78/258条对象匹配。W3 样本的历史 type 值与 DB1 当前未恢复的 identity type 字段不能一一对应，因此 API 保留 history type 原值且不宣称类型关联。匹配覆盖率不同也说明不是每条历史记录都对应当前 DB1 快照中的对象。

这项实现覆盖已观察到的 SQLite 表结构，不解码 Tekla 的对象版本快照，不提供修订差异/撤销重建语义，也不推断历史对象删除后与当前 DB1 的关系。遇到新的 Tekla 版本或不同 schema 时应先固定原生样本，再增加适配和字段真值。

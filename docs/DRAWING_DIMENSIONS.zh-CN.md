# DG 直线尺寸与尺寸链

`Drawing::straightDimensions` 和 `straightDimensionSets` 公开 DG 9.54 子类型1、8.44已观察到的子类型1、7.30子类型0、7.64观察到的子类型1及7.82已知子类型0/1的直线尺寸记录与无歧义所属关系。记录保存端点、方向、已存偏移、视图上下文、尺寸集 ID 和子类型码；`Drawing::raw` 继续保留全部字段。7.82重复尺寸 ID 的已知布局记录另由 `duplicateStraightDimensionRecords` 按物理行读取，并带原始文件偏移；ID-keyed map 仍不会替冲突 ID 任选一条。7.82重复尺寸集行由 `duplicateStraightDimensionSetRecords` 保留物理偏移；仅当重复 ID 组的完整 payload 逐字节相同时，map 才暴露该组的唯一值，冲突组仍不入 map。7.64没有独立出图尺寸真值；未链接锚点及其他未验证子类型保持显式诊断。7.30的独立DWG证据与边界见[专门说明](DRAWING_DIMENSIONS_730.zh-CN.md)。8.44只有一个公开图纸样本且没有独立尺寸真值，见[8.44证据](DG_844.zh-CN.md)。

```cpp
tekla::Drawing drawing;
std::string error;
if (tekla::parseDrawing("drawing.dg", drawing, error)) {
    for (const auto& entry : drawing.straightDimensions) {
        const auto& dimension = entry.second;
        const auto view = drawing.viewsByContext.find(dimension.contextId);
        // 旧版歧义或退化视图可能不在唯一视图索引内。
        const auto& set = drawing.straightDimensionSets.at(dimension.dimensionSetId);
        // startPoint/endPoint/upDirection 保留视图中的已存坐标。
        // distance 保留符号；不应用尚未验证的纸面比例转换。
        // projectedLength 有值时仍不等于最终标注文字。
    }
}
```

## 字段与读取边界

偏移从负载首字节起算，不含行标签。

| 类型 | 宽度 | 字段 |
|---|---:|---|
| 256 | 1496（9.54）/1400（7.30、7.64、7.82、8.44） | 类型@0、ID@4、视图上下文@8、尺寸集ID@12、子类型@16、起点3d@24、终点3d@48、方向3d@168、偏移double@192 |
| 257 | 144（9.54）/136（7.30、7.64、7.82） | 类型@0、ID@4、视图上下文@8；其余字段不命名 |

9.54子类型1及7.30子类型0有独立出图证据。7.64和7.82的尺寸表宽、字段数量与已知布局相符；7.64 RABAS 样本的137条尺寸均为 subtype 1，未发现重复ID、断链或缺失视图context。7.64/7.82均没有独立出图真值，因此这些字段属于有边界的跨版本布局支持，不证明纸面精度。未知子类型保留在原始表及`unhandledDimensionRecordIds`，其ID仍列入尺寸链，未套用已知的坐标布局。`dimensionIds`按文件记录顺序保存，不能当成尺寸链的空间顺序。重复尺寸 ID 不进入 ID-keyed map，7.82 中已识别子类型的重复物理行可从 `duplicateStraightDimensionRecords` 读取；这些 ID 仍列于 `unhandledDimensionRecordIds` 以维持旧字段行为。重复尺寸集 ID仍列于`unhandledDimensionSetRecordIds`以维持旧字段行为；7.82 中完整 payload 相同的重复集会在 map 中合并为唯一值，物理行仍可从 occurrence 向量读取，任何字段冲突都不会合并。

已解码坐标要求有限值，ID不能为零。7.30/9.54要求尺寸、尺寸集及链接严格唯一，冲突按格式错误处理；7.82对重复及缺失关联保留歧义和诊断。固定 293 图纸中有 62 条 subtype-0 重复尺寸物理行，分属 31 个重复 ID；payload 彼此不同，因此按偏移分别解码，不去重、不覆盖 ID-keyed map。另有 28 条重复尺寸集行，分属 14 个 ID；每组 136 字节 payload 均逐字节相同，可安全合并语义值并保留两条物理 occurrence。控制测试构造了同 ID、相同已知前缀但未知尾部冲突的尺寸集，确认完整 payload 冲突时不建立 map 项。14 条此前未链接的唯一尺寸因此接入唯一尺寸集。对剩余111条唯一未链接锚点重新扫描其所在图纸的原始 type-257 表，确认它们引用96个不同 ID，且这些 ID 均没有对应物理行；其中0条是 context 不符或尺寸未出现在已解码尺寸集中。它们属于文件内悬空引用，继续明确未链接。悬空/歧义关联诊断列出尺寸ID与所引用的尺寸集ID；context冲突时同时列出两端context。视图上下文歧义不猜选。方向不静默归一化：非单位XY方向或端点Z不一致时，参数仍保留，投影值为空并诊断。

对单位XY方向且端点共面的记录，`projectedLength`计算为端点差在方向垂线上的绝对投影。它不等于两端点的欧氏距离，也不代表最终显示文字；尺寸舍入、单位转换、覆盖值、箭头、字体、引出线、隐藏状态及其他样式字段尚未恢复。偏移保存原值，未将不同版本的比例规则强行统一。

DG 9.54 的 260 视图记录另提供可选 `storedPaperPosition`（负载偏移352/360）与 `storedScaleValue`（偏移384）。它们只表示文件中的存储值；比例标量在已检查的9.54图纸中参与尺寸链对齐的方向并不一致，因此解析器不据此投影、缩放或重绘视图。其他版本不填充这两个字段，原始负载始终保留。

## 独立图纸证据

来源为工程作者的论文 [Tekla Structures Drawing Automation Through Grasshopper in Rhinoceros 3D](https://www.theseus.fi/items/1f6c48ea-7fc0-4b56-8f04-b7093939c35f)，附录2第1和第4页（PDF第58和61页）。已取得并核对的原件大小为11,102,676字节、SHA-256为`b39df60ffb61d5c456ff4e6b60bf508d9e554a73143817c0a2b703c871d8cc5b`。论文和模型均不打包进源码；当前GitHub托管运行器对Theseus的原始下载入口持续返回403，因此仓库语料回归直接使用已固定的DG/DB1样本和从论文转录的期望尺寸，不强制下载这份参考PDF。

先通过DG主体GUID关联DB1，再核对论文中的图号：钢板509825对应1010，装配467268对应C/2。测试使用人工转录的图上尺寸作为独立期望值，不由解析器生成期望值，也不声称测试代码可以解析PDF图形。

| 钢板尺寸记录 | 所属链 | 图上尺寸 |
|---:|---:|---:|
| 2131 | 2130 | 105 |
| 2128 | 2127 | 130 |
| 2048 | 2046 | 70 |
| 2047 | 2046 | 35 |
| 2045 | 2042 | 30 |
| 2044 | 2042 | 70 |
| 2043 | 2042 | 30 |

这7项及4条链与图中总尺寸、水平30/70/30、竖向35/70对应。C/2核对10000、9980、4760、4840、两项450，以及正视图中的20、210、390、155、80、155，共19个独立尺寸值。C/2新增六项分别对应 DG 记录10599、10598、10597、10589、9668、9572。图上舍入后的43、142、9838等值未当作精确浮点真值。

7份9.54 DG共144条尺寸记录、57条链；重复C/8文件是回归样本，不作为独立绘图实验。新增7项语义指纹回归、1项独立图纸对照；合成测试覆盖有符号偏移、斜向投影、非单位/非共面保留、未知子类型、13种无效输入及旧版边界。安装后独立消费项目还会读取合成DG，验证公开类型与链接后的解析行为。

官方 [StraightDimension 属性说明](https://developer.tekla.com/doc/tekla-structures/2025/straight-dimension-properties-50116)与[尺寸链说明](https://developer.tekla.com/doc/tekla-structures/2026/straight-dimension-set-class-69048)佐证端点、方向、偏移和父子关系的语义；它们不提供二进制偏移。

## 纸面变换仍有缺口

论文图纸与模型目录不保证处于同一保存状态：论文C/8含5个视图，现有两份C/8 DG只有4个，不能据此校准它们的纸面变换。

37条起点不同的尺寸链揭示了比例风险：type260@384在D0af文件中以分母方式参与尺寸线对齐，在D452/Deec文件中却以乘数方式参与；单纯按同一公式应用会造成数百纸面单位的差异。两组的版本头、行标签与附近标志相同，不能按比例值是否小于1猜测转换。因此本接口恢复尺寸参数和关联，完整纸面定位、比例、缩短与重绘仍未完成。

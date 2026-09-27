# DG 直线尺寸与尺寸链

`Drawing::straightDimensions` 和 `straightDimensionSets` 公开 DG 9.54 的直线尺寸记录及所属关系。记录保存端点、方向、已存偏移、视图上下文、尺寸链 ID 和子类型码；`Drawing::raw` 继续保留全部字段。7.30/7.82 的尺寸布局尚未验证，保持原始记录及诊断。

```cpp
tekla::Drawing drawing;
std::string error;
if (tekla::parseDrawing("drawing.dg", drawing, error)) {
    for (const auto& entry : drawing.straightDimensions) {
        const auto& dimension = entry.second;
        const auto& view = drawing.viewsByContext.at(dimension.contextId);
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
| 256 | 1496 | 类型@0、ID@4、视图上下文@8、尺寸链ID@12、子类型@16、起点3d@24、终点3d@48、方向3d@168、偏移double@192 |
| 257 | 144 | 类型@0、ID@4、视图上下文@8；其余字段不命名 |

子类型1已有证据。未知子类型保留在原始表及`unhandledDimensionRecordIds`，其ID仍列入尺寸链，未套用已知的坐标布局。`dimensionIds`按文件记录顺序保存，不能当成尺寸链的空间顺序。

已解码字段要求有限值，ID不能为零或在同类记录中重复；所属链、视图必须存在，链与尺寸的上下文必须一致。异常读取清空结果并返回错误。方向不静默归一化：非单位XY方向或端点Z不一致时，参数仍保留，投影值为空并诊断。

对单位XY方向且端点共面的记录，`projectedLength`计算为端点差在方向垂线上的绝对投影。它不等于两端点的欧氏距离，也不代表最终显示文字；尺寸舍入、单位转换、覆盖值、箭头、字体、引出线、隐藏状态及其他样式字段尚未恢复。偏移保存原值，未将不同版本的比例规则强行统一。

## 独立图纸证据

来源为工程作者的论文 [Tekla Structures Drawing Automation Through Grasshopper in Rhinoceros 3D](https://www.theseus.fi/bitstream/handle/10024/920230/Krozanovski_Edgar.pdf?sequence=2&isAllowed=y)，附录2第1和第4页（PDF第58和61页）。PDF的来源、11,102,676字节大小及SHA-256固定在`tests/corpus.json`；论文和模型均不打包进源码。

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

这7项及4条链与图中总尺寸、水平30/70/30、竖向35/70对应。C/2另核对10000、9980、4760、4840及两项450，共13个独立尺寸值。图上舍入后的142、9838等值未当作精确浮点真值。

7份9.54 DG共144条尺寸记录、57条链；重复C/8文件是回归样本，不作为独立绘图实验。新增7项语义指纹回归、1项独立图纸对照；合成测试覆盖有符号偏移、斜向投影、非单位/非共面保留、未知子类型、13种无效输入及旧版边界。安装后独立消费项目还会读取合成DG，验证公开类型与链接后的解析行为。

官方 [StraightDimension 属性说明](https://developer.tekla.com/doc/tekla-structures/2025/straight-dimension-properties-50116)与[尺寸链说明](https://developer.tekla.com/doc/tekla-structures/2026/straight-dimension-set-class-69048)佐证端点、方向、偏移和父子关系的语义；它们不提供二进制偏移。

## 纸面变换仍有缺口

论文图纸与模型目录不保证处于同一保存状态：论文C/8含5个视图，现有两份C/8 DG只有4个，不能据此校准它们的纸面变换。

37条起点不同的尺寸链揭示了比例风险：type260@384在D0af文件中以分母方式参与尺寸线对齐，在D452/Deec文件中却以乘数方式参与；单纯按同一公式应用会造成数百纸面单位的差异。两组的版本头、行标签与附近标志相同，不能按比例值是否小于1猜测转换。因此本接口恢复尺寸参数和关联，完整纸面定位、比例、缩短与重绘仍未完成。

# qskcomp 组件注意事项

## 日志组件族（LogModel + LogListView）

- `logmodel.{h,cpp}` 与 `loglistview.{h,cpp}` 自包含于 qskcomp（曾驻 anystik/src，`git mv` 迁入）
- LogModel 用法不变：`instance()` 单例 / 私有 `new LogModel(this)` / `append/clear` / `entryAdded/cleared`
- LogListView = 过滤条(级别+搜索) + 滚动列表 + 工具行(计数/复制/清空)；取消/关闭由宿主自摆
- ✅ 两个滚动坑已固化在组件内：`setScrolledItem(m_listBox)`（滚动内容须显式声明）、
  内容盒 `setSizePolicy(Expanding, Minimum)`（可长不可缩→内容超视口出滚动条/否则填满）
- 宿主仅需 `buildRow(LogModel::Entry)` → `LogListView::RowItem`（level/tag/message/text/color），
  `appendItem` / `clearItems`，`copyClicked(text)`（复制）与 `clearClicked()` 只发信号由宿主处理
- 计数文案/级别下拉文案经 `setCountLabelFormat("..%1 / %2..")`、`setLevelComboOptions()` 配置
- 行为开关: `setAutoScroll(false)`（不滚底）、`setListPreferredHeight(-1)`（填满）、
  `setCopyButtonVisible/setClearButtonVisible/setCountLabelVisible(false)`
- 宿主实例: anystik/src/logpage.cpp、anystik/src/syncprogresspopup.cpp
- 注意: `setPlaceholderText` 在 QskTextField 上（非 QskTextInput）；宿主调组件返回控件方法须含对应头文件

## QSKinny 文本输入信号（易踩坑）

- `QskAbstractTextInput::textChanged()` 是【无参】纯属性通知信号
- 携带用户输入的是 `textEdited(const QString&)`
- ❌ 不要 `connect(field, &QskTextField::textChanged, 带参lambda)`
  → macOS clang 因重载解析报 makeCallableObject 错
- ✅ 用户编辑用 textEdited；仅需"内容变了"用无参 textChanged()
- 同类实例：anystik/src/settingspage.cpp:657、logpage.cpp:74

## QSKinny 滚动视图内容不显示（已踩坑）

- 症状: QskScrollView 内容（如 QskLinearBox 行）全部空白，但数据计数正常；无滚动条，所有平台一致
- 原因: QskScrollView 不会自动采纳子项为滚动内容（scrolledItem==nullptr → scrollableSize 恒 0）
- ✅ 显式声明 `scrollView->setScrolledItem(m_listBox)`（QskScrollArea 的公开 API）
- ✅ 若持有的是 QskScrollView*：`static_cast<QskScrollArea*>(sv)->setScrolledItem(box)`
- 同类实例: anystik/src/syncprogresspopup.cpp（日志区）、anystik/src/logpage.cpp(:86)

### 内容盒尺寸策略（resizable 默认开时仍会压扁）

- 坑: 内容盒默认垂直 Expanding ⇒ 尺寸被钳到视口高 ⇒ 行被压扁重叠、滚动条永不出现（内容高=视口高）
- ✅ 内容盒（垂直滚动内容）设 `setSizePolicy(Expanding, Minimum)`：可长不可缩
  → 内容超视口保持内容高（出滚动条）；内容矮时填满视口
- 同类实例: 同上两文件（m_listBox）

## QSKinny 文本输入框后半高字（易踩坑）

- 症状: mac/Android 下 QskTextInput/QskTextField 编辑文字只显下半截（字形顶部被裁）；placeholder 正常，Linux 正常
- 原因: 编辑态文字由内嵌 QQuickTextInput 渲染，几何=subControlRect(Text)（字段高−两层padding），
  大字体/高 dpr 下内容区<行高 → 顶裁；setPreferredHeight(40) 不够，必须 setFixedHeight 锁死
- ✅ 所有输入框统一 `field->setFixedHeight(30)`
- 同类实例: anystik/src/settingspage.cpp:214/:227/:295/:308/:322（Gotify2 + DAV3）

## QSKinny 弹出层透明无背景（易踩坑）

- QskPopup 本身透明，内容直接挂 popup 根部 = 完全穿透、无背景
- ❌ 控件直接 `new QskXxx(..., popup)`
- ✅ 不透明 QskBox 面板托管布局，消除透明感：

```cpp
setOverlay(true);                          // 全屏压暗遮罩,增强对话框感
auto* panel = new QskBox(this);
panel->setBoxShapeHint(QskBox::Panel, QskBoxShapeMetrics(14, Qt::AbsoluteSize));
auto* layout = new QskLinearBox(Qt::Vertical, panel);
layout->setMargins(18);
```

- 同类实例：anystik/src/dialogpopup.cpp:43、migrationdialog.cpp:49、toastpopup.cpp:59
- 漏网修复：anystik/src/syncprogresspopup.cpp（进度弹窗原无面板 → 全透明）

## QSKinny 弹窗不自动布局子项（已多次踩坑）

- QskPopup **不会**自动给直接子项分配几何 → 只 `new QskBox(popup)` 挂面板仍全透明（面板无几何无从绘制，syncprogresspopup 曾误改一次仍透明）
- ✅ 范式（dialogpopup/toastpopup/migrationdialog 同）：
  - `setPolishOnResize(true) + setPolishOnParentResize(true)`
  - `protected: void updateLayout() override` → 私有 `updateGeometry()`
  - `updateGeometry()` 手动 `setGeometry(popupRect)` + `m_panel->setGeometry(...)`（居中、`qBound` 限宽高）
  - `updateLayout()` 里 `m_layout->setGeometry(layoutRect())`
  - open 后补一次 `QTimer::singleShot(0, ...updateGeometry)`
# qskcomp 组件注意事项

## QSKinny 文本输入信号（易踩坑）

- `QskAbstractTextInput::textChanged()` 是【无参】纯属性通知信号
- 携带用户输入的是 `textEdited(const QString&)`
- ❌ 不要 `connect(field, &QskTextField::textChanged, 带参lambda)`
  → macOS clang 因重载解析报 makeCallableObject 错
- ✅ 用户编辑用 textEdited；仅需"内容变了"用无参 textChanged()
- 同类实例：anystik/src/settingspage.cpp:657、logpage.cpp:74

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
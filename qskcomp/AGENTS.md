# qskcomp 组件注意事项

## QSKinny 文本输入信号（易踩坑）

- `QskAbstractTextInput::textChanged()` 是【无参】纯属性通知信号
- 携带用户输入的是 `textEdited(const QString&)`
- ❌ 不要 `connect(field, &QskTextField::textChanged, 带参lambda)`
  → macOS clang 因重载解析报 makeCallableObject 错
- ✅ 用户编辑用 textEdited；仅需"内容变了"用无参 textChanged()
- 同类实例：anystik/src/settingspage.cpp:657、logpage.cpp:74
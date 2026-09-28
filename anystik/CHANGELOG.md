# Changelog

## [0.6] — 2026-09-28（v0.5 起 75 项提交）

### 新增
- 手机号/短信浮层、短信计数、移动网络状态监视（mob net monitor、phone/sms 弹窗）
- 贴纸生成（stikgen）：文生图/图生图提示词、深度求索预览贴纸 19、AI 视觉候选、图转文字、Bing 图片描述、图片临时上传站点扩展
- 贴纸描述编辑：多行文本组件（qskcomp/multilinetextedit，真换行/中文 IME 全原生）与描述编辑三阶段、自动生成描述、描述弹窗宽度调整
- 照片/贴纸包管理：eif 格式包支持与解包、排序、在线包（online packs）、多个内置包（nailong/深按/tgs×2 及解析器）、包探测(版本/MD5)
- 以图搜图（search similar image）：相似图搜索、搜索结果网格、子菜单级联、搜索相似浮层进度/状态
- 日志重构：logmodel/loglistview 迁入 qskcomp、日志页/同步弹窗瘦身复用
- 同步：同步(Single)处理器清理、上传/下载速率与状态显示、进度条空内容修复
- 文本输入等高修复、弹窗透明度、弹窗 ESC/关闭/返回、级联子菜单、滚条淡出/自显
- 代码混淆（advobfus）与发布构建、c++23 切换、C 源目录整理

### 修复
- droid 输入法弹窗 resize 主窗、droid 崩溃（crox）
- gif 缩放复制、缩略图列表分辨率、输入框行高、日志列表空内容视图

### 基础设施
- qskcomp 组件库目录（组件迁移、qskcomp.cmake、AGENTS 踩坑记录）
- 移除 bin/.qm、脚本/工程文件整理（impr proj file）

## Unreleased（工作树，未提交）
- 结果区只读（拦截式 readonly：禁改文本但可选中/复制/移光标，对齐 Qt 惯例）
- 页面焦点链激活（Page StrongFocus + 切页 forceActiveFocus）
- MultiLineTextEdit 鼠标事件原生化（AllButtons + focusOnPress）
- 版本号 bump 0.6

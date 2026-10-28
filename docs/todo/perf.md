# perf · 性能优化

[返回待办总览](README.md) · 更新：2026-10-28

来源：2026-10-22 前端代码审查（10.1–10.3），聚焦 QML 加载、编译与曲库列表性能；2026-10-25 运行时内存分析（10.4–10.6）。

保留原条目编号，优先级与工作量说明见[总览](README.md)。

## 清单

| 状态 | 优先级 | 项目 | 分类 | 原估算 |
| --- | --- | --- | --- | --- |
| 待开发 | P2 | QML 模块化与编译期检查 | 性能 | M–L |
| 待开发 | P2 | 曲库多选与列表模型性能 | 性能 | S–M |

## 详细说明

### 10.1 QML 模块化与编译期检查 — P2 · M–L

- 现状：`frontend/qt-qml/CMakeLists.txt` 以 `resources.qrc` 打包 QML，`src/main.cpp` 通过 `setContextProperty` 注入 `i18n`、控制器等全局对象；qmlcachegen／qmlsc 不生效，qmllint 无法解析全局名称，绑定只能运行时解释。
- 方案：迁移到 `qt_add_qml_module`，控制器改为 `QML_SINGLETON` 或 required property；CI 增加 `all_qmllint`。
- 注意：迁移会改变 `AGENT.md` 中“新增 QML 同步 `resources.qrc`”的约定，需同步文档。

### 10.2 曲库多选与列表模型性能 — P2 · S–M

- `qml/components/LibraryPanel.qml:52-54`、`:323`、`:330`：每行执行 `selectedSongIds.indexOf`，全选判断为 `every` 套 `indexOf`，选择变化时全部行重算；每行还通过 `songActions(modelData)` 新建菜单数组。改为 C++ 侧 `QSet` 维护选择并以 role 或方法暴露，全选状态由控制器计算，菜单在打开时生成。
- `src/models/record_model.cpp:12-22`（已复核）：常规增量更新接近 O(n)，整体重排（如切换排序）退化为 O(n²)，且每次比较都 `toMap()` 复制。可先建标识到行号的索引，并缓存标识值。
- 验收：数千首曲库切换排序、全选、批量选择不卡顿；用 `QAbstractItemModelTester` 覆盖 `RecordModel` 增删改移。

10.3 页面按需加载已于 2026-10-28 完成并从清单移除，实现及验证范围见[验收记录](acceptance.md#2026-10-28-页面按需加载)。

## 内存分析记录（2026-10-25）

对象：`./build/nekotune`（`CMAKE_BUILD_TYPE` 为空），连续运行约 13.5 小时后 RSS 约 1.0 GB、峰值 `VmHWM` 1.16 GB；另有集显驱动报告约 237 MB 系统内存（`drm-total-system0`），不计入 RSS。方法：`malloc_info`，dump 匿名映射后遍历 glibc chunk，按 vtable 和内容特征归类。

| 类别 | 大小 | 说明 |
| --- | --- | --- |
| malloc 空闲但未归还 | 333 MB | 其中约 184 MB 是已播放过的交错立体声 float PCM，散落在 9 个线程 arena，每个 16–25 MB，各自在用不足 3 MB |
| malloc 在用 | 318 MB | 主 heap 约 119 MB（UI／模型，带 vtable 的 Qt 对象仅 9.4 MB）、图片像素约 45 MB、当前／预载 PCM 约 45 MB、来源不明的近全零大块约 37 MB |
| 文件映射 | 277 MB | Noto CJK 字体 136 MB 等，可回收、可共享，属正常 |
| JS 堆及其他 | 73 MB | `JSGCHeap` 37 MB，正常 |

复查脚本曾放在 `/tmp/nekoana/`（tmpfs，重启后丢失）。以上为修复前采样，来源不明的 37 MB 全零大块仍需后续定位。

10.4 播放解码缓冲、10.5 壁纸与背景图解码尺寸、10.6 扩展视图引擎复用已于 2026-10-25 完成并从清单移除，实现及验证范围见[验收记录](acceptance.md#2026-10-25-内存优化)。

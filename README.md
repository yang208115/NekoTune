# NekoTune

NekoTune 是一款本地优先的 Linux 音乐播放器，采用前后端解耦架构：C++ 后端负责播放、队列和 IPC，客户端可以按需替换。

## 当前状态

当前仓库已经包含第一版可运行骨架：

- `nekotune`：统一桌面程序，在同一个进程内启动后端、IPC 服务和 Qt6/QML 前端
- `nekotune-backend`：可选的独立 C++20 后端进程，提供基于 Unix domain socket 的 JSON-RPC API
- `nekotune-qt-qml`：可选的独立 Qt6/QML 桌面前端，只通过后端 API 交互
- 通过 Qt Multimedia 支持基础本地文件播放
- 使用 SQLite 按音频内容 hash 管理歌曲记录，保存自定义歌名、作者名和歌词，并在启动时恢复已有歌曲
- 文件选择器默认显示 `mp3`、`m4a`、`aac`、`wav`、`flac`、`ogg`；实际能否解码取决于本机 Qt Multimedia 后端和系统音频编解码插件
- 支持独立歌单：创建、重命名、删除、导入音乐、从队列加入、移除歌曲和整单播放；同一首歌可加入多个歌单
- 已支持队列、单曲播放/移除、状态查询、进度、跳转、音量、上一首、下一首、停止和暂停控制
- 播放时由后端按同目录 `.lrc`、歌词缓存、自定义歌词、LRCLIB 顺序加载，支持同步歌词、高亮、手动候选选择和离线缓存
- 手动搜索歌词可选 LRCLIB 或酷狗音乐；酷狗依次选择歌曲版本和歌词，下载的 LRC 会缓存到当前歌曲

后端 API 边界会保持稳定，方便后续接入 Qt/QML、Web、GTK、终端或移动端客户端。

## 构建

```bash
cmake -S . -B build
cmake --build build
```

## 运行

桌面使用时只需启动统一程序：

```bash
./build/nekotune
```

它会在同一个进程内创建播放器和 IPC 服务，再加载 QML 界面，因此不需要手动分别启动前端和后端。排查歌词时间轴时，可以直接传入诊断参数：

```bash
./build/nekotune --lyrics-debug
```

使用 `--lyrics-debug` 启动后，侧边栏会显示「歌词调试」入口，可查看当前歌词时间轴、毫秒时间戳和播放进度，点击歌词行可以跳转试听。普通启动不显示该入口。

如果需要运行无界面的后端，或调试可替换客户端，仍可以分别启动：

```bash
./build/backend/nekotune-backend
./build/frontend/qt-qml/nekotune-qt-qml
```

两个进程使用相同的 Qt 本地 socket 名称：

- 如果设置了 `NEKOTUNE_SOCKET`，优先使用该值
- 否则使用默认名称 `nekotune`

在 Linux 上，Qt 会将它映射到底层 Unix domain socket，通常位于 `/tmp` 下。
统一程序仍保留这个 IPC 边界，所以外部客户端和现有 JSON-RPC 调试方式无需改变。

`build/nekotune` 是包含 QML、图片和语言文件的单个应用可执行文件；它仍依赖系统安装的 Qt、Multimedia/FFmpeg 插件和其他动态库。若要连同这些运行库一起分发，需要制作 AppImage 或使用静态 Qt 构建。

开发环境默认 SQLite 数据库位于 `build/nekotune.sqlite3`，避免写入用户数据目录。可以通过 `NEKOTUNE_DB_PATH=/path/to/nekotune.sqlite3` 显式指定数据库位置。

队列会和歌曲资料一起保存在 SQLite 中，重启后恢复上次的队列顺序、重复项和当前项。清空队列后，历史歌曲资料仍会保留，但不会再次自动进入队列。

歌单独立保存在 SQLite 中，清空队列不会清空歌单。点击顶部的歌单名称浏览内容，使用「播放歌单」按歌单顺序生成播放队列；在歌单内「添加音乐」只导入该歌单。删除歌单不影响当前队列、其他歌单或本地音乐文件。旧版文件夹首次启动时自动迁移为歌单，嵌套名称保留为「父级 / 子级」，原队列顺序和当前项保持不变。

前端支持中文和英文国际化，默认跟随系统语言；也可以在界面右上角直接切换，或通过 `NEKOTUNE_LANGUAGE=zh` / `NEKOTUNE_LANGUAGE=en` 在启动时指定语言。

## 歌词

自动加载顺序为同目录同名 `.lrc` → 本地歌词缓存 → 歌曲自定义歌词 → LRCLIB。同名 `.lrc` 优先，包括普通文本。点击「刷新」会跳过缓存和自定义歌词，重新搜索 LRCLIB。

手动打开「搜索歌词」后可选择 LRCLIB 或「酷狗音乐」。酷狗会先列出歌曲版本，再列出该版本的歌词候选；选定歌词后才下载并缓存。酷狗搜索使用匿名的 [酷狗歌词 Worker](https://kugou-lyrics-api.lyuy.workers.dev)，无需登录或 Cookie，仅在手动选择酷狗时访问它。「仅本地歌词」模式不会发出网络请求。

酷狗歌曲版本会显示封面缩略图；选定歌词后，封面也会显示在播放器中。封面图片保存在本地磁盘缓存，歌词缓存保留封面地址。

旧版数据库中的识别任务和转写记录、本地 `.asr.json` 文件及原有设置文件不会被删除；此版本不再读取或生成 ASR 歌词。

## 路线图

Milestone 1 聚焦最小可播放产品。当前实现使用 Qt Multimedia 作为第一版播放适配层；后续可以把解码和音频输出迁移到独立的 FFmpeg、PipeWire 模块，而不影响已有前端客户端。

# NekoTune

NekoTune 是一款本地优先的 Linux 音乐播放器，采用前后端解耦架构：C++ 后端负责播放、队列和 IPC，客户端可以按需替换。

## 当前状态

当前仓库已经包含第一版可运行骨架：

- `nekotune-backend`：C++20 后端进程，提供基于 Unix domain socket 的 JSON-RPC API
- `nekotune-qt-qml`：Qt6/QML 桌面前端，只通过后端 API 交互
- 通过 Qt Multimedia 支持基础本地文件播放
- 使用 SQLite 按音频内容 hash 管理歌曲记录，保存自定义歌名、作者名和歌词，并在启动时恢复已有歌曲
- 文件选择器默认显示 `mp3`、`m4a`、`aac`、`wav`、`flac`、`ogg`；实际能否解码取决于本机 Qt Multimedia 后端和系统音频编解码插件
- 已支持队列、单曲播放/移除、状态查询、进度、跳转、音量、上一首、下一首、停止和暂停控制

后端 API 边界会保持稳定，方便后续接入 Qt/QML、Web、GTK、终端或移动端客户端。

## 构建

```bash
cmake -S . -B build
cmake --build build
```

## 运行

先启动后端：

```bash
./build/backend/nekotune-backend
```

再启动 Qt/QML 前端：

```bash
./build/frontend/qt-qml/nekotune-qt-qml
```

两个进程使用相同的 Qt 本地 socket 名称：

- 如果设置了 `NEKOTUNE_SOCKET`，优先使用该值
- 否则使用默认名称 `nekotune`

在 Linux 上，Qt 会将它映射到底层 Unix domain socket，通常位于 `/tmp` 下。

开发环境默认 SQLite 数据库位于 `build/nekotune.sqlite3`，避免写入用户数据目录。可以通过 `NEKOTUNE_DB_PATH=/path/to/nekotune.sqlite3` 显式指定数据库位置。

队列会和歌曲资料一起保存在 SQLite 中，重启后恢复上次的队列顺序、重复项和当前项。清空队列后，历史歌曲资料仍会保留，但不会再次自动进入队列。

前端支持中文和英文国际化，默认跟随系统语言；也可以在界面右上角直接切换，或通过 `NEKOTUNE_LANGUAGE=zh` / `NEKOTUNE_LANGUAGE=en` 在启动时指定语言。

## 路线图

Milestone 1 聚焦最小可播放产品。当前实现使用 Qt Multimedia 作为第一版播放适配层；后续可以把解码和音频输出迁移到独立的 FFmpeg、PipeWire 模块，而不影响已有前端客户端。

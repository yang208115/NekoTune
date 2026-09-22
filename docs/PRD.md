# NekoTune 音乐播放器 PRD

## 产品定位

NekoTune 是一款本地优先的 Linux 桌面音乐播放器。

目标：

- 提供流畅、轻量、可扩展的音乐播放体验。
- 使用 C++ 构建核心播放后端。
- 前端与后端完全解耦，便于未来替换 Qt/QML、Web、终端或其他客户端。

产品理念：

> Local first, lightweight, extensible.

## 架构

```text
Frontend Layer
      |
IPC / API Interface
      |
C++ Backend
      |
--------------------------------
Decoder       Library      Audio Engine
FFmpeg        SQLite       PipeWire/ALSA
```

前端负责 UI 展示、用户交互和状态展示。后端负责音频播放、队列、音乐库数据、持久化和 Linux 系统集成。

播放队列与歌曲资料分开管理：歌曲资料长期保存在 SQLite，队列保存顺序、重复项、文件路径和当前项。后端重启后恢复最近一次队列，用户清空队列后历史歌曲不会自动重新加入。

## 开发阶段

### Milestone 1

- C++ 后端
- MP3/WAV 播放
- Qt/QML 前端
- 基础播放控制

### Milestone 2

- 本地音乐库
- 目录扫描
- SQLite
- 搜索
- 播放列表

### Milestone 3

- PipeWire 优化
- MPRIS
- 通知
- 媒体快捷键

### Milestone 4

- 插件系统
- Web 前端
- 远程控制
- 主题系统

## 成功标准

- 用户可以自由替换前端。
- C++ 后端可以稳定运行。
- 支持 Linux 主流音频环境。
- 项目可以作为长期维护的开源项目继续发展。

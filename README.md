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
- 播放时由后端按本地 `.lrc`、同名 `.asr.json`、数据库 ASR、缓存、LRCLIB 顺序加载歌词，支持同步歌词、高亮、手动候选选择和离线缓存

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

使用 `--lyrics-debug` 启动后，侧边栏会显示「歌词调试」入口，点击后在主内容区打开独立调试页，底部播放控制始终可用。普通启动不显示该入口，也不加载调试页。调试页可切换「当前时间轴」和「ASR / LRC 对比」。对比模式并排显示当前歌曲的 ASR 与 LRC 原文、毫秒时间戳和来源，随同一播放进度分别高亮、滚动；点击任一行可跳转试听。上方显示当前两句起点的 `ASR − LRC` 差值，仅用于观察时间偏差，不代表两句已经按文字匹配。

同名 `.lrc` 存在时，仍可对比同名 `.asr.json` 或数据库中的 ASR；也会保留可用的缓存/自定义 LRC，以及本次手动选择的 LRC 或导入/识别的 ASR。切歌或刷新时重新收集当前歌曲的数据。缺少一份歌词时面板显示获取提示，进入对比模式不会触发网络识别，也不改变正常歌词的加载优先级。

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

## 阿里云 ASR 歌词

在左侧「设置」中填写**北京地域的阿里云百炼 API Key**并保存。留空保存会保留已有密钥，也可以单独清除密钥。

播放歌曲后，切到歌词页点击「ASR 识别」，在弹窗中选择本次识别模型和语言，再点击「开始上传识别」。每次打开默认 `fun-asr` 和日语，也可选择自动识别、中文、英语或韩语；取消弹窗不会上传，模型和语言不会保存为全局设置。后端以 Qt/C++ 执行获取上传凭证、上传当前本地音频、提交所选模型的任务、轮询和下载结果，无需 Python 环境。界面显示各阶段状态；成功后自动显示逐词歌词。音频会上传至阿里云，并按账号规则计费，仅在手动点击时发起，不会自动识别整个队列。

可选模型：Fun-ASR（`fun-asr`，默认）、Qwen-Audio 3.1/3.0 ASR Flash Filetrans（`qwen-audio-3.1-asr-flash-filetrans` / `qwen-audio-3.0-asr-flash-filetrans`）、Qwen3 ASR Flash Filetrans（`qwen3-asr-flash-filetrans`）、Paraformer v2（`paraformer-v2`）。上传凭证和识别任务使用同一模型。Qwen3 按[官方 API](https://help.aliyun.com/en/model-studio/qwen-asr-api-reference)使用单个 `file_url`、`language` 和 `output.result`，并开启 `enable_words` 以返回逐词时间戳；其他模型使用 `file_urls`、`language_hints` 和 `output.results`。统一识别首个声道，原始结果继续存入数据库。

支持最多 512 MiB 的本地音频，单次网络请求最长 60 秒、整体任务最长 30 分钟。点击「停止等待」、切歌、刷新/重新选择歌词或开启歌词离线模式会停止本地请求与轮询；已经提交的云端任务可能继续执行。此功能沿用脚本的临时 OSS 上传方式，遵循[阿里云上传接口](https://www.alibabacloud.com/help/zh/model-studio/get-temporary-file-url)和 [Fun-ASR HTTP 接口](https://help.aliyun.com/en/model-studio/fun-asr-recorded-speech-recognition-http-api)。

控制台默认输出 `nekotune.asr` 和 `nekotune.lyrics` 日志：包含请求阶段、耗时、HTTP 状态、Qt 网络错误类型，以及可用的上游错误码和请求 ID。日志不输出 API Key、上传凭证、签名 URL 或歌词正文。重新编译后需重启程序；从终端运行 `./build/nekotune 2>&1 | tee /tmp/nekotune-network.log` 可同时查看和保存日志。

已有的 `transcription.json` 仍可通过「导入 ASR」导入（不超过 2 MiB）。也可将 JSON 命名为 `歌曲名.asr.json` 放在音频同目录。程序使用句子和词的毫秒时间戳滚动、高亮，点击句子跳转；识别文字与标点不自动纠错。多声道取首个有效声道，词对齐不可靠时退回整句高亮。

云端提交成功返回 `task_id` 后，会立即写入同一数据库的 `asr_tasks` 表，保存 `task_id`、`song_hash`、模型、语言和创建时间。每个新任务单独保留，后续识别失败、取消或重启不会清除此备份；同一任务 ID 不重复写入。仅作备份，不用于自动恢复、轮询或重试。备份写入失败会输出控制台警告，但不打断当前识别。

**完整识别 JSON 存入当前 SQLite 数据库的 `song_transcriptions` 表**，以 `song_hash` 关联歌曲，包含原始 `properties`、`transcripts`、词时间戳、置信度等内容；网络识别、手动导入和同名 ASR 文件都走同一入库流程。渲染使用解析后的数据，不把原始 URL/元数据广播到界面。重启或离线时直接从数据库恢复，无需再次识别；旧版 ASR 文件缓存首次读取时也会迁入数据库（旧缓存只能保留当时已存下的字段）。

加载顺序为同名 `.lrc` → 同名 `.asr.json` → 数据库 ASR → 原歌词缓存/自定义歌词 → LRCLIB。同名 `.lrc` 仍优先；手动导入/识别立即应用，后续加载按上述顺序处理。「刷新」会跳过数据库和缓存，但不删除已有 ASR 数据。

API Key 单独保存在数据库同目录的 `nekotune-settings.json`，权限为仅文件所有者读写（`0600`），它是本地明文配置而非加密密钥库。设置查询和播放器状态仅返回是否已配置；本地 IPC socket 也限制为当前用户访问。

## 路线图

Milestone 1 聚焦最小可播放产品。当前实现使用 Qt Multimedia 作为第一版播放适配层；后续可以把解码和音频输出迁移到独立的 FFmpeg、PipeWire 模块，而不影响已有前端客户端。

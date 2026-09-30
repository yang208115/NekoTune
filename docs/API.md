# NekoTune IPC API

NekoTune 使用基于换行分隔的 JSON 协议，通过 Unix domain socket 进行本地 IPC 通信。

## Socket 名称

后端和客户端按以下顺序解析 Qt 本地 socket 名称：

1. `NEKOTUNE_SOCKET`
2. `nekotune`

在 Linux 上，Qt 会将它映射到底层 Unix domain socket，通常位于 `/tmp` 下。

## 请求

```json
{"id":1,"method":"player.status","params":{}}
```

`id` 是可选字段，但建议提供。响应会在存在 `id` 时原样带回，方便客户端匹配请求和响应。

## 响应

成功响应：

```json
{"id":1,"status":"ok","data":{"state":"playing","position":120000,"duration":300000}}
```

错误响应：

```json
{"id":1,"status":"error","message":"No song loaded"}
```

请求必须以换行结束。服务端支持一次读取多个请求，也支持一个请求分多次写入；带有 `id` 的请求会在响应中原样返回该 ID。缺少 `method`、`path`、数值参数或队列项 `id` 时，服务端返回带 ID 的参数错误，不会执行操作。

## 事件

后端会向所有已连接客户端广播事件：

```json
{"event":"player.state_changed","state":"playing"}
```

## 歌曲身份

队列项的 `id` 仍表示本次入队产生的队列项 ID，用于 `queue.play` 和 `queue.remove`。后端现在还会为每首歌返回：

- `song_hash`：文件内容的 SHA-256 hash，用于区分真实歌曲内容
- `song_id`：SQLite 中的持久歌曲 ID；同一份音频内容重复入队会复用同一个 `song_id`
- `custom_title`：用户自定义歌名
- `artist`：用户自定义作者/歌手名
- `lyrics`：用户自定义歌词，当前播放歌曲对象会返回该字段

当前播放歌曲对象还会返回 `queue_id`，它与队列项的 `id` 相同。

## 方法

### `player.play`

播放当前曲目、队列中的第一首曲目，或传入的本地文件路径。

```json
{"id":1,"method":"player.play","params":{"path":"/home/user/Music/song.mp3"}}
```

### `player.pause`

暂停播放。

```json
{"id":2,"method":"player.pause","params":{}}
```

### `player.toggle_play_pause`

播放时暂停，暂停或停止时开始/继续播放当前曲目。

```json
{"id":3,"method":"player.toggle_play_pause","params":{}}
```

### `player.stop`

停止播放。

```json
{"id":3,"method":"player.stop","params":{}}
```

### `player.next`

切换到队列中的下一项。

```json
{"id":4,"method":"player.next","params":{}}
```

### `player.previous`

切换到队列中的上一项。

```json
{"id":5,"method":"player.previous","params":{}}
```

### `player.seek`

跳转到指定播放位置，单位为毫秒。

```json
{"id":6,"method":"player.seek","params":{"position":120000}}
```

### `player.set_volume`

设置音量，取值范围为 `0` 到 `1`。

```json
{"id":7,"method":"player.set_volume","params":{"volume":0.8}}
```

### `player.status`

返回当前播放器状态。

```json
{"id":8,"method":"player.status","params":{}}
```

响应里的 `data.database_path` 是当前 SQLite 数据库文件路径。

### `queue.add`

把本地音频文件添加到播放队列。

```json
{"id":9,"method":"queue.add","params":{"path":"/home/user/Music/song.wav"}}
```

### `queue.play`

播放队列中指定 `id` 的曲目。

```json
{"id":10,"method":"queue.play","params":{"id":1}}
```

### `queue.remove`

从播放队列中移除指定 `id` 的曲目。如果移除的是当前播放曲目，后端会停止当前曲目；当后面还有曲目且播放器此前处于播放状态时，会自动继续播放下一首。

```json
{"id":11,"method":"queue.remove","params":{"id":1}}
```

### `queue.clear`

清空队列并停止播放。

```json
{"id":12,"method":"queue.clear","params":{}}
```

### `queue.status`

返回队列内容和当前播放索引。

```json
{"id":13,"method":"queue.status","params":{}}
```

队列会保存到当前 SQLite 数据库，包含顺序、重复项、文件路径和当前索引。后端重启后恢复最近一次队列；歌曲资料表中的历史歌曲不会自动重新加入队列。

### 歌单 `playlist.*`

歌单是独立于播放队列的持久歌曲集合，没有文件夹嵌套。同一 `song_id` 可以加入多个歌单，在同一歌单中只保留一次；再次添加会更新文件路径并保留原顺序。歌单中的歌曲使用 `song_id` 标识，不使用临时的队列项 ID。

- `playlist.list`：返回 `data.playlists`，每个歌单包含 `id`、`name`、有序的 `items`；歌曲包含 `song_id`、`song_hash`、`path`、`first_path`、`title`、`custom_title` 和 `artist`。
- `playlist.create`：传入 `name`，成功返回 `data.playlist_id` 和完整歌单列表。名称去除首尾空白后长度为 1–128 字符。
- `playlist.rename` / `playlist.delete`：通过歌单 `id` 改名或删除。删除歌单不影响队列、其他歌单、歌曲资料或本地音乐文件。
- `playlist.add`：传入歌单 `id` 和本地 `path`，或已有队列项的 `queue_id`。导入仅影响歌单，不自动入队或播放。
- `playlist.remove`：传入歌单 `id`、`song_id`，仅移除该歌单中的关联。
- `playlist.play`：传入歌单 `id`，按歌单顺序替换队列并播放；可选 `song_id` 指定从哪首开始。空歌单、失效 ID、缺失文件或队列保存失败时返回错误并保留原队列。

```json
{"id":14,"method":"playlist.create","params":{"name":"现场录音"}}
{"id":15,"method":"playlist.add","params":{"id":1,"queue_id":3}}
{"id":16,"method":"playlist.add","params":{"id":1,"path":"/home/user/Music/live.flac"}}
{"id":17,"method":"playlist.play","params":{"id":1}}
{"id":18,"method":"playlist.rename","params":{"id":1,"name":"Live"}}
{"id":19,"method":"playlist.remove","params":{"id":1,"song_id":2}}
{"id":20,"method":"playlist.delete","params":{"id":1}}
```

`player.status` 和首次连接事件包含 `playlists`；歌单改动、歌单歌曲资料更新通过 `playlist.changed` 广播完整 `playlists`。`queue.changed` 仅更新队列。清空队列不清空歌单。

旧版本的队列文件夹会在启动时通过事务自动迁移为歌单，嵌套名称展开为 `父级 / 子级`，每个歌单保留原文件夹直接包含的歌曲和顺序；原队列及其重复项保持不变。旧 `queue.folder.*` 方法及 `folders`、`folder_id` 响应字段已移除。

### `song.metadata`

读取指定歌曲的自定义元数据。

```json
{"id":14,"method":"song.metadata","params":{"song_id":1}}
```

### `song.update_metadata`

更新指定歌曲的用户自定义元数据。`custom_title` 也可用 `title` 传入，`artist` 也可用 `author` 传入。未传入的字段会保持原值。

```json
{"id":15,"method":"song.update_metadata","params":{"song_id":1,"custom_title":"自定义歌名","artist":"作者名","lyrics":"歌词内容"}}
```

## 歌词

后端负责解析、搜索和缓存歌词，Qt/QML 客户端只通过 IPC 获取结果。播放曲目后按“同目录同名 `.lrc` → 本地歌词缓存 → 歌曲自定义歌词 → LRCLIB”顺序加载。缓存文件位于 `QStandardPaths::AppDataLocation/lyrics-cache`，以音频内容 hash（或标题、歌手、专辑和时长）为键，由 `QSaveFile` 原子写入。刷新跳过缓存和自定义歌词；同目录 `.lrc` 仍优先。

`lyrics.changed` 广播当前歌曲的 `track_id`、`revision`、`state` 和可选 `document`、`candidates`。`document.lines` 为 `{time_ms,text}` 数组，普通歌词位于 `document.plain_text`。酷狗候选及选定歌词的 `cover_url` 为可选封面地址。状态包括 `loading`、`waiting_metadata`、`searching`、`ready`、`instrumental`、`not_found`、`offline`、`error` 和 `candidates`。

歌词请求携带当前歌曲的 `track_id`（音频内容 hash）：

- `lyrics.refresh`：忽略缓存和自定义歌词，重新获取当前歌曲歌词。
- `lyrics.search`：按 `title`、`artist`、`album` 手动搜索；`source` 为 `lrclib`（默认）或 `kugou`。
- `lyrics.select`：按当前字符串 `revision` 和候选 `index` 选择结果。酷狗先选歌曲版本，再选歌词；第二次选择后才下载 LRC。
- `lyrics.set_offline`：设置离线模式；仍可读取同名 LRC、缓存和自定义歌词，不发起网络请求。

自动结果仅在标题、歌手、专辑和时长满足精确匹配（时长误差不超过 2 秒）且候选明显领先时直接应用；其余结果交给客户端选择。旧版数据库的 ASR 表不会被删除，但后端不再读取或写入其中的转写结果。

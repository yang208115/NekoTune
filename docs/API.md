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

### `queue.folder.create` / `queue.folder.rename`

创建或重命名播放列表文件夹。`parent_id` 为 `0` 时放在根目录，因此可以创建多层文件夹。

```json
{"id":14,"method":"queue.folder.create","params":{"name":"现场录音","parent_id":0}}
{"id":15,"method":"queue.folder.rename","params":{"id":1,"name":"Live","parent_id":0}}
```

### `queue.folder.move` / `queue.folder.move_item`

移动文件夹或歌曲。移动文件夹会阻止形成循环；删除文件夹时，内容会移到它的上一级。

```json
{"id":16,"method":"queue.folder.move_item","params":{"id":3,"parent_id":1}}
{"id":17,"method":"queue.folder.move","params":{"id":2,"parent_id":1}}
```

### `queue.folder.delete`

删除文件夹并将歌曲和子文件夹移到上一级，不会删除本地音乐文件。

```json
{"id":18,"method":"queue.folder.delete","params":{"id":1}}
```

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

后端通过 LRCLIB 获取歌词，Qt/QML 客户端不会直接访问歌词服务。播放曲目后，后端按“同目录同名 `.lrc` → 本地歌词缓存 → LRCLIB”顺序加载；缓存文件位于 `QStandardPaths::AppDataLocation/lyrics-cache`，也可以由 `LyricsCache` 调用方指定目录。缓存采用内容 hash（或标题、歌手、专辑和时长）作为键，并通过 `QSaveFile` 原子替换。

歌词状态通过 `lyrics.changed` 事件广播，`lyrics.state` 可能为 `loading`、`waiting_metadata`、`searching`、`ready`、`instrumental`、`not_found`、`offline`、`error` 或 `candidates`。`lyrics.document.lines` 是后端解析后的 `{time_ms,text}` 数组，普通歌词在 `lyrics.document.plain_text` 中。

歌词相关请求都需要带当前歌曲的 `track_id`（音频内容 hash）：

- `lyrics.refresh`：忽略缓存并重新获取当前歌曲歌词。
- `lyrics.search`：按传入的 `title`、`artist`、`album` 手动搜索。
- `lyrics.select`：按 `revision` 和候选 `index` 应用搜索结果。
- `lyrics.set_offline`：设置离线模式。离线时仍会读取同名 LRC 和已有缓存，不发起网络请求。

自动结果只有在标题、歌手、专辑和时长满足精确匹配（时长误差不超过 2 秒）且候选明显领先时才会直接应用；其余情况通过 `lyrics.changed` 的 `candidates` 数组交给客户端选择。网络失败、未找到歌词和无效响应会使用不同状态，均不会改变播放器播放状态。

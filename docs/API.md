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

- `song_hash`：本地文件内容的 SHA-256；在线曲目为空
- `song_id`：SQLite 中的持久歌曲 ID；同一份音频内容重复入队会复用同一个 `song_id`
- `custom_title`：用户自定义歌名
- `artist`：用户自定义作者/歌手名
- `lyrics`：用户自定义歌词，当前播放歌曲对象会返回该字段
- `cover_url`：统一解析的本地优先封面 URL，各歌曲列表与播放状态均可提供
- `source`：`{kind:"local"|"extension",provider_id,track_id}`，在线曲目以来源及来源曲目 ID 标识
- `resource_key`：歌词和封面使用的稳定资源身份，本地为音频 hash，在线为独立命名空间中的缓存键

在线曲目沿用数字 `song_id`，`path`/`first_path` 为空；客户端不得用空路径推断在线曲目记录应被删除。`lyrics.*` 请求的 `track_id` 使用 `resource_key`，本地歌曲原有 hash 调用保持兼容。播放地址和鉴权头不包含在歌曲状态中。

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

按当前播放顺序切换到下一项；顺序和单曲循环模式在队尾停止。

```json
{"id":4,"method":"player.next","params":{}}
```

### `player.previous`

按当前播放顺序直接切换到上一项；随机模式返回播放历史。

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

### `player.set_playback_mode`

设置全局播放顺序，参数 `mode` 为 `sequential`（顺序）、`repeat_one`（单曲循环）、`shuffle`（随机）或 `repeat_all`（列表循环）。成功返回完整播放器状态，包含 `playback_mode`；非法值或保存失败返回错误并保留原模式。模式保存至 `config/settings.json`，首次使用默认顺序播放。切换不打断当前歌曲或改变进度。

```json
{"id":8,"method":"player.set_playback_mode","params":{"mode":"shuffle"}}
```

`player.status` 和首次连接事件均包含 `playback_mode`。模式变更广播 `{"event":"player.playback_mode_changed","playback_mode":"shuffle"}`，供所有客户端同步。

顺序模式播到队尾停止；列表循环在队尾回到队首，上一首可从队首回到队尾。单曲循环只在自然结束时重复，手动切歌仍沿队列顺序。上一首直接切歌，不再根据 3 秒进度重播本曲；顺序和单曲循环在队首重新播放本曲。

随机模式默认使用 `shuffle_bag`：每轮每个队列项播放一次，轮次结束后重新洗牌，至少两项时避免跨轮连续重复同一项。队列显示顺序保持不变，相同歌曲的不同 `queue_id` 分别参与。上一首回到播放历史，下一首先沿历史前进，无历史时上一首重播当前项。直接点播、替换队列或切换模式重建随机轮次；追加项加入本轮待播序列，删除项从待播序列和历史移除。随机历史不跨重启保存。

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
- `playlist.add`：传入歌单 `id` 和本地 `path`、已有队列项的 `queue_id`，或曲库歌曲的 `song_id`。按 `song_id` 添加时需有可用文件路径。导入仅影响歌单，不自动入队或播放。
- `playlist.remove`：传入歌单 `id`、`song_id`，仅移除该歌单中的关联。
- `playlist.play`：传入歌单 `id`，按歌单顺序替换队列并播放；可选 `song_id` 指定从哪首开始。空歌单、失效 ID、缺失文件或队列保存失败时返回错误并保留原队列。

```json
{"id":14,"method":"playlist.create","params":{"name":"现场录音"}}
{"id":15,"method":"playlist.add","params":{"id":1,"queue_id":3}}
{"id":16,"method":"playlist.add","params":{"id":1,"path":"/home/user/Music/live.flac"}}
{"id":21,"method":"playlist.add","params":{"id":1,"song_id":2}}
{"id":17,"method":"playlist.play","params":{"id":1}}
{"id":18,"method":"playlist.rename","params":{"id":1,"name":"Live"}}
{"id":19,"method":"playlist.remove","params":{"id":1,"song_id":2}}
{"id":20,"method":"playlist.delete","params":{"id":1}}
```

`player.status` 和首次连接事件包含 `playlists`；歌单改动、歌单歌曲资料更新通过 `playlist.changed` 广播完整 `playlists`。`queue.changed` 仅更新队列。清空队列不清空歌单。

旧版本的队列文件夹会在启动时通过事务自动迁移为歌单，嵌套名称展开为 `父级 / 子级`，每个歌单保留原文件夹直接包含的歌曲和顺序；原队列及其重复项保持不变。旧 `queue.folder.*` 方法及 `folders`、`folder_id` 响应字段已移除。

### 曲库与标签

`library.import` 传入本地音频 `path`，按文件内容 hash 记录歌曲并持久保存该路径，只更新曲库，不加入队列或开始播放。返回 `song_id` 和绝对路径；曲库变化广播 `library.changed`。

```json
{"id":25,"method":"library.import","params":{"path":"/home/user/Music/NekoTune/song.mp3"}}
```

`library.list` 返回 `data.library`，其中 `songs` 按 `song_id` 升序列出全部历史歌曲，`tags` 列出全局标签。每首歌曲包含 `song_id`、标题、作者、`tags`（`{id,name}` 数组）、`path`、`available` 和 `cover_url`；后端依次从队列、歌单和持久化的导入路径寻找仍存在的文件。不可用歌曲仍会列出，`path` 为空、`available` 为 `false`。曲库变化会广播 `library.changed`，客户端应重新请求 `library.list`。

曲库、歌单、队列和播放状态中的歌曲对象统一提供 `cover_url`：优先使用同目录同名 `.jpg`、`.jpeg`、`.png`、`.webp` 的 `file:` URL，其次使用按音频 hash 关联的已选歌词／歌词缓存封面，无封面时为空字符串。离线模式仅返回本地封面。歌词封面或离线状态变化后，后端同步广播 `library.changed`、`playlist.changed`、`queue.changed` 和 `player.track_changed`，客户端直接使用歌曲对象的 `cover_url`。

`tag.create` 传入 `name`；`tag.rename` 传入 `id`、`name`；`tag.delete` 传入 `id`。名称去首尾空白后须为 1–64 字符，按大小写折叠后的名称唯一。删除标签只移除歌曲上的关联，不删除歌曲、歌单或文件。

`library.play` 接收可选的 `tag_ids` 整数数组，并以“同时拥有全部所选标签”筛选；未传或空数组表示全部歌曲。可选 `song_id` 指定起播歌曲。后端按 `song_id` 升序生成队列，跳过文件不可用的歌曲，并在成功响应中返回 `data.skipped_song_ids`。无可播放歌曲、指定起播歌曲不可用或不匹配、队列保存失败时返回错误，原队列不变。

`library.play` 和 `playlist.play` 均支持可选的非空 `song_ids` 数组，表示当前可见歌曲及其顺序；数组必须包含互不重复的正整数。曲库歌曲须存在并满足 `tag_ids`，歌单歌曲须属于指定歌单；`song_id` 若提供，须属于这个列表且可播放。缺省时保持原有全曲库／全歌单顺序。曲库继续跳过不可用文件；歌单仍在任一选中歌曲文件缺失时失败。无效列表、起播歌曲不匹配或保存失败均不改变原队列、当前歌曲和播放状态。

```json
{"id":25,"method":"library.play","params":{"tag_ids":[1],"song_ids":[9,7],"song_id":7}}
{"id":26,"method":"playlist.play","params":{"id":2,"song_ids":[9,7],"song_id":9}}
```

`library.delete` 接收非空、无重复的 `song_ids` 整数数组和可选布尔值 `clean_files`（默认 `false`）。成功时在同一数据库事务中移除对应的曲库记录、标签关联、所有歌单关联及队列项，返回 `data.deleted_count`，并广播曲库、歌单和队列变化。默认保留本地文件；`clean_files: true` 同时清理已登记的编号目录中的音频、软链接或 `.audio.json` 引用和同名 `.krc`、`.lrc`、`.jpg`、`.jpeg`、`.png`、`.webp`，保留外部原文件和目录中的其他文件，只删除清理后的空目录。目录软链接或越界路径会拒绝清理。

清理先暂存文件，再提交数据库删除；任何 ID 无效、暂存失败或数据库写入失败时整批回滚。数据库提交后删除暂存文件，未能删除的路径通过 `data.cleanup_errors` 返回，界面明确提示已移除歌曲但仍有文件待清理。扩展下载正在进行时暂不允许清理。若正在播放的歌曲被删除，尝试继续播放后续队列项；没有后续项则停止。

```json
{"id":22,"method":"library.list","params":{}}
{"id":23,"method":"tag.create","params":{"name":"现场"}}
{"id":24,"method":"library.play","params":{"tag_ids":[1,2],"song_id":7}}
```

### 外部插件服务

外部插件通过 `extensions.call` 暴露独立服务；具体参数和事件由插件自己的文档定义。通用音乐与歌词客户端使用 `music.*` 和 `lyrics.*`，不依赖某个音源的专属协议。主项目不附带第三方音源插件，需自行安装和信任。

### `song.metadata`

读取指定歌曲的自定义元数据。

```json
{"id":14,"method":"song.metadata","params":{"song_id":1}}
```

### `ai.config.get` / `ai.config.set` / `ai.config.clear_key`

AI 配置接口是异步操作，不占用曲库写命令队列。`get` 与 `clear_key` 无参数，`set` 必须提供 `base_url` 和 `model`，可选提供非空 `api_key`。省略 Key 保留该服务原有 Key，清除必须使用独立方法；地址变化后不会使用其他服务的 Key。

```json
{"id":70,"method":"ai.config.set","params":{"base_url":"https://api.example.com/v1","model":"your-model","api_key":"your-key"}}
{"id":70,"status":"ok","data":{"config":{"base_url":"https://api.example.com/v1","model":"your-model","configured":true,"key_saved":true,"credential_error":""}}}
```

`configured` 表示地址和模型已设置，不代表连接测试通过。`key_saved` 表示当前服务保存了密钥引用；密钥库不可用或条目丢失时 `credential_error` 非空，请重存或清除。返回值从不包含明文 Key。Base URL 仅接受 HTTP(S)，不接受内嵌凭据、查询参数或 URL fragment；自动追加 `/chat/completions`，不覆盖已有路径。Keyless 服务可省略 Key。

### `ai.test`

无参数。使用已保存配置发送一次不含歌曲资料的短请求，同时检查鉴权、模型和返回 JSON 结构。成功返回 `{"connected":true}`，不会修改歌曲或配置。

### `song.suggest_metadata`

为单首歌曲生成建议，不修改数据库、文件或标签。必填正整数 `song_id`，可选 `draft` 对象含 `custom_title`、`artist`、`lyrics` 字符串和 `tags` 字符串数组。草稿缺失字段使用已保存内容。歌词优先取非空草稿、已保存自定义歌词、本地歌词、缓存；传给模型的是限长纯文本，不会触发在线歌词搜索。输入包含原始文件名和最多 200 个已有分类，不包含本地绝对路径和歌曲 hash。

```json
{"id":71,"method":"song.suggest_metadata","params":{"song_id":1,"draft":{"custom_title":"原始歌名","artist":"","tags":["收藏"]}}}
{"id":71,"status":"ok","data":{"song_id":1,"custom_title":"夜空","artist":"演唱者","tags":["中文","抒情"],"warning":""}}
```

建议标签为最多 5 个非空、去重的 1～64 字符名称；优先规范到已有分类名称。不确定的歌名、歌手返回空字符串；客户端应保留该字段当前内容，将标签合并到草稿，最后通过 `song.update_metadata` 保存。`warning: "ai_partial_result"` 提示部分名称缺少依据。

`artist` 保持字符串格式。AI 建议中的多人主署名按原顺序用英文逗号和空格连接，例如 `Orangestar, 初音ミク`；主署名中明确列出的制作人与虚拟歌手都应保留，歌词职务行中的作词、作曲等人员不自动加入。界面将逗号、中文逗号、顿号、分号或换行分隔的名字分别显示，名字内部的空格、`&` 和 `/` 保留。

使用 60 秒总超时，最多同时处理 4 个生成或测试请求；窗口关闭、断开连接不重放请求，客户端必须丢弃过期结果。JSON 模式仅在上游明确不支持相应参数时回退一次，网络错误、鉴权失败或非法返回不会自动重试。

AI 操作失败沿用 `status: "error"` 和 `message`，后者为可翻译的 `ai_error_*` 标识，包括 `configuration`、`url`、`model`、`key`、`keyring`、`key_missing`、`settings`、`auth`、`rate_limit`、`timeout`、`network`、`service`、`request`、`response`、`refused`、`busy`、`cancelled`。参数校验错误仍返回说明文字。上游错误正文及凭据不会原样回传。

### `song.update_metadata`

更新指定歌曲的用户自定义元数据。`custom_title` 也可用 `title` 传入，`artist` 也可用 `author` 传入。可选 `tags` 为标签名称数组，提供时在同一事务中创建缺失标签并替换该歌曲的全部标签；未传入的字段会保持原值。歌曲信息和标签更新后广播 `library.changed`，原有歌单与队列更新事件仍会发送。

```json
{"id":15,"method":"song.update_metadata","params":{"song_id":1,"custom_title":"自定义歌名","artist":"作者名","lyrics":"歌词内容"}}
```

## 歌词

后端负责解析、搜索和缓存歌词，Qt/QML 客户端只通过 IPC 获取结果。播放曲目后按“同目录同名 `.krc` → `.lrc` → 歌曲自定义歌词 → 本地歌词缓存 → LRCLIB”顺序加载。`.krc` 支持酷狗二进制与已解码文本，损坏时回退 `.lrc`。缓存文件位于 `~/Music/NekoTune/config/lyrics-cache`，以音频内容 hash（或标题、歌手、专辑和时长）为键，由 `QSaveFile` 原子写入；旧版缓存仍可读取。刷新跳过缓存和自定义歌词，同名文件仍优先。

`lyrics.changed` 广播当前歌曲的 `track_id`、`revision`、`state` 和可选 `document`、`candidates`。`document.format` 为 `krc`、`lrc` 或 `plain`；`document.lines` 保留 `{time_ms,text}`，KRC 行另有 `duration_ms` 和 `words`，每个词组包含 `{text,offset_ms,time_ms,duration_ms}`，时间单位均为毫秒。普通歌词位于 `document.plain_text`。歌词候选及选定歌词的 `cover_url` 为可选封面地址。状态包括 `loading`、`waiting_metadata`、`searching`、`ready`、`instrumental`、`not_found`、`offline`、`error` 和 `candidates`。

歌词请求携带当前歌曲的 `track_id`（音频内容 hash）：

- `lyrics.refresh`：忽略缓存和自定义歌词，重新获取当前歌曲歌词。
- `lyrics.search`：按 `title`、`artist`、`album` 手动搜索；`source` 从 `lyrics.sources` 读取，包括 `lrclib`（默认）和启用扩展的来源 ID。
- `lyrics.select`：按当前字符串 `revision` 和候选 `index` 选择结果。支持分阶段选择的来源可以先返回歌曲版本，再返回歌词候选；具体阶段由来源实现。
- `lyrics.set_offline`：设置离线模式；仍可读取同名 KRC/LRC、缓存和自定义歌词，不发起歌词网络请求。

自动结果仅在标题、歌手、专辑和时长满足精确匹配（时长误差不超过 2 秒）且候选明显领先时直接应用；其余结果交给客户端选择。旧版数据库的 ASR 表不会被删除，但后端不再读取或写入其中的转写结果。

### `lyrics.sources`

返回当前注册的手动歌词搜索来源。客户端应从此方法生成来源菜单，避免硬编码提供方列表。

歌词操作的 `track_id` 使用歌曲的 `resource_key`：本地曲目等于音频 SHA-256，在线曲目使用稳定的来源身份摘要。扩展启停时通过 `lyrics.sources_changed` 通知客户端刷新来源。

```json
{"id":20,"method":"lyrics.sources","params":{}}
```

```json
{"id":20,"status":"ok","data":{"sources":[{"id":"lrclib","name":"LRCLIB","supports_search":true},{"id":"example.lyrics/demo","name":"Example / 示例歌词","supports_search":true}]}}
```

### 异步完成与元数据补丁

协议格式不变。文件导入可能异步完成，响应仍沿用请求的 `id`，且只有实际入库或入队完成后才返回成功。多个结构性修改按接收顺序执行；导入期间仍可查询状态、暂停、停止、跳转和调节音量。断开连接后客户端不应自动重放修改请求，应先重新获取状态。

`song.update_metadata` 支持部分字段更新：缺少的字段保留，空字符串明确清空文本，空 `tags` 数组明确清空标签。编辑前使用 `song.metadata` 获取完整资料，不应把列表中未包含的歌词字段视为已有空歌词。

队列写入失败返回错误，并保留原来的队列和播放状态；成功响应不再掩盖持久化失败。

## 音乐目录和扫描

默认音乐目录为 `~/Music/NekoTune`，应用数据目录为其下的 `config`。`NEKOTUNE_HOME` 可覆盖音乐根目录，`NEKOTUNE_DB_PATH` 仍可单独覆盖数据库。`player.status` 除原有 `database_path` 外增加 `music_directory` 和 `config_directory`。

- `library.scan`：无参数，异步扫描音乐目录，返回 `data.started`。`true` 表示启动新任务，`false` 表示已有扫描正在进行。后端启动及独立前端首次连接自动发起扫描。
- 扫描同时为已有曲库中可访问但缺少时长的歌曲补齐记录，包括音乐目录之外的旧导入路径；不改动歌曲文件、标签或队列顺序。读取失败或文件丢失时保留未知时长，后续扫描可重试。
- `library.scan_finished`：事件包含 `imported`、`skipped`、`failed` 和 `errors`（路径与安全错误信息数组）。扫描支持编号 `.audio.json` 引用，并验证实际音频内容 SHA-256；损坏引用、不可读音频或内容不匹配通过 `errors` 报告。扫描按内容 SHA-256 去重，不修改队列或自动联网获取配套文件；忽略 `config`、临时文件、目录软链接和曾删除的歌曲。
- `library.import`、`playlist.add` 的路径导入、`queue.add`、带路径的 `player.play`：参数结构保持原样，外部音频优先建立编号原生软链接，链接创建失败时保存编号 `.audio.json` 引用。`library.import` 返回的 `data.path`，以及歌单、队列和播放器的歌曲 `path`，均为可供解码的音频路径：软链接模式返回编号音频路径，引用模式返回外部原音频的绝对路径，不返回 JSON 路径。
- `library.delete`：默认保留音频及配套文件；`clean_files: true` 清理托管文件。两种操作都会在事务内记录内容 hash 的扫描忽略状态。手动导入或下载可恢复；扫描不会恢复。
- `library.assets_failed`：配套文件保存或封面下载失败事件，包含 `song_hash` 和 `message`；不会中断音频播放。

歌曲对象（曲库、队列、歌单和 `song.metadata`）包含持久化的 `duration_ms`，单位为毫秒，`0` 表示未知。导入和扫描在后台读取本地音频时长；旧数据库自动新增字段，已知时长不会被读取失败的结果清空。

每首歌曲使用独立的递增编号目录，例如 `000001/000001.flac`。下载音频为真实文件，外部音频为绝对软链接或 `.audio.json` 引用；程序获取的同编号 KRC、LRC 和封面始终为真实文件。成功在线匹配或手动选定后更新配套文件；结果受歌曲 hash 和歌词 revision 约束，过期请求不能覆盖新选择。原音频离线或断链后保留歌曲资料和配套文件，曲库 `available` 为 `false`。

设置和缓存集中在 `config`；首次默认目录迁移保留旧数据库和缓存，不覆盖已有目标。账号密钥和会话迁入系统密钥环，回读校验成功后删除旧明文凭据；失败保留原文件并返回 `credential_error`。设置页保存的语言优先于系统语言，`NEKOTUNE_LANGUAGE` 优先于保存语言；`lyrics.set_offline` 持久保存到 `config/settings.json`。

扩展通过宿主凭据接口使用系统安全存储（Windows 凭据管理器、macOS Keychain 或 Linux Secret Service / KWallet）。凭据不会通过公开业务 IPC 返回；系统密钥环访问或旧凭据迁移失败时返回错误说明。保存失败不会回退为明文存储。Linux 升级时会回读校验并迁移旧 libsecret 条目，成功后清理旧条目。

## 动态扩展接口

扩展使用方式和 SDK 见 [扩展开发文档](extensions.md)。以下方法保持原有 `{id,method,params}` / `{status,data}` 响应约定。管理操作只访问本地安装包，安装本身不会启用代码。

| 方法 | 参数 | data / 行为 |
| --- | --- | --- |
| `extensions.list` | 无 | `extensions` 注册状态数组、`selections` 界面选择、`runtimeReady` 表示管理进程已连接且扩展发现已完成 |
| `extensions.install` | `path` 本地 ZIP；开发目录传 `development:true`；覆盖已有版本需 `replace:true` | `extension` 安装记录；更新启动失败回滚旧程序版本 |
| `extensions.enable` | `id`；首次信任需 `trusted:true` | 依赖按序启动，返回注册快照 |
| `extensions.disable` | `id` | 撤销能力，依赖该扩展的其他扩展进入 blocked 状态 |
| `extensions.reload` | `id` | 重新读取 manifest 并重建相关进程和界面贡献 |
| `extensions.uninstall` | `id,clearData?` | 默认保留数据；不删除挂载的开发目录 |
| `extensions.get_config` | `id` | `config` 对象 |
| `extensions.set_config` | `id,config` | 原子替换配置，通知运行中的扩展 |
| `extensions.logs` | `id` | 最近最多 500 条 `logs`，字段为 `time,level,message` |
| `extensions.call` | `id,service,params?` | 调用扩展注册的服务或命令 |
| `extensions.select` | `slot,contribution` | 选择命名空间贡献 ID，空字符串恢复默认 |

扩展记录包含 `id,name,version,directory,development,trusted,enabled,state,error,generation,contributes,registrations`。`enabled` 表示持久启用意图，`state` 为实际运行状态；可为 `disabled,starting,running,stopping,failed,blocked`。来源和 UI 贡献只有在运行状态下公开。

后台广播 `extensions.changed`，其字段与注册快照一致。`extensions.error` 表示管理进程级别错误。扩展主动发布的事件使用 `extension.<扩展ID>.<事件名>`，附带 `extensionId` 与 `data`。私有运行时调用、密钥读取和原始音频解析结果不作为通用管理 API 暴露。

## 扩展音乐来源

| 方法 | 参数 | data / 行为 |
| --- | --- | --- |
| `music.sources` | 无 | `sources` 数组，来源 ID 形如 `example.test-source/tones` |
| `music.search` | `source,query,cursor?` | `tracks` 与可选下一页 `cursor`，不写入曲库 |
| `music.track` | `source,id` | 扩展提供的曲目详情 |
| `music.enqueue` | `source,track,play?` | 保存在线身份并追加队列，返回 `queue_id`；默认不切歌 |
| `music.add_to_playlist` | `source,track,playlist_id` | 将在线曲目加入歌单，不更改播放队列 |
| `music.download` | `source,track` | 立即返回 `task`，完成结果通过事件报告 |
| `music.cancel` | `task` | 请求取消尚未结束的下载；最终状态通过 `music.download` 事件报告 |

`track` 必须包含字符串 `id,title`，可包含 `artist,album,duration_ms,cover_url`；时长单位为毫秒且不可为负。搜索、解析和下载在扩展进程中异步执行，不占用数据库写队列。在线身份以来源和曲目 ID 去重，队列仍允许同一歌曲重复出现。

```json
{"id":20,"method":"music.enqueue","params":{"source":"example.test-source/tones","track":{"id":"one","title":"Test tone","artist":"NekoTune","duration_ms":8000},"play":true}}
```

`music.sources_changed` 通知客户端重新获取可用音源。下载状态事件：

```json
{"event":"music.download","task":"...","state":"finished","song_id":42,"message":""}
```

`state` 为 `running,finished,failed,cancelled`。传输进度可包含 `received,total`（字节数，未知总量为 `0`）；完成结果包含 `song_id,path`，可附带来源提供的资源状态或 `asset_warning`。只有文件已校验并成功入库后才发出 `finished`；配套资源失败不撤销音频入库，下载不改变队列或歌单。取消不会删除已成功入库的音频。传输期间不允许同时执行托管文件清理，避免下载与删除竞争。来源失效、解析失败和网络错误保留在线队列项，用户可重新启用来源或重试播放。

# NekoTune 架构与扩展开发

## 依赖方向

```text
QML 页面 → 功能控制器 / 列表模型 → IpcClient
                                    │ 本地 Socket，换行分隔 JSON
                          IpcServer → 注册的 IPC 处理器
                                    │
                             类型化应用服务
                                    │
                    领域类型、仓储接口、播放与文件检查接口
                                    ↑
                  SQLite / Qt Multimedia / 网络与文件实现
```

`BackendSession` 是后端装配入口，负责构造和连接对象；业务服务使用构造注入，不从全局容器获取依赖。`BackendRuntime` 管理后台线程。独立前端不链接后端业务；统一桌面程序和独立后端复用同一套后端装配。

CMake 将领域、存储、歌词提供方、基础设施、应用、IPC、运行时拆成独立目标。领域层只使用 Qt Core 值类型、接口和歌词解析，不包含 JSON、SQL、网络或 Multimedia 类型；应用层只链接领域目标，IPC 不链接任何存储或播放实现。IPC 负责将类型化结果映射为现有协议。

## 模块职责

- `PlayerEngine`：播放状态、切歌、媒体源、进度、音量和元数据就绪；通过 `IPlaybackBackend` 操作播放器。
- `QueueService`：内存队列与持久队列之间的提交入口。`PlayerQueue` 是独立领域对象，队列 ID 与歌曲 ID 保持不同语义。
- `LibraryService`、`PlaylistService`、`TagService`：各自管理资料、歌单和标签；元数据修改使用可选字段组成的 `MetadataPatch`。
- `CollectionService`：导入并入队、播放歌单或筛选结果、跨曲库/歌单/队列删除。需要多个仓储时，由这里协调事务。
- `LyricsController`：连接播放状态和歌词工作线程，过滤过期歌曲与 revision；`LyricsService` 管理本地优先级、候选、缓存和离线模式。`ILyricsStorage` 隔离本地文件与缓存读写，具体 `LyricsStorage` 在启动模块注入。
- `KugouService`：通过 `IKugouBackend` 接收类型化状态和事件，向 IPC 提供应用操作。基础设施中的 `KugouMusicService` 协调 `KugouAccountSession`（密钥和会话）、`KugouApiClient`（API 请求）和 `KugouDownloadJob`（音频及附属文件下载）。`DownloadService` 将已下载音频交给曲库导入。
- `IpcRouter`：方法注册、参数分发、请求关联及异步响应，不实现播放或存储规则。功能处理器分为播放、曲库、歌词和酷狗模块。

## 线程、提交与退出

GUI 线程只运行前端。后端线程创建播放器适配器、IPC、网络服务、SQLite 会话和业务服务；数据库连接只能在创建它的线程使用。文件检查和 SHA-256 计算由 `ImportExecutor` 工作线程处理，返回 `ImportedFile` 值，不返回文件句柄或数据库连接。歌词服务及其提供方运行在歌词线程。

结构性写操作通过 `CommandScheduler` 串行执行，导入期间后续写操作等待，但状态查询、暂停、停止、音量和进度控制仍可响应。原有导入方法的成功响应表示实际完成入库或入队，不表示仅进入任务队列。客户端断开不会重放请求，已接受的任务仍可完成。

队列及跨集合操作顺序为：

1. 校验参数，构造候选内存状态。
2. 在共享数据库会话上开启事务，写入相关仓储并提交。
3. 提交成功后更新内存队列和媒体源，再发出状态事件。

仓储不自行嵌套开启事务。`Transaction` 在未提交时自动回滚。失败以 `Result<T>` 返回，IPC 保留 `status: error` 和 `message`；写入失败不能被当作成功提交。

退出时先停止接收新连接和请求调度，取消待执行命令及导入任务并完成其回调，再关闭已连接客户端、取消网络请求、停止歌词线程、停止播放并释放服务，最后退出后端线程。不要先停止线程事件循环，再尝试跨线程销毁仍存活的服务。

## 前端状态与页面

`IpcClient` 只管理连接、协议解析、请求 ID 和完成回调。每个功能控制器订阅自己需要的事件，不存在一个供所有页面绑定的全量 `status` 对象。

播放位置、时长、音量、曲目分别通知。曲库、队列、歌单和标签通过 `RecordModel` 维护稳定 ID；更新使用插入、移动、删除和 `dataChanged`，避免整表重置。曲库控制器拥有筛选和勾选状态，播放位置更新不能改变用户选择。

元数据编辑器通过 `song.metadata` 加载完整记录后才允许保存；保存仅发送修改的字段。字段缺失表示保留原值，空字符串或空标签数组表示明确清空。编辑器忽略先前歌曲的迟到加载结果。

`Main.qml` 只装配窗口、导航、页面、弹窗与底部播放器。页面描述表位于 `frontend/qt-qml/qml/shell/PageRegistry.js`，页面通过属性接收控制器、翻译器及连接状态，不直接访问后端对象。

## 新增业务功能

1. 在领域层定义输入、输出和必要的可替换接口；避免为单个普通函数增加无用途的抽象类。
2. 编写应用服务，构造函数显式接收所需接口；需要持久化时增加专用仓储，跨仓储事务留在用例中。
3. 在独立 IPC 模块调用 `registerMethod`。读操作直接完成，影响集合的写操作使用串行调度；异步处理器必须恰好调用一次完成回调。
4. 在 `BackendSession` 创建服务并注册模块，明确对象所有权和退出顺序。
5. 补充服务测试与协议测试，记录新方法、字段及错误行为。

模块可以增加自己的处理器，无需修改 `PlayerEngine`、Socket 分帧或通用路由实现。重复注册的方法名会被拒绝。

## 新增歌词来源

实现 `LyricsProvider` 的来源描述、请求、取消和候选解析。单阶段来源可使用默认候选解析；多阶段来源声明 `staged` 并覆写 `choose`，通过 `completed` 发布下一组候选或通过 `resolved` 发布文档。

提供方私有的歌曲 hash、访问凭据和下载标识存放在适配器内部，通用候选只携带不透明句柄。新提供方在后端装配处注册，并设置为歌词服务的子对象，随歌词线程统一管理；不要把网络对象留在创建线程。

`lyrics.sources` 自动列出已注册来源，前端来源菜单只显示其中支持搜索的来源。自动加载仍为本地 KRC → 本地 LRC → 自定义歌词 → 缓存 → LRCLIB；新增手动来源不会自动改变默认加载顺序。

## 新增页面

在 `pages` 下创建页面并声明 `shell`、`controllers`、`transport`、`translator` 属性，以及 `importRequested`、`editRequested` 信号。页面内部注入所需的功能控制器。将描述加入 `PageRegistry.js` 并把 QML 文件加入资源清单，导航和页面容器无需新增条件分支。

页面状态由控制器或页面自身管理；避免把页面业务、请求回调或列表选择放回 `Main.qml`。

## 验证

```sh
cmake -S . -B build
cmake --build build -j2
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
git diff --check
```

`nekotune_refactor_test` 使用假播放后端验证失败提交，使用注册的假模块和歌词来源验证扩展接口，并启动真实后端、Socket、前端控制器及 Main.qml 验证完整交互。

需要检查实际页面时，可运行 `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software NEKOTUNE_TEST_SCREENSHOT=/tmp/nekotune.png ./build/tests/nekotune_refactor_test realQmlMetadataSelectionAndMute`。该测试使用隔离的数据和 Socket，输出各页面截图；截图仍需人工查看，不能只检查文件是否生成。

运行时检查需隔离 `NEKOTUNE_SOCKET`、`NEKOTUNE_DB_PATH` 和 XDG 配置、数据、缓存目录。网络单测使用模拟响应，不触发真实短信、登录或下载。QML 检查应区分静态警告、交互回归和实际截图，不能用编译成功替代视觉验收。

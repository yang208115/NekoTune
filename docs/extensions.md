# 扩展开发与使用

NekoTune 的扩展由 JavaScript/TypeScript 后台代码和可选 QML 界面组成。后台使用随应用提供的 Node.js 24.21.0，用户无需安装 Node.js；开发工具使用 pnpm。第一版的运行时发行目标是 Linux x86_64。

## 安装、启用和恢复

打开侧栏「扩展」，导入 ZIP 包，或选择「挂载开发目录」。安装完成后扩展处于停用状态，首次启用需要确认信任。扩展拥有当前用户的完整权限，可以读取文件、访问网络、启动外部程序及替换界面；这不是安全沙箱。

开发目录中的文件变更会触发重载，也可以手动重载。停用会移除扩展的来源、命令、菜单和页面。卸载默认保留配置与数据，开发目录原文件不会被删除。更新采用新的程序目录，启动失败会恢复上一程序版本；扩展自行修改的数据不属于程序版本回滚范围。

完整界面、首页、曲库、歌单、歌词页、播放栏和主题的替代实现需要在管理页显式选择。`Ctrl+Shift+E` 打开独立管理窗口，其中「恢复默认界面」清除所有替换选择。该窗口使用内置主题。

如果 QML 代码使 GUI 线程无法响应，退出应用后运行：

```sh
./build/nekotune --safe-mode
```

安全模式保留扩展及其数据，不启动扩展代码。分离运行时，`nekotune-backend --safe-mode` 控制后端脚本，`nekotune-qt-qml --safe-mode` 跳过该前端的扩展视图；已经运行的另一后端不会被前端启动参数停止。包含 Qt 原生模块的扩展应声明 `nativeModules: true`，其更新需要重启。

应用随附的 [酷狗插件](../extensions/builtin/kugou/README.md) 会自动登记到扩展列表，首次运行仍需要用户启用并信任。卸载后不会在下次启动时重新登记，可手动导入发行 ZIP 恢复。

## 目录、构建与打包

最小目录：

```text
my-extension/
├── extension.json
├── dist/main.mjs
└── Page.qml
```

```json
{
  "id": "my.extension",
  "name": "My extension",
  "version": "1.0.0",
  "apiVersion": 1,
  "main": "dist/main.mjs",
  "config": { "enabledFeature": true },
  "dependencies": {},
  "contributes": {
    "pages": [
      { "id": "hello", "title": { "zh": "你好", "en": "Hello" }, "source": "Page.qml" }
    ]
  }
}
```

`id` 使用小写字母、数字、点、横线和下划线，以字母开头，长度 2–101。`version` 使用 SemVer，`apiVersion` 当前为 `1`。`dependencies` 将扩展 ID 映射到 SemVer 范围，宿主检测版本不匹配、缺失与循环依赖；依赖必须分别被用户信任。

`main` 导出 `activate(context)`，可导出 `deactivate(context)`；纯界面扩展可以省略 `main`。QML 文件路径相对于扩展根目录，不能越出目录。所有 UI 根组件使用 `Item` 或其子类；宿主提供顶层窗口。

在仓库中运行：

```sh
cd extensions
pnpm install
pnpm build
pnpm check
pnpm test
pnpm run validate examples/test-source
pnpm run pack examples/test-source
pnpm run create /tmp/my-extension
```

创建模板后，在模板目录运行 `pnpm install`、`pnpm check` 和 `pnpm build`。模板引用本仓库的 `@nekotune/sdk` 类型包。TypeScript 和第三方依赖在构建时由 esbuild 打包；扩展安装和启用时不运行包管理器或安装脚本。打包工具忽略开发用 `node_modules`，因此应将第三方依赖打包进 JS 产物。需要原生 Node 模块时，由扩展作者提供匹配捆绑 Node 版本及平台的实际文件；不要将开发目录的符号链接打入 ZIP。

ZIP 根目录必须包含 `extension.json`，不额外嵌套目录。宿主拒绝越界路径、链接和超限包：最多 20,000 个条目、总解压大小 512 MiB。

## 后台 SDK

完整 TypeScript 接口见 [SDK 类型声明](../extensions/sdk/index.d.ts)。

```ts
import type { ExtensionContext } from '@nekotune/sdk';

export async function activate(context: ExtensionContext) {
  await context.commands.register('pause', async () => {
    return context.host.call('player.pause');
  }, { title: 'Pause playback', shortcut: 'Ctrl+Shift+P' });

  context.events.on('player.track_changed', event => {
    context.log.info('Now playing:', event.song?.title);
  });

  await context.services.register('hello', async ({ name }) => ({
    greeting: `Hello, ${name}`
  }));
}
```

| 能力 | 接口 |
| --- | --- |
| 调用宿主业务 | `context.host.call(method, params)`，方法见 [API](API.md) |
| 事件 | `events.on(name, handler)`，`events.emit(name, data)`；自定义事件带扩展命名空间 |
| 命令、快捷键 | `commands.register(id, handler, {title, shortcut})` |
| 扩展间调用 | `services.register`、`services.call(extensionId, service, params)` |
| 动态界面贡献 | `ui.register(kind, id, descriptor)`，字段与 manifest 相同 |
| 配置 | `context.config`、`settings.update(config)`；更新后发出 `extension.config_changed` |
| 小型 JSON 存储 | `storage.get/set`；总量上限 8 MiB，大文件使用 `dataDirectory` |
| 凭据 | `secrets.get/set/delete`，按扩展命名空间保存到系统密钥库，无明文回退 |
| 任务进度 | `tasks.report(taskId, {state, progress, message})` |
| 系统能力 | 标准 Node.js 文件、网络、进程和模块 API |

注册方法返回清理函数。事件订阅和注册项会在停用时撤销；作者创建的定时器、服务器、文件监听等资源应把清理函数加入 `context.subscriptions`，或在 `deactivate` 中释放。普通调用超时 30 秒，激活上限 10 秒，退出清理上限 3 秒。宿主定期检测后台进程响应，崩溃或无响应时撤销能力并标记故障，需要用户手动恢复。

服务返回 JSON 对象；标量或数组应包装为 `{value: ...}`。ZIP 更新在新版本激活失败时恢复旧程序；开发目录自动重载无法还原作者已经覆盖的源文件，修改错误后手动重试即可。

后台 RPC 使用专用本地 Socket，`console` 输出进入该扩展的日志环形缓冲区，不会破坏协议。每个扩展保留最近 500 条日志。宿主不输出凭据，SDK 已知的密钥值会在日志中替换；扩展作者仍应避免主动输出未知的敏感数据。

程序保存在音乐目录下 `config/extensions/packages`，数据保存在 `config/extensions/data/<id>`，注册状态与界面选择保存在 `config/extensions/registry.json`。这些位置均遵循 `NEKOTUNE_HOME`。

## QML 界面、菜单和主题

```qml
import QtQuick
import QtQuick.Controls
import NekoTune 1.0

Item {
    ExtensionApi { id: api }
    Button {
        text: "Call extension"
        onClicked: api.invoke("hello", {name: "NekoTune"})
            .then(result => text = result.greeting)
            .catch(error => text = String(error))
    }
}
```

`ExtensionApi` 提供 `call(method, params)`、`invoke(service, params)`、宿主事件信号、`app` 业务控制器、`window` 窗口对象、`theme` 主题变量和本地化辅助函数 `t`。`ownerId` 是当前扩展 ID。后台不直接持有 QML 对象；多前端可各自加载视图，共享同一扩展后台。

每个扩展视图用独立 QML 引擎管理；重载销毁旧视图与引擎。耗时任务应放在后台，QML 代码仍共享 GUI 线程，不能保证任意 QML 代码的故障隔离。

`contributes` 支持以下数组，同类贡献的 `id` 不得重复；公开身份为 `<extensionId>/<id>`：

| 类别 | 字段与用途 |
| --- | --- |
| `pages` | `id,title,source,group`；group 默认 `primary`，也可为 `utility` |
| `settings` | `id,title,source`；显示在「设置 → 扩展设置」，点击进入子页面，不占用主侧栏；默认 Shell 可通过 `ExtensionApi.window.navigate("<extensionId>/<id>")` 打开 |
| `slots` | `id,title,source,slot`；slot 为 `shell`、`bottomPlayer` 或 `page:<内置页面ID>` |
| `themes` | `id,title,tokens`；覆盖已有 `Theme.qml` 的颜色、字体、尺寸和动效变量 |
| `menus` | `id,title,context,command`；context 为 `song` 或 `playlist`，命令收到歌曲或歌单上下文 |
| `toolbars` | `id,title,source,height`；插入默认 Shell 顶部工具栏 |

`title` 可用字符串或 `{zh,en}`。界面替换通过管理页选择；主题或替换项停用后回退到内置实现。完整 Shell 自行决定布局；默认 Shell 提供的工具栏和菜单不会自动出现在作者完全替换的界面中。

## 音乐和歌词来源

```ts
await context.music.register('music', {
  async search({ query, cursor }) {
    return { tracks: [{ id: 'song-1', title: query, artist: 'Example', duration_ms: 180000 }] };
  },
  async track({ id }) { return { id, title: 'Example song' }; },
  async resolve({ id, purpose }) {
    return { url: 'https://example.org/audio.mp3', headers: {}, expiresAt: Date.now() + 60000 };
  }
}, { name: 'Example music', download: true });
```

搜索返回 `{tracks, cursor?}`，`cursor` 是扩展自己的分页游标。曲目必须有稳定字符串 `id` 和 `title`，可带 `artist`、`album`、`duration_ms`、`cover_url`。不要把凭据或签名播放地址放入曲目描述。

音源自带浏览页面时，在注册描述中指定 `page`，其值为本扩展 `contributes.pages` 的局部 ID，例如 `{name: 'Example music', page: 'browse'}`。未指定时自动匹配与音源局部 ID 相同的页面。页面必须位于默认或 `primary`、`utility` 导航组；设置页和无关页面不算音乐页面。存在对应页面的音源使用自己的入口，不重复出现在通用「扩展音源」中。只有仍有无专属页面的音源时，默认侧栏才显示通用入口；页面撤销后音源重新出现在通用列表。`music.sources` 和 SDK 的完整来源列表保持不变，默认界面使用控制器的 `browserSources` 过滤结果。

解析返回 HTTP(S) `url`、可选 `headers`、毫秒 Unix 时间戳 `expiresAt`，以及下载文件扩展名 `extension`。下载可实现独立 `download` 方法；省略时复用 `resolve({purpose:'download'})`。媒体编码能力由 Qt Multimedia 决定。

来源可通过 `allowedHosts` 限制音频及重定向的 HTTPS 主机，通过 `contentTypes` 限制下载 MIME 类型，通过 `maxBytes` 限制下载大小。可选 `downloaded({id,path,song_id,existing})` 在音频完成校验和入库后调用，适合补充歌词、封面；调用期间不占用曲库写队列。返回的 JSON 字段附在 `music.download` 完成事件中，失败只产生 `asset_warning`，已入库音频保留。配套处理限时两分钟。

`music.download` 返回任务 ID，进度事件包含 `received/total`，通过 `music.cancel({task})` 可取消传输。取消后不发布不完整文件；已入库的音频仍保留。

宿主使用本地流代理转发鉴权头和 Range 请求，使用流式背压而非把音频整体读入内存。临时地址和鉴权头不写入数据库，也不出现在普通播放状态中。过期或收到 401/403/410 时重新解析一次，跨域重定向不携带 Authorization/Cookie。下载具有 10 分钟总时限。

在线曲目加入队列或歌单后才保存，以来源和来源曲目 ID 去重；本地音频继续以真实 SHA-256 去重。本地曲库默认不显示纯在线记录。来源故障或停用不会删除队列/歌单项。下载完成后返回本地 `song_id`，不自动改动原有在线引用或播放队列。

歌词使用 `context.lyrics.register(id, {search, resolve}, {name})`。`search` 返回 `{candidates:[...]}`，候选可带 `title`、`artist`、`score` 和来源自行定义的字段。`resolve(candidate)` 返回 `synced_lyrics`、`krc_lyrics`、`plain_lyrics`、`instrumental`、`cover_url` 等字段。返回现有 LRC/KRC 文本，宿主负责解析、选择和缓存。动态歌词来源出现在既有歌词搜索入口中，不改变本地歌词优先级。

分阶段歌词来源可以让 `resolve` 再次返回 `{candidates:[...]}`；歌曲版本候选标记 `song_result:true`，随后的歌词候选标记为 false 或省略。候选中的访问凭据只保存在后端解析句柄中，不序列化给前端。

## 示例与验证

四个示例位于 `extensions/examples`：

- `automation`：TypeScript 睡眠定时器和播放事件订阅。
- `lyrics`：可安装的示例 LRC 来源。
- `test-source`：现场生成音频，提供鉴权、Range、搜索和下载；无需真实音乐账号。
- `custom-ui`：独立页面、设置页、菜单、工具栏、播放栏、主题和完整 Shell。

`pnpm build` 后可直接挂载示例目录，或使用 `pnpm run pack` 分发 ZIP。验证命令：

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
pnpm --dir extensions check
```

CMake 会验证并缓存官方 Node.js 压缩包。离线构建可传入 `-DNEKOTUNE_NODE_ARCHIVE=/absolute/path/node-v24.21.0-linux-x64.tar.xz`；JS 构建依赖也需提前填充 pnpm 缓存。`cmake --install build --prefix <directory>` 安装应用、后台、Node 及打包后的运行时；Qt 等系统依赖仍由部署环境提供。

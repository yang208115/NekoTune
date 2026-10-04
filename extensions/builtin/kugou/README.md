# 酷狗音乐插件

`nekotune.kugou` 是可独立安装、启停、重载和卸载的 JS + QML 扩展。账号、Worker 请求、歌曲搜索、音频解析、歌词及专属界面均在此目录；宿主通过通用扩展协议提供播放器、入库、任务和系统密钥库。

## 使用

1. 在侧栏「扩展」启用并信任「酷狗音乐 / Kugou Music」。应用首次启动会登记随发行包提供的插件，但不自动信任或运行它。
2. 打开「设置 → 扩展设置 → 酷狗设置」，填写 Worker **根地址**并启用服务，例如 `https://worker.example`。酷狗音乐页的「前往设置」也会打开此子页面。程序自动追加 `/api/music/kugou/v1`；远程地址须使用 HTTPS，回环测试服务允许 HTTP。
3. 匿名用户可搜索歌曲和歌词。在插件设置中填写服务端对应的账号密钥，再通过右上角弹窗进行短信登录，即可在线播放及下载账号有权限的歌曲。

酷狗通过专属音乐页面浏览，不重复显示在通用「扩展音源」列表中。扩展和自定义界面仍可通过通用音乐 API 搜索酷狗、加入队列或歌单。下载完成后按音频 SHA-256 去重入库，不自动改变队列。单次下载最多 100 MiB，仅接受受信任的酷狗 HTTPS 音频地址；取消传输不会发布半成品。新歌曲在可靠匹配时保存 KRC、LRC 和封面，重复歌曲或已有用户侧载文件不覆盖。资源获取失败不撤销已成功入库的音频。

歌词来源支持两次选择：歌曲版本 → 歌词候选。KRC 从酷狗接口获取并在插件中有界解压，失败后通过 Worker 获取同一候选的 LRC。账号或版权限制不绕过；不实现 VIP 领取、升级或付费购买。

## 配置与凭据

首次启用导入旧 `settings.json.kugou` 配置，并保留原配置。之后使用插件独立的 `config.json`。插件停用、未配置 Worker 或安全模式下，不注册音源。

账号密钥与会话使用 `context.secrets`，继续沿用旧版的两个系统密钥库条目，以免丢失登录信息。宿主保留这一迁移兼容层：旧明文文件仅在密钥库写入并回读成功后清理；失败时保留原文件、显示凭据错误。没有明文降级。卸载默认保留配置和凭据，明确选择清理数据才会删除已使用的插件凭据。

密钥优先级为系统密钥库 → `KUGOU_ACCOUNT_API_KEY` → `KUGOU_ACCOUNT_API_KEY_FILE`。外部密钥文件不会被迁移或删除。界面、状态、日志和事件不返回账号密钥、Cookie 或签名播放地址。

## 开发与打包

```sh
cd extensions
pnpm build
pnpm run validate builtin/kugou
pnpm run pack builtin/kugou ../build/extension-packages/nekotune.kugou.nekotune.zip
```

CMake 构建自动产生 `extensions/dist/builtin/nekotune.kugou.zip`，安装时与 Node 运行时一同分发。可直接挂载源码目录开发。插件被卸载后不会在每次启动时重新出现，可手动导入 ZIP 恢复；升级插件使用「安装 / 更新包」。

## 接口

通过 `extensions.call` 调用，`id` 为 `nekotune.kugou`：

| 服务 | 参数 / 行为 |
| --- | --- |
| `status` | 仅返回配置、登录、忙碌和凭据错误状态 |
| `config.set` | `{enabled, worker_url}`；配置原子写入，动态注册或撤销来源 |
| `save_key` / `clear_key` | 写入 `{key}` 或清除密钥库覆盖值 |
| `send_code` | `{mobile}`；注册设备后发送短信 |
| `login` | `{mobile, code}`；会话保存成功才报告登录成功 |
| `search` | `{keywords, page}`；异步发布搜索结果 |
| `play` / `download` | `{hash}`；调用通用 `music.enqueue` / `music.download` |
| `cancel` | 取消当前专属界面发起的下载 |

事件使用 `extension.nekotune.kugou.<名称>`，实际载荷在 `data`：`status`、`config_changed`、`code_sent`、`logged_in`、`search_results`、`download_progress`、`download_finished`、`download_cancelled`、`operation_failed`。

来源 ID：音乐 `nekotune.kugou/music`；歌词 `nekotune.kugou/lyrics`。旧内置 `kugou.*` IPC 已移除，新集成应使用上述扩展服务或通用音乐、歌词接口。

自动测试使用模拟 Worker、音频与凭据，不发送真实短信，不登录真实账号，也不调用付费或受限下载。

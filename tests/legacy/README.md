# 旧酷狗协议回归夹具

这里保留迁移前的 C++ 账号网络、下载和歌词适配器，供既有模拟网络测试继续校验协议和历史行为。它们仅链接到 `nekotune_legacy_kugou_test_support` 及测试程序，不链接或安装到 NekoTune 应用。

运行中的酷狗实现位于 `extensions/builtin/kugou`，新增功能和修复应修改插件。插件协议测试位于 `extensions/tests/kugou.test.mjs`；真正仍由宿主使用的旧凭据迁移兼容代码位于 `backend/infrastructure/kugou/kugou_account_session.*`。

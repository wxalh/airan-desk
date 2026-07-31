# Airan-Desk 被控端通知脚本示例

简体中文 | [English](README.md)

程序固定传入三个参数：

1. `event`
2. `peer_id`
3. `detail`

支持的实时事件：

- `connection_requested`
- `authentication_failed`
- `connection_established`
- `connection_disconnected`

设置页面测试时使用：

```text
test local "notification script test"
```

通知脚本默认关闭，请在本机配置 `notification.notify_script`。可使用 `{app_dir}`
表示 Airan-Desk 可执行文件所在目录。

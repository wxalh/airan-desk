# Airan-Desk Controlled-Side Notification Script Examples

[简体中文](README.zh-CN.md) | English

The program passes exactly three arguments:

1. `event`
2. `peer_id`
3. `detail`

Supported live events:

- `connection_requested`
- `authentication_failed`
- `connection_established`
- `connection_disconnected`

The settings test uses:

```text
test local "notification script test"
```

Scripts are disabled by default. Configure `notification.notify_script`
locally. Use `{app_dir}` to refer to the directory containing the Airan-Desk
executable.

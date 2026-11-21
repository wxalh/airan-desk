# 飞牛 OS（fnOS）安装与打包

Airan Desk 的 fnOS 包是 NAS 被控端，支持远程文件和终端。它不创建虚拟桌面；控制端使用 Windows、Linux 或 macOS 桌面版 Airan Desk。

本包连接已有的私有信令和 STUN / TURN 服务。若要在飞牛上部署这些服务，可单独安装 [Signal Server + coturn](https://github.com/wxalh/signal-server) 的飞牛包，并在其管理页查看按当前配置生成的公网 NAT 端口映射清单。

## 安装和连接

1. 按 NAS 架构选择 `airan-desk-fnos-<版本>-amd64.fpk`（x86_64）或 `airan-desk-fnos-<版本>-arm64.fpk`（aarch64）。
2. 在飞牛应用中心启用手动安装并安装 FPK。本地生成的包默认未签名，需要使用系统允许的手动安装方式。
3. 打开飞牛桌面的 **Airan Desk** 图标。管理页经飞牛登录网关提供，仅管理员可访问，无须额外开放管理端口。
4. 填写自建信令服务器，例如 `wss://signal.example.com/ws`。端口可以直接写在 URL 中。按需设置 STUN / TURN 主机、端口、用户名和密码；新 TURN 密码留空会保留原密码。TURN 用户名和密码是服务器凭据，不受设备 UUID 规则限制。
5. 首次启用时确认设备所有者授权，勾选“启用远程文件和终端访问”，再保存。新安装默认关闭远程访问，也没有预置的信令或 TURN 服务。
6. 点击 **分享** 自动复制设备码和验证码。将文本粘贴到桌面端远程设备码栏，并选择文件或终端连接。双方须配置可互通的信令和 ICE 服务。

设备码（ID）和连接密码（验证码）均须为有效且非全零的 UUID。管理页中的设备 ID 只读；点击 **更换密码**，管理服务会生成并立即保存新的 UUID 密码，设备 ID 保持不变，后续分享使用新验证码。更换密码或保存设置会重启被控进程并断开已有连接。

分享文本包含连接验证码，请只发给获授权的连接者。浏览器限制自动复制时，页面会选中文本供手动复制。分享不包含信令地址和 TURN 密码。

## 文件权限与数据

应用和远程终端以飞牛的 `airan-desk` 应用账户运行，不以 root 运行。文件访问受这个账户的系统权限约束。在飞牛共享文件夹授权中按需授予该应用读写权限；本包不会自动授予所有共享目录权限。

持久数据位于 `/var/apps/airan-desk/var/state/`：

| 路径 | 内容 |
| --- | --- |
| `settings.json`、`conf/` | 连接设置、设备码、验证码 |
| `logs/` | 原生客户端日志 |
| `home/.wxalh/airan-desk/audit/` | 本机操作审计 |
| `notifications.jsonl` | 连接通知，管理页显示最近记录 |

若需手动维护 `conf/id.ini`，请先停止应用并备份该文件；`[local]` 下的 `local_id` 和 `local_pwd` 也必须是有效且非全零的 UUID，不能使用任意字符串。日常更换连接密码请使用管理页的 **更换密码** 按钮。

管理服务日志为 `/var/apps/airan-desk/var/run/manager.log`。更新前请备份应用数据，再在应用中心安装版本号更高的 FPK。同版本的 `install-fpk` 可能只报告已安装；`install-local` 会先卸载旧应用，可能清除设备身份、设置和日志。备份配置时也要保护其中的验证码和 TURN 密码。

管理服务使用本机 Unix socket，经飞牛网关校验管理员身份和表单 CSRF。它启动完整 portable 包的包装脚本，以保持私有 Qt、WebRTC 相关库和运行时路径一致。原生进程目前没有 POSIX 信号到 Qt 事件循环的优雅退出桥接；停止时管理服务采用有时限的进程组终止及 PTY 后代清理。

## 本地打包

需要 Linux、Go 1.24+、Python 3、curl、binutils，以及与目标架构和版本匹配的**完整** Linux glibc 2.27 portable 包。可在 WSL 中运行。不要只替换 `airan-desk.real` 或混用不同版本的运行库。

```bash
bash packaging/fnos/package.sh amd64 1.2.1 \
  out/fnos-input/airan-desk-linux-x64-glibc2.27-portable.tar.gz out/packages

bash packaging/fnos/package.sh arm64 1.2.1 \
  out/fnos-input/airan-desk-linux-arm64-glibc2.27-portable.tar.gz out/packages
```

脚本检查 ELF 架构、必要库、插件、PipeWire 配置和许可证，并拒绝包含运行身份或状态的输入。默认下载并校验固定版本 fnpack；也可通过 `FN_PACK` 指定已有工具。使用开发者签名时同时设置 `FN_PACK_KEY` 和 `FN_PACK_CERT`，密钥应留在构建环境中。

输出为 `out/packages/airan-desk-fnos-<版本>-<架构>.fpk` 和对应 `.sha256`。

## 验证

```bash
(cd packaging/fnos/runtime && go test -race ./... && go vet ./...)
python3 packaging/fnos/tests/smoke.py out/packages/airan-desk-fnos-1.2.1-amd64.fpk
(cd out/packages && sha256sum -c airan-desk-fnos-1.2.1-amd64.fpk.sha256)
```

成品冒烟测试需在相同架构的 Linux 上执行，并安装 Qt 所需的 X11、GL、字体、ALSA 和 D-Bus 系统运行库。测试实际提取的完整包、原生信令注册、管理员鉴权、显式授权、身份持久化、验证码更换、通知及停止重启。ARM64 可在本机 ARM64 Linux 或配置好的 QEMU 环境验证；模拟器验证不能替代目标 NAS 实测。

可选的真实连接测试使用 `packaging/fnos/tests/rtc_probe.py`（需要 `aiortc`、`websockets`）：

```bash
python3 packaging/fnos/tests/rtc_probe.py wss://signal.example.com/ws <设备码> \
  --path /var/apps/airan-desk/var/state
```

通过标准输入提供连接验证码，或设置 `AIRAN_RTC_PASSWORD`。脚本只检查指定目录，并通过固定命令验证终端以非 root 用户运行，最后退出终端和会话。不要把验证码写入命令行参数或 CI 日志。

## GitHub Actions

`.github/workflows/build-packages.yml` 的 `fnos-package` 矩阵使用 x86_64 和 ARM64 原生 runner。它下载**同一次工作流**生成的相应 glibc 2.27 portable artifact，再构建并验证 FPK，上传两个架构的包和 SHA256。既有 release job 会收集这些产物。

工作流必须提交并推送后才会在 GitHub 生效。本地 YAML 或打包验证不代表远端 Actions 已运行。

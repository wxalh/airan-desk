# Privacy And Local Data / 隐私与本地数据

## English

Airan-Desk does not include telemetry, analytics, advertising, or an automatic
update service. The author does not operate a public signaling, STUN, TURN,
SFU, or relay service and does not receive application data through the
software as distributed.

A fresh installation has no signaling or ICE endpoint. A local user must
explicitly configure infrastructure before the application attempts a network
connection. The selected operators, not the author, receive the data needed to
provide those services:

- A signaling server receives connection metadata such as device identifiers,
  host names, installation identifiers, source addresses, SDP/ICE negotiation
  data, session routing data, and the current authentication digest.
- A STUN or TURN service observes network addresses and connection metadata. A
  TURN service also relays encrypted WebRTC traffic.
- The connected peer receives only the desktop, audio, clipboard, file, input,
  or terminal data enabled and used during that session.

The controlled device stores configuration, identity, and audit files locally
under the application and user data directories. These files use the operating
system's normal filesystem permissions. The identity file contains the locally
displayed verification code. Audit files can include peer identifiers, source
addresses, file names and hashes, recognizable ordinary-shell commands, session
events, and transfer summaries. Daily audit files are retained for at least six
calendar months; the local device owner or administrator is responsible for
local storage and retention.

The person or organization deploying Airan-Desk and its private infrastructure
is responsible for providing any notices, obtaining any authorization or
consent, selecting lawful retention periods, protecting server logs, handling
individual-rights requests, and assessing cross-border transfers required by
applicable law.

## 简体中文

Airan-Desk 不包含遥测、分析、广告或自动更新服务。作者不运营公共信令、
STUN、TURN、SFU 或其他中继服务，也不会接收本应用的数据。

新安装不预置信令或 ICE 地址。本地用户必须明确配置基础设施后，程序才会
尝试联网。相关服务所必需的数据由使用者选择的服务运营者而非作者接收：

- 信令服务器会接收设备标识、主机名、安装标识、来源地址、SDP/ICE 协商
  数据、会话路由数据和当前认证摘要等连接信息。
- STUN 或 TURN 服务会获知网络地址及连接元数据；TURN 还会中继已加密的
  WebRTC 流量。
- 连接对端只会接收该会话中实际启用和使用的桌面、音频、剪贴板、文件、
  输入或终端数据。

被控设备会在应用目录和用户数据目录中本地保存配置、身份文件和审计文件，
使用操作系统的普通文件权限。身份文件包含本机显示的验证码。审计文件可能
包含对端标识、来源地址、文件名和哈希、可识别的普通 shell 命令、会话事件及
传输汇总。审计文件按天生成，至少保留六个自然月；本地存储和保存期限由本机
设备所有者或管理员负责。

部署 Airan-Desk 及其私有基础设施的个人或组织负责履行适用法律要求的告知、
授权或同意、保存期限确定、服务器日志保护、个人权利请求处理及数据出境评估
等义务。

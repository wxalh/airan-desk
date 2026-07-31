# Linux 编译指南

简体中文 | [English](build_linux.en.md)

## 当前构建模型

Linux 构建使用 Qt 5 UI、Google WebRTC 静态包、可选 FFmpeg shared/dev 运行时包、静态 `spdlog` 和静态 `libvterm`。项目要求 CMake 3.21+、C++20 和 GCC/G++ 11+。

| 架构 | WebRTC target | Preset | FFmpeg 默认目录 |
| --- | --- | --- | --- |
| x64 | `libwebrtc::linux_x64_m144_gnu` | `linux-x64` | `third_party/ffmpeg-builds/ffmpeg-n8.1-latest-linux64-lgpl-shared-8.1` |
| arm64 | `libwebrtc::linux_arm64_m144_gnu` | `linux-arm64` | `third_party/ffmpeg-builds/ffmpeg-n8.1-latest-linuxarm64-lgpl-shared-8.1` |
| armhf | `libwebrtc::linux_armhf_m144_gnu` | `linux-armhf` | `third_party/ffmpeg-builds/ffmpeg-n8.1-latest-linuxarmhf-lgpl-shared-8.1` |

说明：

- Linux 默认使用 GNU libstdc++ ABI 的 WebRTC 包，可通过 `-DWEBRTC_LINUX_STL=libcxx` 切换到 libc++ 包，前提是本地包目录存在对应库。
- Linux 默认静态链接 `libstdc++` 和 `libgcc`，由 `-DSTATIC_LINK_LIBSTDCPP=ON/OFF` 控制。
- FFmpeg backend 默认为启用；开发构建缺少头文件或 `.so` 运行时时会明确警告并降级为 WebRTC 内部 codec。正式包装强制启用，并要求完整运行库与来源/许可 metadata。
- Qt 系统翻译会自动从 `qmake -query QT_INSTALL_TRANSLATIONS`、Qt 安装前缀、`/usr/share/qt5/translations` 等位置查找，也可以用 `-DAIRAN_QT_TRANSLATIONS_DIR=/path/to/translations` 指定。

## 系统依赖

### 基础构建依赖

```bash
sudo apt update
sudo apt install \
    build-essential \
    cmake \
    git \
    pkg-config \
    qtbase5-dev \
    libqt5svg5-dev \
    libqt5websockets5-dev \
    qttools5-dev \
    qttools5-dev-tools \
    libasound2-dev \
    zlib1g-dev \
    libx11-dev \
    libxext-dev \
    libxdamage-dev \
    libxfixes-dev \
    libxcomposite-dev \
    libxrandr-dev \
    libxtst-dev
```

Ubuntu 18.04 默认 `g++` 是 7.5，不能编译当前 WebRTC m144 头文件。CMake 最低版本是 3.21+；GCC/G++ 需要 11 或更新版本。

### Ubuntu 18.04 安装 GCC/G++ 11

如果系统源没有 `gcc-11` / `g++-11`，可以启用 Ubuntu Toolchain PPA：

```bash
sudo apt install software-properties-common
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt update
sudo apt install gcc-11 g++-11
```

默认编译器过旧时，请通过 `CMakeUserPresets.json` 覆盖这些 cache variables：

- `CMAKE_C_COMPILER`：`gcc-11`
- `CMAKE_CXX_COMPILER`：`g++-11`

如果希望系统默认 `gcc` / `g++` 指向 11，可以注册 alternatives：

```bash
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 110 \
  --slave /usr/bin/g++ g++ /usr/bin/g++-11
gcc --version
g++ --version
```

切换编译器后请删除旧构建目录重新配置，避免 CMake 继续使用缓存里的旧编译器。

Qt DBus 是可选组件。发行版 Qt 通常已随 `qtbase5-dev` 提供；如果使用精简安装并启用 Wayland/portal 相关能力，请确认 Qt DBus 开发包可用。

### 可选 Wayland/PipeWire 采集依赖

CMake 会用 `pkg-config` 探测以下开发包。全部找到时启用 PipeWire/portal 采集源码；缺失时自动回退为未启用，不阻塞构建。

```bash
sudo apt install \
    libglib2.0-dev \
    libpipewire-0.3-dev \
    libspa-0.2-dev \
    libgbm-dev \
    libdrm-dev \
    libegl1-mesa-dev \
    libgl1-mesa-dev
```

## 第三方二进制包

大型二进制包不提交到仓库，需要放在本地 `third_party` 下：

- `third_party/webrtc`：Google WebRTC 静态包，必须包含当前架构的 `libwebrtc::linux_*_m144_*` target。
- `third_party/ffmpeg-builds/<package>`：FFmpeg 8.1 LGPL shared/dev 包。没有该包时 FFmpeg codec backend 禁用。
- `third_party/linux-runtime/lib`：可选 Linux 私有运行库目录。CI 会通过 `tools/build_linux_runtime_libva.sh` 构建 libva，并通过 `tools/build_linux_runtime_pipewire.sh` 在 Ubuntu 18.04 基线上构建 PipeWire 0.3.65 客户端、模块和 SPA support。这些运行库会保持目录结构进入 `/opt/airan-desk/lib` 和 portable 包。
- `third_party/linux-runtime/pipewire-sdk`：上述 PipeWire 构建生成的头文件和 pkg-config SDK。配置时将此目录加入 `CMAKE_PREFIX_PATH`，可在系统没有 `libpipewire-0.3-dev` 时启用 PipeWire 后端。

更多路径和下载说明见 [dependencies.md](dependencies.md)。

## 获取源码

```bash
git clone <repository-url>
cd airan-desk
git submodule update --init --recursive
```

## 使用 CMake Presets

需要 CMake 3.21+。
Preset 默认使用配置里的 `gcc` / `g++` 名称。如果它们指向低于 11 的编译器，请更新系统 alternatives，或通过 `CMakeUserPresets.json` 覆盖 `CMAKE_C_COMPILER` 和 `CMAKE_CXX_COMPILER`。

```bash
cmake --preset linux-x64
cmake --build --preset linux-x64
```

arm64 / armhf：

```bash
cmake --preset linux-arm64
cmake --build --preset linux-arm64

cmake --preset linux-armhf
cmake --build --preset linux-armhf
```

## 交叉编译 arm64 / armhf

安装交叉编译器：

```bash
sudo apt install \
    gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
    gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
```

交叉编译器也需要 GCC/G++ 11+ 级别的工具链；旧版 7.x 工具链会在 WebRTC m144 头文件处失败。内置 `linux-arm64` 和 `linux-armhf` preset 会选择对应 GNU 交叉编译器名称和默认 sysroot 路径。本地目标 Qt、sysroot、WebRTC 或 FFmpeg 目录不同时，请通过 `CMakeUserPresets.json` 覆盖。

## 输出与安装

默认输出：

```text
out/build/linux-x64/release/airan-desk
```

构建输出目录会复制：

- `conf/`
- `locale/`，项目自带翻译。
- `translations/`，Qt 系统翻译，找到时复制。
- FFmpeg `.so*`，找到 FFmpeg runtime 包时复制。

### 构建 portable 包

`tools/build_package.sh` 生成可直接运行、也可安装到系统的统一 portable 包。脚本不能无参数执行，至少需要 `--configure-preset`；以下命令假定 WebRTC、FFmpeg 和 Linux 私有运行库已经准备完成：

发布 Linux 产物请使用这个 portable 打包流程。`cmake --build --preset linux-x64 --target install` 是开发/系统安装流程，不会生成完整 portable 包。

```bash
bash tools/build_package.sh \
  --configure-preset linux-x64 \
  --build-preset linux-x64 \
  --portable-package-name airan-desk-linux-x64-portable \
  --portable-glibc-floor 2.27
```

portable 打包器会收集 Qt Core/Gui/Widgets/Network/Svg/WebSockets、Qt XCB 和图片插件、启用时的 FFmpeg、可选私有运行库及其递归 ELF 依赖。它使用 `patchelf` 为主程序、私有库和插件写入相对 RPATH。必须通过包内的 `./airan-desk` 启动脚本运行；该脚本会把 `lib/`、`lib/codecs/` 和兼容目录放到 `LD_LIBRARY_PATH` 最前面，因为 FFmpeg 运行时启用时是通过裸库名 `dlopen()` 加载的。

#### glibc 包选择与常见发行版

先在目标机器执行：

```bash
ldd --version | head -n 1
```

根据输出选择包：

| portable 包 | 架构 | 目标 glibc | 常见发行版默认版本示例 | 编解码差异 |
| --- | --- | --- | --- | --- |
| `airan-desk-linux-x64-glibc2.17-portable.tar.gz` | x64 | 2.17 及以上；主要面向 2.17-2.26 | RHEL/CentOS 7（2.17）、Ubuntu 14.04（2.19）、Debian 8（2.19）、SLES 12/openSUSE Leap 42（约 2.22）、Ubuntu 16.04（2.23）、Debian 9（2.24）、Amazon Linux 2（2.26） | 不携带 FFmpeg runtime，使用 WebRTC builtin/Internal 软件编解码兜底 |
| `airan-desk-linux-*-glibc2.27-portable.tar.gz` | x64、arm64、armhf | 2.27 及以上 | Ubuntu 18.04（2.27）、Debian 10/RHEL 8/Rocky 8/AlmaLinux 8（2.28）、Ubuntu 20.04/Debian 11（2.31）、RHEL 9/Rocky 9/AlmaLinux 9/Amazon Linux 2023（2.34）、Ubuntu 22.04（2.35）、Debian 12（2.36）、Ubuntu 24.04（2.39） | 携带 FFmpeg runtime，并按运行环境探测可用的硬件编解码器 |

发行版版本仅为常见默认值示例，不是运行保证。衍生发行版、国产发行版和厂商回补系统可能保留不同的 glibc 版本，必须以目标机器的 `ldd --version` 为准。glibc 低于 2.17 当前不支持。CentOS 7、Ubuntu 14.04/16.04、Debian 8/9 等系统已经结束常规安全维护；兼容包只解决二进制运行下限，不代表这些系统仍适合联网部署。

2.17 包也可以在 glibc 2.27 及以上系统运行，但新系统应优先选择 2.27 包，以保留 FFmpeg runtime 和硬件编解码探测。glibc 只是其中一个下限：目标系统仍需提供兼容的 Linux 内核、图形驱动、桌面会话和系统服务。

当前 CI 在 Ubuntu 18.04 容器内生成 x64、arm64 和 armhf `GLIBC_2.27` portable 包，并在 CentOS 7 容器内生成 x64 `GLIBC_2.17` portable 包。portable 可执行文件同时编入 X11 与 PipeWire/portal 后端，并设置包内 `PIPEWIRE_MODULE_DIR`、`SPA_PLUGIN_DIR` 和 `PIPEWIRE_CONFIG_DIR`。目标 Wayland 桌面仍需提供正在运行的 PipeWire 服务、`xdg-desktop-portal` 和对应桌面 portal 后端。

#### 本地复现 CI 包

GitHub runner 虽然是 Ubuntu 22.04，但 Linux 产物实际在对应基线容器内构建。直接在 Ubuntu 22.04 主机编译会链接该主机的 glibc，不能得到等同的 2.27 或 2.17 兼容包。最准确的复现方法是执行 `.github/workflows/build-packages.yml` 中对应 job 的完整 `docker run` 块。

Windows 主机上的环境选择：

- 只做当前发行版的 Linux x64 编译、命令行测试和一般源码验证时，启用 WSL2 并安装 Ubuntu 即可，不强制需要 Docker。建议把仓库克隆到 WSL2 自己的 ext4 文件系统（例如 `~/src/airan-desk`），避免直接在 `/mnt/c` 或 `/mnt/d` 下编译带来的权限、大小写和 I/O 性能差异。
- 要生成与 CI 相同兼容下限的 portable 包时，需要容器引擎。WSL2 中直接编译会使用该 WSL 发行版自身的 glibc，不能代替 Ubuntu 18.04 或 CentOS 7 基线容器。可以使用 Docker Desktop 的 WSL2 backend，也可以在 WSL2 内安装 Docker Engine；两者选择其一即可。
- WSLg 可以用于基本 GUI 启动测试，但 WSL2 的虚拟显示、音频、`uinput`、systemd、PipeWire 和 portal 环境与真实 Linux 桌面不同。屏幕采集、远程输入、登录后自启动及 Wayland portal 流程仍应在真实 Linux 桌面或虚拟机中验证。

进入已经安装依赖的 Ubuntu 18.04 x64 容器后，2.27 包的构建顺序为：

```bash
bash ./tools/build_linux_runtime_libva.sh
bash ./tools/build_linux_runtime_pipewire.sh
bash ./tools/prepare_third_party.sh "" linux linux-ubuntu18-x64-m144-gnu
bash ./tools/build_package.sh \
  --configure-preset linux-x64 \
  --build-preset linux-x64 \
  --portable-package-name airan-desk-linux-x64-glibc2.27-portable \
  --portable-glibc-floor 2.27 \
  --cmake-arg -DCMAKE_C_COMPILER=gcc-11 \
  --cmake-arg -DCMAKE_CXX_COMPILER=g++-11 \
  --cmake-arg "-DCMAKE_PREFIX_PATH=$PWD/third_party/linux-runtime/pipewire-sdk" \
  --cmake-arg -DAIRAN_REQUIRE_PIPEWIRE_CAPTURE=ON
```

进入已经安装 devtoolset-11 和 Qt 5 构建依赖的 CentOS 7 x64 容器后，使用 conda-forge GCC 构建 2.17 包。CentOS devtoolset-11 的 libstdc++ 禁用了双 ABI，而项目内的 GNU WebRTC 包要求 C++11 ABI：

```bash
source /opt/rh/devtoolset-11/enable
bash ./tools/build_linux_runtime_libva.sh
bash ./tools/build_linux_runtime_pipewire.sh
bash ./tools/prepare_third_party.sh "" webrtc linux-centos7-x64-m144-libcxx
curl -Ls https://micro.mamba.pm/api/micromamba/linux-64/latest \
  | tar -xj -C /usr/local/bin --strip-components=1 bin/micromamba
MAMBA_ROOT_PREFIX=/opt/micromamba micromamba create -y \
  -p /opt/airan-toolchain \
  -c conda-forge \
  gcc_linux-64=11.4.0 gxx_linux-64=11.4.0 sysroot_linux-64=2.17
conda_toolchain=/opt/airan-toolchain/bin
QMAKE=qmake-qt5 bash ./tools/build_package.sh \
  --configure-preset linux-x64 \
  --build-preset linux-x64 \
  --portable-package-name airan-desk-linux-x64-glibc2.17-portable \
  --portable-glibc-floor 2.17 \
  --cmake-arg -DWEBRTC_LINUX_COMPAT=centos7 \
  --cmake-arg -DWEBRTC_LINUX_STL=libcxx \
  --cmake-arg "-DCMAKE_C_COMPILER=$conda_toolchain/x86_64-conda-linux-gnu-gcc" \
  --cmake-arg "-DCMAKE_CXX_COMPILER=$conda_toolchain/x86_64-conda-linux-gnu-g++" \
  --cmake-arg -DCMAKE_SYSROOT=/ \
  --cmake-arg "-DCMAKE_EXE_LINKER_FLAGS=-B/usr/lib64 -L/usr/lib64 -Wl,-rpath-link,/usr/lib64" \
  --cmake-arg "-DCMAKE_PREFIX_PATH=$PWD/third_party/linux-runtime/pipewire-sdk" \
  --cmake-arg -DAIRAN_REQUIRE_PIPEWIRE_CAPTURE=ON
```

两种包均输出到 `out/packages/`。打包器会扫描最终包中的全部 ELF；只要任一文件要求的 glibc 高于声明基线，构建就会失败，不会生成或上传标错兼容版本的产物。

每个 portable 包同时包含 `install-linux.sh` 和 `remove-linux.sh`。解压后可运行 `./airan-desk`，或执行 `sudo ./install-linux.sh` 安装桌面入口、自动启动、uinput 规则以及同一套私有运行库。

在 Ubuntu 18.04 基线上手动启用 portable PipeWire 构建：

```bash
bash tools/build_linux_runtime_pipewire.sh
cmake --preset linux-x64 \
  -DCMAKE_PREFIX_PATH="$PWD/third_party/linux-runtime/pipewire-sdk" \
  -DAIRAN_REQUIRE_PIPEWIRE_CAPTURE=ON
```

portable 包不携带 glibc、Linux 内核、OpenGL/EGL、DRM/GBM、Vulkan/CUDA、系统 PipeWire 服务或硬件驱动。启动脚本会检查 `60-airan-desk-uinput.rules`；缺失或版本不一致时自动请求管理员权限安装，成功后提示注销重新登录或重启。

系统安装版和 portable 版使用同一个 uinput 检查器：它会确保 `input` 组存在、把当前启动用户加入该组、加载 `uinput` 内核模块、刷新 udev 并等待设备节点出现。桌面会话优先通过 `pkexec` 请求权限，交互终端中回退到 `sudo`。新增的组成员关系必须注销重新登录或重启后才会进入应用进程。无法提权或当前内核没有 uinput 模块时应用仍会启动，并显示修复提示。`-v`、`-V`、`--version` 不触发安装；自动化环境可设置 `AIRAN_DESK_SKIP_UINPUT_SETUP=1`。

安装到系统：

```bash
sudo cmake --build --preset linux-x64 --target install
sudo udevadm control --reload-rules
sudo udevadm trigger
```

`install` 会安装：

- 可执行文件到 `${CMAKE_INSTALL_BINDIR}`，默认 `/opt/airan-desk/bin`。
- FFmpeg 和可选 third_party Linux 运行库到 `${CMAKE_INSTALL_LIBDIR}`，默认 `/opt/airan-desk/lib`。
- 桌面入口到 `${AIRAN_SYSTEM_DATADIR}/applications`，默认 `/usr/local/share/applications`。
- 当前用户自启动模板到 `${CMAKE_INSTALL_DATADIR}/airan-desk`。
- `uinput` udev 规则到 `/etc/udev/rules.d`，可用 `-DUDEV_RULES_INSTALL_DIR=...` 覆盖。
- 图标、默认配置、项目翻译和 Qt 翻译。

启用当前用户登录后自启动：

```bash
mkdir -p ~/.config/autostart
cp /opt/airan-desk/share/airan-desk/airan-desk-autostart.desktop ~/.config/autostart/airan-desk.desktop
```

取消自启动：

```bash
rm -f ~/.config/autostart/airan-desk.desktop
```

## 运行配置

运行前编辑 `conf/common.ini`：

```ini
[signal_server]
wsUrl = wss://your-signal-server.example/ws
```

从构建输出目录启动：

```bash
cd out/build/linux-x64/release
./airan-desk
```

无界面模式：

```bash
./airan-desk --no-ui
```

远程桌面建议在真实桌面会话内启动。桌面会话会继承 `DISPLAY` / `WAYLAND_DISPLAY` / DBus 等环境，屏幕采集和输入注入更可靠。

## 常见问题

### CMake 版本过旧或不认识 `--preset`

这是版本限制。请升级到 CMake 3.21+ 并使用 presets 构建。

### GCC 7.5 在 WebRTC 头文件报 `non-constexpr`

当前 WebRTC m144 头文件需要更新的 GCC C++20 支持。请升级到 GCC/G++ 11+，并重新配置干净的构建目录：

```bash
rm -rf out/build/linux-x64
cmake --preset linux-x64
cmake --build --preset linux-x64
```

### 找不到 Qt5 Svg 或 WebSockets

```bash
sudo apt install libqt5svg5-dev libqt5websockets5-dev
```

### 找不到 Qt translations

发行版 Qt 常见路径是 `/usr/share/qt5/translations`。如果自动探测失败，请通过 `CMakeUserPresets.json` 设置 `AIRAN_QT_TRANSLATIONS_DIR`。

### FFmpeg backend 被禁用

确认 `AIRAN_FFMPEG_ROOT` 指向包含 `include/` 和 `lib/` 的 FFmpeg shared/dev 包；缺少该包时程序会使用 WebRTC 内部 codec。

### 无法注入键鼠

确认已安装 uinput 规则并重新加载：

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

重新登录桌面会话后再测试。

### Wayland 下采集异常

优先安装可选 PipeWire/portal 开发包后重新配置构建，并在真实桌面会话中运行。不要从 SSH 或 system 服务中直接启动桌面采集流程。

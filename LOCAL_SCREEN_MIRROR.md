# 本地主板：小智 2.5.2 + USB OLED 同步及文字对话

上游基线：[v2.5.2](https://github.com/78/xiaozhi-esp32/releases/tag/v2.5.2)，提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。本地分支：`local/screen-mirror`。

## 屏幕同步按需开启

2026-10-29 根据用户反馈，主板端同步已关闭并保存到 NVS，重启后保持关闭。关闭跳过像素复制、编码及 USB 帧发送，实体 OLED 和文字对话继续工作。仅在用户明确要求时通过本机 `POST /api/mirror` 发送 `{"enabled":true}` 开启；连接时的 `{}` 查询不会开启。同期保持三条唤醒词、阈值和音频处理设置不变，用于判断同步是否与播放卡顿有关，未将其预先认定为卡顿根因。

## 大冷总管唤醒词

2026-10-29 现场复核后改用完整自定义唤醒应用及匹配资源，并增加“你好总管”“总管总管”两个短语，主显示名统一为“大冷总管”，阈值保持 20。诊断版确认物理麦克风采音、MultiNet 命中及真实回复；正常版关闭每秒诊断输出。用户已分别确认主词及两个新增短语均能唤醒，现场结果记录在最新发布包。此前只替换资源的方式未通过实体唤醒验收，后续更新应使用完整配套构建。

2026-10-28 增加本机专用变体 `bread-compact-wifi-128x32-usb-mirror-devmac-daleng`，使用中文 MultiNet7 本地识别“大冷总管”（`da leng zong guan`），初始阈值 20。实读主板应用和旧资源与保存的构建一致，因此本次只更新资源分区，原应用及开发身份继续使用；新变体也已完整编译。模型初始化、主机测试、声学测试和真人现场效果分别记录，不能用 USB 文字输入代替唤醒验收。详见 [大冷总管唤醒词实施与回退](docs/daleng-wake-word.md)。

## 本机独立开发身份

当前实物已由用户确认使用 128×32 OLED。新增构建 `bread-compact-wifi-128x32-usb-mirror-devmac` 保留 Quad PSRAM、USB 镜像和远程文字输入，并在网络接口初始化前调用 ESP-IDF `esp_base_mac_addr_set()`，使用本地管理的单播软件 MAC `02:21:64:63:44:5A`。原厂 eFuse MAC 仍为 `74:4D:BD:A2:C4:2C`，代码只读取 eFuse。构建在运行时核对原厂 MAC，拒绝将这份固定开发身份用于另一块主板。

开发 UUID 为 `b67553bc-4483-4a0f-84a0-ce7e801e8bf7`。第一次应用身份时把原 UUID 保留在 `board.factory_uuid`，再设置 `board.uuid` 和 `board.dev_mac`；后续重启沿用同一身份。Wi-Fi 配置、资源、分区和其他设置保留。软件 MAC 只在该专用固件运行时应用，原有云端绑定记录不会因此删除。

```sh
source /Users/lengwang/code/esp32/tools/activate-esp-idf.sh
python3 scripts/build.py bread-compact-wifi --name bread-compact-wifi-128x32-usb-mirror-devmac
```

当前应用已在 0x20000 写入并通过 esptool 校验。启动日志确认开发身份生效，设备已在用户当前账号激活。重启后日志再次显示 `wifi:mode : sta (02:21:64:63:44:5a)` 与 `Activation done`；实读 NVS 验证新 UUID、原 UUID 备份和软件 MAC 标记持久化，Wi-Fi 配置与修改前一致。新智能体 MCP 接入点已与本机 DSH 连通，实机验证两轮提问、回答回传和同一 DSH 会话上下文，USB 镜像保留。

身份配置在 `.local/development-device-identity.json`。修改前即时 NVS 和旧应用备份位置记录在 `.local/backups/pre-development-identity-path.txt`；完整 Flash 备份位置在 `.local/backups/pre-rebind-path.txt`。恢复原状态需使用匹配的旧应用及 NVS 备份，不应只恢复旧应用后假定 UUID 也已恢复。

验证：111 项主机测试（含 MAC/UUID 格式验证）、17 项网页协议/对话测试通过；主板持续回传 128×32 OLED 成功写入的校验帧。新增模块及配置默认关闭，普通板型与原引脚保持不变。

小屏普通镜像变体名为 `bread-compact-wifi-128x32-usb-mirror`，本机当前使用上述 `-devmac` 变体。两者均保留原 `bread-compact-wifi` 的引脚和单一板工厂；使用 GPIO 41/42、SSD1306 0x3C、128×32 OLED、16 MB Flash、8 MB Quad PSRAM。官方普通构建的默认 Octal PSRAM 与当前实物不匹配，因此只在新增构建中覆盖 PSRAM 模式。

2026-10-28 用户观察到物理屏幕比 64 行预览矮约一半、圆形呈椭圆，因此从 128×64 改用 128×32 小屏布局。该选项同时改变 SSD1306 驱动的 multiplex/COM 配置、LVGL 布局和同步帧高度，未在网页中压缩画面。烧录后主板成功写入 OLED 并回传 128×32 帧，用户现场确认“比例正常了”，因此当前小屏采用此配置。I2C/USB 本身无法自动测出面板实际行数。原 `bread-compact-wifi-128x64-usb-mirror` 构建和已验证发布文件保留，可回退。

```sh
source /Users/lengwang/code/esp32/tools/activate-esp-idf.sh
python3 scripts/build.py bread-compact-wifi --name bread-compact-wifi-128x32-usb-mirror
```

同步代码在 `main/display/usb_screen_mirror.cc`：从 OLED 面板完成的 draw_bitmap 复制最终页格式像素，使用一个长度为 1 的队列覆盖旧帧，由低优先级任务以最多 4 fps 通过 USB 控制台发送。没有在 LVGL、音频或应用主循环中等待 USB。屏幕关闭与反色也同步到网页。

网页继续使用 `http://127.0.0.1:8765`，协议与原独立演示兼容。小智帧携带 `display_control=false`，网页因此隐藏演示文字控件，后端也拒绝这类指令；缩放、像素网格和保存 PNG 保留。

本地构建同时启用 `CONFIG_XIAOZHI_USB_TEXT_CHAT`。电脑把网页输入通过 macOS Tingting 合成为 16 kHz、60 ms Opus 音频，经 USB 交给主板使用正常手动收音通道发送。主板接收任务通过 USB Serial/JTAG 中断缓冲区读取命令，应用操作均调度到主循环；音频队列最多 8 包，回复队列最多 16 条，按请求 ID 回传实际 STT/TTS 文本。主板在远程输入期间关闭麦克风处理和唤醒检测，结束后恢复待机。未使用只适合短唤醒词的 detect 消息发送长文本。

网页一次发送一条，最长 500 字符；本地合成音频最长 60 秒，主板等待回复超时为 90 秒（接收音频或回复文本时刷新）。设备忙、断线、服务端错误、超时均显示失败，不伪造回复。聊天记录仅保存在电脑本地，最多 20 轮。相关源码在 `usb_text_chat.cc`、应用主循环和 `screen-mirror/speech_input.py`。新增构建启用此能力，其他构建默认关闭。

本构建关闭自动固件升级，避免官方普通固件覆盖本地同步功能；仍使用官方 OTA 接口获取服务配置和进行设备激活。手动固件升级会替换整个程序，后续应继续构建带同步的版本。

上游 v2 分区表与原 1.x/演示布局不同：NVS、otadata、phy_init 的起始地址保持一致；应用变为 0x20000 和 0x410000，各 0x3f0000 字节，资源分区从 0x800000 开始，8 MB。烧录前保存完整 Flash，升级时使用项目生成的分区表、OTA 选择数据和资源镜像。

原始 1.x 固件备份：`/Users/lengwang/code/esp32/.local/backups/snapshot-20261027-212741/flash.bin`。升级前演示备份位置记录在 `/Users/lengwang/code/esp32/.local/backups/pre-xiaozhi-path.txt`。可复核各目录的 `SHA256.txt`。完整恢复写入尚未测试。

验证命令：

```sh
python3 -m unittest discover -s scripts/tests -v
uvx clang-format --dry-run -Werror main/display/usb_screen_mirror.cc main/display/usb_screen_mirror.h main/display/usb_text_chat.cc main/display/usb_text_chat.h
```

在主机网页侧运行 `screen-mirror` 的协议和对话测试。当前 111 项固件主机测试、17 项网页侧测试通过，已实机验证远程文字请求和真实回复，同时保持 128×32 OLED 成功写入、约 4 fps 校验帧、零坏帧。DSH 插件另有 7 项回归测试通过。当前小屏物理比例已由用户确认正常；物理麦克风和扬声器听说效果尚未做现场体验检查。

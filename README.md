# ESP32 语音终端固件

当前版本：**2.5.24**。本仓库维护 OLED 与圆屏两种 ESP32-S3 语音终端固件，配合 [xiaozhi-server](https://github.com/lonycn/xiaozhi-server) 使用。

ESP32 负责采音、播放、唤醒、屏幕和设备控制；语音识别、模型对话与语音合成由 Server 提供。

## 当前硬件

| 硬件 | 编译变体 | 屏幕 | 内存 |
| --- | --- | --- | --- |
| OLED 板 | `bread-compact-wifi-128x32-ai-lan` | SSD1306 128×32 | 16 MB Flash / 8 MB Quad PSRAM |
| 圆屏板 | `sp-esp32-s3-1.28-box-ai-lan` | GC9A01 240×240 | 16 MB Flash / 8 MB Octal PSRAM |

圆屏与 OLED 使用各自的屏幕、音频和引脚配置。圆屏当前实机按非触摸模式运行，具体接线见对应板型源码。

## 主要功能

- “你好总管 / 总管总管 / 你好小云 / 小云小云”本地唤醒，支持停止、暂停和退出口令。
- WebSocket 语音对话、流式播放与设备端 MCP 工具。
- OLED 时钟、Wi-Fi 状态、表情与微动作；圆屏使用彩屏界面。
- 应用 OTA 按板型、版本和资源清单校验，设备身份与联网配置保存在 NVS。

当前两个 `ai-lan` 变体用于局域网联调；应用固件与配套 assets 应按同一发布版本使用。

## 编译

使用 ESP-IDF **6.0.1 或更新版本**，推荐 6.1；当前 ESP32-S3 构建已使用 6.0.1 验证。

```sh
source /path/to/esp-idf/export.sh
python3 scripts/build.py --list-boards

# OLED
python3 scripts/build.py bread-compact-wifi   --name bread-compact-wifi-128x32-ai-lan

# 圆屏
python3 scripts/build.py spotpear/sp-esp32-s3-1.28-box   --name sp-esp32-s3-1.28-box-ai-lan
```

以上两个变体共享 `profiles/daleng-cn.defaults`，默认中文界面及四条自定义唤醒词。显式传入 `--language` 或 `--wake-word` 可覆盖对应默认值；正式打包仍会检查四条唤醒词。

编译脚本会更新本机 `sdkconfig` 与 `build/`；以上命令只编译，不烧录。连接设备前需明确配置自己的 OTA 服务地址：OLED 受控变体默认留空，圆屏配置中的 LAN 地址是已有开发环境的值。OLED 的外部配置片段和打包方式见[发布说明](main/boards/bread-compact-wifi/RELEASE_AI.md)。

## 源码入口

- `main/application.*`：对话与设备主流程。
- `main/boards/`：板型、引脚与编译变体。
- `main/audio/`：音频、唤醒与播放控制。
- `main/display/`：OLED、彩屏与表情。
- `main/protocols/`、`main/mcp_server.*`：传输协议与设备工具。
- `profiles/`：跨板型共享的产品语言与唤醒配置。
- `scripts/`：构建、资源处理、打包和主机测试。

## 验证与版本

```sh
python3 -m unittest discover -s scripts/tests -v
```

[2.5.24 版本记录](docs/releases/2.5.24.md)说明源码基线与验证范围。`main` 为合并后的主分支，`Dev` 为日常开发分支；固件包、设备备份、Wi-Fi 凭据和本地运行文件不纳入源码提交。

本项目基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 定制，保留上游源代码和 MIT 许可，详见 [LICENSE](LICENSE)。

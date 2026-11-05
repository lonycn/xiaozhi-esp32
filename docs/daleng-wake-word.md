# 大冷总管：本机 ESP32-S3 自定义唤醒词

本方案使用 ESP-SR 2.4.7 的中文 MultiNet7 在设备本地识别 `da leng zong guan`，命中后沿用小智正常的唤醒、收音和云端对话流程。当前唤醒短语为“大冷总管”、“你好总管”和“总管总管”；显示及发送给服务器的名称统一为“大冷总管”。这是通用命令词模型的自定义唤醒用法，远场、噪声和误唤醒表现仍需现场测量。

## 构建

在 `firmware/xiaozhi` 执行：

```sh
source /Users/lengwang/code/esp32/tools/activate-esp-idf.sh
python3 scripts/build.py bread-compact-wifi --name bread-compact-wifi-128x32-usb-mirror-devmac-daleng
```

这是当前实物的专用变体，继承原 `-devmac` 配置：128×32 OLED、Quad PSRAM、USB 屏幕同步及文字输入、关闭自动固件升级、原开发身份。固定开发身份只适用于当前主板，不可直接复制给其他设备。

主词为 `da leng zong guan`，`CONFIG_CUSTOM_WAKE_WORD_ALIASES` 配置两个附加短语：`ni hao zong guan;zong guan zong guan`。附加短语以分号分隔，空白项忽略、重复项去重，全部映射到主词显示名称。资源包和无资源分区的后备初始化都支持该配置。

词条和模型选择保存在 `main/boards/bread-compact-wifi/config.json`，由构建脚本生成配置，不直接修改生成的 `sdkconfig`。配置使用 `CONFIG_USE_CUSTOM_WAKE_WORD=y`、`CONFIG_SR_MN_CN_MULTINET7_QUANT=y`，初始阈值为 20（写入资源索引后为 0.2）。阈值越低越敏感，不代表识别成功率。

构建生成的 `build/generated_assets.bin` 必须包含：

- `srmodels.bin` 中的 `mn7_cn` 和 `fst`，不打包旧 WakeNet 模型。
- `index.json` 中 `multinet_model.language=cn`、`duration=3000`、`threshold=0.2`。
- 三个唤醒条目：`da leng zong guan`、`ni hao zong guan`、`zong guan zong guan`，每项均为 `text="大冷总管"`、`action="wake"`。
- 原 OLED 中文字体。

资源构建现在拒绝缺失或空的 MultiNet 模型目录（包括 `fst`）、空词条及无效阈值，避免输出缺少唤醒能力的资源包。

## 本机安装与回退

当前交付应使用上述专用变体的完整应用及匹配资源。2026-10-28 曾尝试仅更新资源：虽然模型和词条初始化成功，但 2026-10-29 用户报告实体唤醒无反应。随后完整重编译并更新启用自定义唤醒的诊断应用，实际记录到麦克风输入、MultiNet 检测和待机唤醒，用户确认能唤醒。由于应用配置、诊断日志和重启同时发生，未单独定位先前失败根因，不能据此承诺资源更新足以完成此切换。

本机应用分区为 `0x20000`，大小 `0x3f0000`；资源分区为 `0x800000`，容量 8 MiB。地址必须与实际分区表核对，不能直接套用于其他固件。本次不清空 NVS，不重新激活，也不修改原板型引脚。

本次发布目录由以下文件记录：

`/Users/lengwang/code/esp32/.local/releases/daleng-wakeword-path.txt`

修改前完整备份目录由以下文件记录：

`/Users/lengwang/code/esp32/.local/backups/pre-daleng-wakeword-path.txt`

当前三短语发布目录包含 `xiaozhi.bin`、`assets.bin`、资源检查结果、构建和测试日志，以及可恢复到本次已确认可唤醒诊断版的 `rollback-diagnostic-app.bin` / `rollback-singleword-assets.bin`。联网及身份配置另有本机私有完整备份。诊断版路径记录在 `.local/releases/audio-diagnostics-path.txt`，修改前完整 Flash 路径记录在 `.local/backups/pre-audio-diagnostics-path.txt`。

回退应使用发布记录指定的匹配应用与资源组合。只回写旧资源不能恢复已经替换的应用；完整回退需要匹配的应用、资源及分区表。备份中的联网及身份配置不应清空。

诊断构建为 `bread-compact-wifi-128x32-usb-mirror-devmac-daleng-diag`，额外启用 `CONFIG_XIAOZHI_AUDIO_DIAGNOSTICS`，每级最多每秒一行，报告麦克风/AFE 音量与 MultiNet 计数，不发送原始录音。正常构建关闭持续诊断输出。

## 验证边界

主机测试、构建通过和模型初始化成功分别记录；它们不能代替物理麦克风的唤醒验收。USB 文字输入会绕过本地唤醒，成功收到文字对话回复也不能当作唤醒词识别成功。

现场应分别检查：

1. 启动日志实际列出 `mn7_cn`、`fst` 及“大冷总管”命令，进入待机且没有异常重启。
2. 真实麦克风输入“大冷总管”后出现检测命中，进入收音并得到真实回复。
3. “你好小智”和相近词的误触发、不同说话人及 1 米/3 米距离、风扇等环境噪声。
4. 连续唤醒、对话结束回到待机、重启、播放期间的行为和断线恢复。
5. 128×32 OLED、USB 校验帧、远程文字输入和原 DSH 对话接入。

发布目录的验收记录区分合成语音声学测试、远程文字对话和真人现场测试；没有实际完成的项目保留为待验收。

## 本次结果（2026-10-28）

- ESP-IDF v6.0.1 完整构建通过，116 项固件主机测试及 17 项屏幕/文字对话测试通过。
- 新资源 2,951,600 字节，容量上限 8,388,608 字节；实际解包核对词条、模型、阈值和原字体。
- 仅写入 `0x800000` 的资源镜像，esptool 校验成功。随后读回此前的完整 8 MiB，与修改前 Flash 逐字节一致，包括应用、NVS、OTA 选择数据和分区表。
- 首次启动及重启均实际列出 `mn7_cn`、`fst`、`da leng zong guan` / “大冷总管”，检测引擎为 MultiNet，设备完成激活。
- 主板远程文字输入收到真实回复；显式请求 DSH 工具后，原“小智语音”会话新增“大冷总管28”测试口令及真实回答。
- OLED 实际回传 128×32 成功写入帧，约 4 帧/秒，零坏帧。
- 电脑当前输出音量 10，播放合成语音“你好小智”和“大冷总管”均未观察到检测命中；设备与扬声器的距离未知，该结果不足以确认声学性能或旧词是否拒绝唤醒。用户当时不在设备旁，真人唤醒、距离、噪声、误触发和语音打断仍待现场验收。

## 现场复核（2026-10-29）

用户确认 BOOT 短按后的语音提问能收到正常回答。诊断固件实际记录到非零采音、每秒约 16,000 个采样和 31–32 次 MultiNet 检测；出现“大冷总管”命中、待机进入对话和回复，用户确认能唤醒，但认为主词识别不够精准，因此增加“你好总管”和“总管总管”。新增短语保持阈值 20，验收以资源索引、启动后实际激活的命令列表和现场命中分别记录。

2026-10-29 三短语正常版本刷入后，用户现场反馈“你好总管”和“总管总管”两个新增短语都能唤醒。此项为真人实测确认；距离、噪声与长时间误触发率仍未做量化验收。

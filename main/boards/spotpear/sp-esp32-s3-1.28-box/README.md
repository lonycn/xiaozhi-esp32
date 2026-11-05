# ESP32-S3 1.28 英寸圆屏

本目录保留圆屏板的 GC9A01 240×240 显示和 ES8311 音频配置。实际引脚见 `config.h` 与板型实现，不能使用 OLED 板的接线或布局替代。

## 2.5.23 局域网变体

- 变体：`sp-esp32-s3-1.28-box-ai-lan`。
- 芯片：ESP32-S3，16 MB Flash，8 MB Octal PSRAM。
- 本地三唤醒词、停止/退出口令与受控发行清单校验。
- 保留各设备自己的 MAC、UUID 和 Wi-Fi；USB 保留日志。
- 当前实机未检测到触摸芯片，使用已有非触摸回退。

```sh
python3 scripts/build.py spotpear/sp-esp32-s3-1.28-box   --name sp-esp32-s3-1.28-box-ai-lan
```

LAN OTA 地址在变体配置中，应按实际开发环境设置。普通上游变体 `sp-esp32-s3-1.28-box` 继续保留。以上命令只编译，不烧录。

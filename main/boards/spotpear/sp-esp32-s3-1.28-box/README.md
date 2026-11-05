【产品简介】
】支持触摸
】支持充电
】独特外形设计
产品链接1：https://spotpear.cn/shop/ESP32-S3-N16R8-AI-DeepSeek-XiaoZhi-XiaGe-Qwen-DouBao-1.28-inch-LCD.html
产品链接2：https://spotpear.cn/shop/ESP32-S3-N16R8-AI-DeepSeek-XiaoZhi-XiaGe-Qwen-DouBao-1.28-inch-Round-LCD-BOX-TouchScreen.html
# 编译配置命令

**配置编译目标为 ESP32S3：**

```bash
idf.py set-target esp32s3
```

**打开 menuconfig：**

```bash
idf.py menuconfig
```

**选择板子：**

```
Xiaozhi Assistant -> Board Type -> Spotpear ESP32-S3-1.28-BOX
```

**编译：**

```bash
idf.py build
```

## 本地大冷总管局域网变体

`sp-esp32-s3-1.28-box-ai-lan` 使用已验证的 2.5.23 业务源码、16 MB Flash 和 8 MB Octal PSRAM。保留此板 GC9A01 240×240 屏幕与 ES8311 引脚。使用本机局域网 OTA/网关、自定义大冷总管唤醒和现有停止/退出控制；固件要求校验发行清单，USB 保留日志。保留各板自己的 MAC、UUID 与 Wi-Fi 配置，不复制旧 OLED 板的身份。触摸芯片不存在时继续使用现有非触摸回退。

构建：`python3 scripts/build.py spotpear/sp-esp32-s3-1.28-box --name sp-esp32-s3-1.28-box-ai-lan`。本变体使用彩屏界面，OLED 专用的 128×32 像素绘图不适用于此屏幕。

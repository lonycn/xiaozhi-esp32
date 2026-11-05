# 128×32 AI 正式固件候选

`bread-compact-wifi-128x32-ai-release` 是独立 OTA `BOARD_NAME`，板型仍为 `bread-compact-wifi`，仅面向 ESP32-S3、16 MB Flash、Quad PSRAM 和 128×32 OLED。它使用“大冷总管 / 你好总管 / 总管总管”三条 MultiNet7 唤醒词。USB 屏幕镜像、USB 文本、软件开发身份及持续音频诊断关闭。自动更新由设备的严格发布清单验证和 Server 的 `automatic` 策略共同控制；开发变体不受此配置影响。该变体依赖 `CONFIG_XIAOZHI_CONTROLLED_RELEASE`、`CONFIG_XIAOZHI_REQUIRE_RELEASE_MANIFEST` 和 `CONFIG_XIAOZHI_REQUIRE_TTS_TURN_TAGS` 实现，缺任一项不能发布。

## 隔离构建及离线核验

使用已提交、版本已递增的源码提交创建单独 Git worktree。不要在正在使用的源码目录运行构建：构建脚本会修改 `sdkconfig` 和 `build/`。在该 worktree 中激活 ESP-IDF 6.0.1 或更新版本，准备由运营方确认归属的 HTTPS OTA 地址；不得使用公共默认地址、URL 账号密码或带凭据查询串。下面的变量均由操作者赋值，配置片段及输出目录放在 worktree 外：

```sh
python3 scripts/package_bread_ai.py prepare-config \
  --snapshot "$RELEASE_SNAPSHOT" --output "$RELEASE_FRAGMENT" --ota-url "$TRUSTED_OTA_URL"

# 在 $RELEASE_SNAPSHOT 目录执行；仅生成构建文件，不连接设备。
idf.py -DIDF_TARGET=esp32s3 \
  -DBOARD_NAME=bread-compact-wifi-128x32-ai-release \
  "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;$RELEASE_FRAGMENT" build

python3 scripts/package_bread_ai.py package \
  --snapshot "$RELEASE_SNAPSHOT" --output "$RELEASE_OUTPUT" \
  --ota-url "$TRUSTED_OTA_URL" --app-version "$APP_VERSION" \
  --previous-version "$PREVIOUS_VERSION" --assets-version "$ASSETS_VERSION"
python3 scripts/package_bread_ai.py verify "$RELEASE_OUTPUT"
```

构建前必须在被提交的源码中递增 `PROJECT_VER`；工具要求 app descriptor、构建元数据和 `--app-version` 一致，并与操作者提供的 `--previous-version` 比较。它不代替服务端对已发布版本的持久化单调检查。worktree 中的源码必须是干净的已提交状态；外部片段只注入已审核 OTA 地址，不修改 Git 源码。构建环境、依赖和源码提交仍应另行归档；源码 SHA 和哈希提供可追溯性，不是固件签名或远程证明。

工具核对真实芯片、变体名、目标分区表、16 MB Flash、Quad PSRAM、编译配置、app 大小、构建来源、匹配的 assets 索引及 MultiNet7/FST 文件和三条唤醒命令。它仅从构建产物的固定组件重建 `first-flash.bin`，不会读取或复制设备的 NVS、密钥或其他私人闪存。产物包括 `app.bin`、`assets.bin`、`first-flash.bin` 和 `manifest.json`，分别记录 SHA-256、字节数、硬件、版本、源码提交和 SDK。首次刷机镜像的 NVS 区域为全 `0xff`，**对已使用设备刷写整片镜像会擦除该区域，不得直接用于保留现有配置的设备**。

manifest 的 `firmware` 是供 Server 发布流程使用的草稿；固件 URL 和 assets URL 为 `null`，必须由受控发布流程填入可信地址并按实际授权机制处理，当前文件不构成可用发布或签名。assets 分区单副本：设备已有资源未匹配时，不能直接执行 app OTA；应使用经过审核的完整首刷/恢复方案，不能宣称 assets 独立更新具有原子回滚。上传、灰度、设备授权、断电回滚、音频、三唤醒词和真机验收均不在离线工具的通过范围内。

## 局域网开发候选

仅在隔离开发环境中，可对上述 `prepare-config`、`package` 和 `verify` 三个命令分别显式添加 `--development-lan`，同时将构建命令中的 BOARD_NAME 改为独立的 `bread-compact-wifi-128x32-ai-lan`；正式构建保持 `bread-compact-wifi-128x32-ai-release`。两者精确板型身份不同，不能相互打包或指派。此选项允许 OTA 地址使用无凭据的 HTTP，但主机必须是字面 IPv4，且属于 RFC1918 的 `10/8`、`172.16/12`、`192.168/16` 或 `127/8` 回环范围；不接受域名、公网、IPv6 或凭据。片段会开启 `CONFIG_XIAOZHI_RELEASE_ALLOW_LAN_HTTP=y`，包外层标记 `development_only=true`，普通正式 `verify` 会拒绝。正式配置保持 HTTPS 且该开关关闭。局域网候选只用于当前开发联调，不应作为生产发布或对外分发凭据。

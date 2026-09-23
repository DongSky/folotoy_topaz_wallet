[English](README.md)

# 托帕钱包

托帕钱包让 AI Passport 成为随身电子名片与收款码展示设备。在 Android 上编辑内容，通过蓝牙同步，随时展示你的名片。

## 功能

- 头像、昵称、简介与社交账号。
- 导入外部支付宝／微信收款码图片。
- 通过助记词导入钱包，生成收款二维码。
- 在 Android 上预览页面，通过蓝牙同步到设备。
- 保留设备的名片与图片入口，原版微信小程序兼容性仍需实机验收。

## 目录

`android/` 为配套应用，`main/` 为固件应用，`components/bsp/` 为板级驱动，`assets/fonts/` 为有许可证的中文字库，`tests/` 为主机测试，`tools/` 提供验证与固件打包。保留 `docs/`、`skills/` 与上游参考演示代码，便于使用工程指南及回归测试。详见[应用说明](docs/development/card-wallet-redesign.zh_CN.md)。

## 构建

固件使用 ESP-IDF 5.5.3、ESP32-C3 和 8 MB Flash：

```sh
. "$IDF_PATH/export.sh"
./tools/validate.sh
```

Android 使用 JDK 17+ 与 Android SDK 35，设置 `JAVA_HOME`、`ANDROID_HOME` 后执行：

```sh
cd android
./gradlew testDebugUnitTest assembleDebug lintDebug
./gradlew connectedDebugAndroidTest  # 需要测试设备或模拟器
```

不携带 `local.properties`、构建缓存和签名私钥。`dist/` 中为最新验证的 0.3-dev（版本号 3）调试 APK，不分发其调试签名私钥。本地重编 APK 可能使用不同签名，未必能直接覆盖原安装。

## 已备产物与数据保留

APK 与固件包可从 [Releases](https://github.com/DongSky/folotoy_topaz_wallet/releases) 下载。本地 `dist/` 已加入 Git 忽略规则，包含已检查的 APK、固件、相对路径清单、SHA-256 校验和中英文刷写说明。固件是编译代码，不是包含设备身份的备份。不包含原始 Flash／NVS 备份、个人钱包数据、串口日志、ELF／MAP 或旧 Git 历史。源码及版本托管于 [DongSky/folotoy_topaz_wallet](https://github.com/DongSky/folotoy_topaz_wallet)。

已有身份配置的设备必须使用清单中的分段镜像，保留 NVS 和资源。合并镜像用于明确的新设备初始化，不可覆盖已有配置。空白设备还需要合法的设备身份初始化和原版资源，本项目不分发这些数据。

构建和主机测试通过；Android 的 62 项单元测试与 10 项模拟器测试通过。更名固件未进行实机测试，真实 BLE、微信小程序、显示／扫码及音频验收仍待完成。

## 致谢

本项目基于 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 开发。感谢原项目贡献者、公开协议研究者及相关开源依赖。来源、贡献与许可证信息详见[致谢文档](docs/acknowledgements.zh_CN.md)。

## 许可证

保留 [LICENSE](LICENSE)（FoloToy MIT）与第三方声明，包括 `assets/fonts/` 字体许可证。测试使用公开虚构数据。

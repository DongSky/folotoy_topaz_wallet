[English](acknowledgements.md)

# 致谢

Topaz Wallet（托帕钱包）基于以下项目及贡献者的工作构建。

## 项目基础与参考

- **[FoloToy AI Passport](https://github.com/FoloToy/ai-passport)**：提供硬件平台、ESP-IDF 工程基础、BSP 驱动、参考演示、工程文档和验证工具。本项目保留 [LICENSE](../LICENSE) 中的原始 MIT 版权声明。
- **[FoloToy 产品文档](https://ai-passport.folotoy.cn/)**：为设备交互和深蓝／绿色视觉提供参考。原始 FoloToy 名称及产品素材归其权利人所有。
- **[neverleftWTF/folotoy-ai-passport-re](https://github.com/neverleftWTF/folotoy-ai-passport-re/tree/c739b4dd95588a0e41624eda05eb1655571d1a2f)**：提供原版 BLE 协议的公开研究（MIT，针对原版 1.0.0）。该研究作为参考，服务 UUID 一致并不能证明兼容；原版 1.0.3 的实现还使用了对应用行为的独立分析。本项目不分发含设备身份的备份。

## 固件与字体

| 项目 | 贡献 | 版本／许可证 |
| --- | --- | --- |
| [Espressif ESP-IDF](https://github.com/espressif/esp-idf) | ESP32-C3 运行环境和工具链 | 5.5.3；Apache-2.0 及内含组件许可证 |
| [Espressif 组件](https://github.com/espressif/esp-iot-solution) | LVGL 适配、按键与音频编解码支持 | 版本固定于 `dependencies.lock`，遵循各组件许可证 |
| [LVGL](https://github.com/lvgl/lvgl) | 设备 UI 与图片渲染 | 9.5.0；MIT |
| [Adobe 思源黑体](https://github.com/adobe-fonts/source-han-sans) | 中文字形 | SIL Open Font License 1.1；[保留的许可证](../assets/fonts/SourceHanSansSC-LICENSE.txt) |
| [lv_font_conv](https://github.com/lvgl/lv_font_conv) | 可复现字体转换 | 1.5.3；MIT |

## Android 与开发工具

| 项目 | 贡献 | 版本／许可证 |
| --- | --- | --- |
| [AndroidX](https://developer.android.com/jetpack/androidx) | Android 集成与设备测试 | Core 1.15.0；Apache-2.0 |
| [ZXing](https://github.com/zxing/zxing) | 二维码编码与解码 | 3.5.3；Apache-2.0 |
| [Gson](https://github.com/google/gson) | 结构化数据解析 | 2.11.0；Apache-2.0 |
| [bitcoinj](https://github.com/bitcoinj/bitcoinj) | 通过助记词导入钱包 | 0.16.3；Apache-2.0 |
| [Bouncy Castle](https://www.bouncycastle.org/) | 密码学基础实现 | 1.83；Bouncy Castle 许可证（MIT 风格） |
| [Gradle](https://github.com/gradle/gradle) | Android 构建工具与 wrapper | 8.14.3；Apache-2.0 |
| [JUnit](https://github.com/junit-team/junit4) | 单元测试 | 4.13.2；EPL-1.0 |
| [actionlint](https://github.com/rhysd/actionlint) | 工作流验证 | 1.7.12；MIT |

本页说明主要直接依赖，不替代完整许可证或传递依赖声明。分发源码和二进制时应保留适用声明。托帕钱包为独立衍生项目；致谢不代表获得上述项目的背书或官方支持。

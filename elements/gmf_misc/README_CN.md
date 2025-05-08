# ESP-GMF-Misc

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_misc/badge.svg)](https://components.espressif.com/components/espressif/gmf_misc)

- [English](./README.md)

ESP GMF Miscellaneous 汇集了 GMF 各种各样功能单一的元素，目前支持的元素列表如下：
|  名称  | 标签 | 功能说明  | 输入端口  | 输出端口  |输入阻塞时间 | 输出阻塞时间 |
|:----:|:----:|:----:|:----:|:----:|:----:|:----:|
| esp_gmf_copier | copier |  将输入数据复制到多个端口输出  | 单个 | 多个 | 最大延迟 | 可用户配置，默认是最大延迟 |

## 示例
ESP GMF Miscellaneous 常与其他 GMF 元素组合成管道使用，示例代码请参考 [test_app](../test_apps/main/elements/gmf_audio_play_el_test.c)。

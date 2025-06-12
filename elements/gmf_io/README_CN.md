# ESP-GMF-IO

- [English](./README.md)

ESP GMF IO 是 GMF 输入和输出流的集合。IO 流有读写两个数据流方向，输入类型为可读，输出类型为可写。IO 流对数据传输类型进行了分类，分别是字节访问和块访问类型，一般来说，字节访问对源数据进行拷贝后传递，块访问仅传递源数据地址无拷贝开销。另外，IO 流还提供了线程的配置可选项，使一些 IO Stream 可以建立自己独立的线程。目前已支持的 IO 流参见下表：

| 名称 | TAG | 数据流方向   | 作为线程 | 数据类型| 依赖的组件  | 备注 |
| :----: | :----: | :----: | :----: | :----: | :----: |:----: |
|  File | io_file | RW  |  NO |  Byte  |NA  | NA |
|  HTTP | io_http |  RW | YES | Block | NA  | Not support HTTP Live Stream |
|  Codec Dev IO | io_codec_dev |  RW | NO | Byte | [ESP codec dev](https://components.espressif.com/components/espressif/esp_codec_dev)  | NA |
|  Embed Flash | io_embed_flash |  R | NO | Byte | NA  | NA |
|  I2S PDM | io_i2s_pdm |  RW | NO | Byte | NA  | NA |

## 示例
ESP GMF IO 一般与 GMF 元素组成管道进行使用，也可以独立使用。在管道中，IO stream 与 GMF 元素的端口连接，独立使用时调用 IO stream 使用 ESP_GMF_IO 接口。 示例代码请参考 [test_app](../test_apps/main/elements/gmf_audio_play_el_test.c)。

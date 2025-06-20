# GMF 应用工具包

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_app_utils/badge.svg)](https://components.espressif.com/components/espressif/gmf_app_utils)

- [English Version](./README.md)

GMF 应用工具包（gmf_app_utils）是一个为方便 ESP GMF 开发应用程序而提供的常用便捷 API，它包含常见外设设置的配置，比如 Wi-Fi、SD card、AudioCodec 初始化，还包括一些系统管理功能，如性能监控接口和串口终端命令行接口（CLI）。

同时 gmf_app_utils 提供了 menuconfig 进行参数配置:

- 配置网络参数(如：SSID 和 Password)

  选择路径是：`GMF APP Configuration` -> `Example Connection Configuration` -> `WiFi SSID` 和 `WiFi Password`

- 选择开发板

  选择路径是：`GMF APP Configuration` -> `Target Board`

- 调整 Unit test 任务优先级

  选择路径是：`GMF APP Configuration` -> `Unit Test`

## 功能特性

### 外设管理（`esp_gmf_app_setup_peripheral.h`）
- 音频编解码器管理
  - 可配置的 I2S 模式（标准、TDM、PDM）
  - 独立的播放和录音配置
- I2C 接口管理
- 存储管理
  - SD 卡初始化和挂载
- 连接管理
  - Wi-Fi 初始化连接
- 通过 Menuconfig 选择支持的开发板
  - 其他开发板支持请参阅 [其他开发板支持](#其他开发板支持) 小节

### 系统工具（`esp_gmf_app_sys.h`）
提供系统资源监控功能的启动/停止，便于用户运行时性能跟踪以及资源使用情况监控，使用时需要在 menuconfig 中开启 `CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID` 和 `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`。

### 命令行接口（`esp_gmf_app_cli.h`）
提供交互式 CLI 支持，用户可自定义命令提示符、注册自定义命令。同时也默认注册了一些常用系统命令便于用户直接使用，如下：

- 默认加载的系统命令
  - `restart`：软件重启
  - `free`：查看当前空闲内存大小
    - 显示内部内存和 PSRAM 的空闲大小
    - 显示历史最小空闲内存记录
  - `tasks`：显示任务运行信息和 CPU 使用情况
  - `log`：设置模块日志级别
    - 用法：log <标签> <级别 0-5>
    - 级别说明：关闭（0）、错误（1）、警告（2）、信息（3）、调试（4）、详细（5）
  - `io`：GPIO 引脚控制
    - 用法：io <引脚编号> <电平 0-1>
    - 设置指定 GPIO 引脚的输出电平

### 单元测试工具（`esp_gmf_app_unit_test.h`）
提供 GMF 应用单元测试常用函数

- 主要功能
  - 多级内存泄漏检测（警告/关键阈值）
  - 组件特定泄漏分类（通用/LWIP/NVS）
  - 灵活的泄漏检查模式（默认/自定义/禁用）
  - 堆损坏检测和报告
  - TCP/IP 栈初始化
  - 单元测试的 Task 创建函数

- 使用方式
  - 在每个测试前后自动调用 `setUp()` 和 `tearDown()`
  - 使用`esp_gmf_app_test_case_uses_tcpip()`进行网络测试项资源预分配
  - 支持通过测试项描述如 `[leaks]` 或 `[leaks=1024]` 控制泄漏检查阈值
  - main 函数调用 `esp_gmf_app_test_main()` 创建单元测试线程

### 其他开发板支持

外设管理目前使用 `codec_board` 作为参考板卡实现，便于快速验证。若要在自定义开发板上使用支持的外设，请按照以下步骤操作：

1. **获取 `codec_board`** 并将其放入项目的 `components` 文件夹
   
   可以使用以下任一方法：
   
   1.1 首先构建代码以触发自动下载，然后复制到项目文件夹：
   ```bash
   idf.py build
   mkdir -p components/codec_board
   cp -rf managed_components/tempotian__codec_board components/codec_board
   ```
   
   1.2 从组件注册表下载 [codec_board](https://components.espressif.com/components/tempotian/codec_board/) 并手动复制到项目文件夹

2. **在 `components/codec_board/board_cfg.txt` 中添加新的开发板配置节**，例如：
   ```
   Board: MY_BOARD
   i2c: {sda: 1, scl: 2}
   i2s: {mclk: 42, bclk: 40, ws: 41, dout: 39}
   out: {codec: ES8311, pa: 38, use_mclk: 0, pa_gain: 6}
   ```

3. **在调用任何外设 API 之前，在应用代码中设置板卡类型**：
   ```c
   #include "codec_board.h"
   void app_main(void)
   {
       set_codec_board_type("MY_BOARD");
   }
   ```

4. **重新构建代码使修改生效**：
   ```bash
   idf.py fullclean
   idf.py -p /dev/XXXXX flash monitor
   ```

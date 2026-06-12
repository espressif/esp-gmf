资源
========

:link_to_translation:`en:[English]`

代码仓库与组件
-------------------

- `ESP-GMF GitHub 仓库 <https://github.com/espressif/esp-gmf>`_\ ：ESP-GMF 主仓库，包含 GMF-Core、处理单元（element）、应用组件（package）与示例工程
- `ESP Board Manager GitHub 仓库 <https://github.com/espressif/esp-board-manager>`_\ ：乐鑫开发板管理组件，可配合 ``esp_board_manager`` 完成板级设备初始化与复用
- `ESP-IDF GitHub 仓库 <https://github.com/espressif/esp-idf>`_\ ：ESP-GMF 基于 ESP-IDF 构建，需要先准备 ESP-IDF 开发环境
- `ESP-ADF GitHub 仓库 <https://github.com/espressif/esp-adf>`_\ ：乐鑫音频开发框架；面向多媒体应用的功能性仓库，将持续补充更多基于 ESP-GMF 的示例工程
- `Espressif Component Registry <https://components.espressif.com>`_\ ：乐鑫组件仓库，可按名称或关键字搜索 GMF 各组件与依赖

文档与示例
-------------------

- 各组件 README：仓库内每个组件目录下有独立的 ``README_CN.md`` / ``README.md``\ ，介绍组件定位、特性与最简用法
- 测试用例：每个组件的 ``test_apps/`` 与 ``examples/`` 目录给出可编译、可烧录的最小示例
- 文档中心：本指南是 ESP-GMF 的文档中心，覆盖架构、组件分类、API 参考与最佳实践
- `ESP-IDF 编程指南 <https://docs.espressif.com/projects/esp-idf/zh_CN/latest/>`_\ ：理解 GMF 依赖的底层框架，包括组件管理器、任务模型等

社区与支持
-------------------

- `ESP-GMF GitHub Issues <https://github.com/espressif/esp-gmf/issues>`_\ ：报告缺陷或提出新功能需求；提交前先搜索是否已有相同问题
- `esp32.com 论坛 <https://esp32.com>`_\ ：乐鑫官方社区论坛，覆盖 ESP32 系列芯片的通用开发问题
- 如需贡献代码或文档，请参考 :doc:`contributions-guide`

硬件参考
-------------------

ESP-GMF 支持的乐鑫芯片与开发板可在以下渠道查找：

- `乐鑫开发板与模组 <https://www.espressif.com/zh-hans/products/devkits>`_\ ：官方开发板，附 GMF 部分组件的板级适配
- `乐鑫芯片产品页 <https://www.espressif.com/zh-hans/products/socs>`_\ ：ESP32 系列芯片技术参数与文档
- `乐鑫官方开发板组件 <https://github.com/espressif/esp-board-manager/tree/main/esp_boards>`_\ ：乐鑫官方开发板 ESP Board Manager 配置文件
- `M5Stack 开发板组件 <https://github.com/espressif/esp-board-manager/tree/main/m5stack_boards>`_\ ：M5Stack 系列开发板 ESP Board Manager 配置文件
- `购买样品 <https://www.espressif.com/en/contact-us/get-sample>`_\ ：申请样品与查找分销渠道

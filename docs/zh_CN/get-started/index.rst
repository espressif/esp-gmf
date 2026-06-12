快速入门
==============

:link_to_translation:`en:[English]`

本文档帮助开发者基于乐鑫 ESP32 系列芯片搭建多媒体应用的开发环境，并通过一个完整的示例工程，演示如何使用 ESP-GMF（Espressif General Multimedia Framework）。

读完本文档后，您将能够：

- 安装并配置一个受支持的 ESP-IDF 版本
- 获取一个 ESP-GMF 示例工程
- 编译、烧录工程并通过串口监视示例工程的运行

关于 ESP-GMF
--------------------------------------

`ESP-GMF <https://github.com/espressif/esp-gmf>`__ 是一组运行在 ESP-IDF 之上的多媒体组件，扩展 ESP-IDF 在音频、视频、图像及通用流式数据处理方面的能力。其核心模块（GMF-Core）与各类处理单元（element）、应用组件（package）已发布到 `IDF 组件管理器 <https://components.espressif.com/>`__，可由组件管理器自动获取与版本管理。

开发者通常只需在工程的 ``idf_component.yml`` 中声明依赖，构建时由组件管理器自动获取，无需设置额外的环境变量。本文提供克隆仓库与组件管理器两种方式获取示例工程。

.. note::

   ESP-GMF 当前支持的 ESP-IDF 版本为 ``>= v5.4.3`` （\ ``release/v5.4`` 分支）、``>= v5.5.2`` （\ ``release/v5.5`` 分支）或 ``>= v6.0``。

.. _get-started-setup-idf:

Step 1. 安装 ESP-IDF
--------------------------------------

请按 `ESP-IDF 编程指南 <https://docs.espressif.com/projects/esp-idf/zh_CN/latest/index.html>`__ 中“快速入门”章节，根据您所在的操作系统（Windows、Linux 或 macOS）完成 ESP-IDF 工具链与依赖的安装。

安装完成并激活环境后，确认终端中可正常运行：

.. code-block:: bash

   idf.py --version

并输出一个受支持的版本号（参见 `关于 ESP-GMF`_ 中的版本说明）。

.. note::

   如已安装但版本不在支持范围内，可参考 `ESP-IDF 版本管理 <https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/versions.html>`__ 切换分支。

.. _get-started-get-gmf:

Step 2. 获取 ESP-GMF 示例工程
--------------------------------------

GMF 示例工程存放在仓库的 ``gmf_examples`` 目录下，本文以 ``pipeline_play_embed_music`` 为例演示完整的编译与运行流程，该示例从 Flash 中读取内置的 MP3 音频，经解码、音效处理后通过编解码器输出至扬声器。

提供以下两种方式获取示例工程，二选一即可。

- 方式 A（克隆仓库）：下载 ESP-GMF 完整源码及全部示例，适合深入学习框架、对比测试各示例，或向 ESP-GMF 贡献代码。
- 方式 B（组件管理器）：仅下载一个示例工程，所需组件依赖在编译阶段由组件管理器自动获取，与实际产品项目的开发方式一致，适合快速验证或集成评估。

**方式 A（克隆仓库）：**

在终端中执行以下命令获取 ESP-GMF 完整源码仓库：

.. code-block:: bash

   git clone https://github.com/espressif/esp-gmf.git

.. note::

   下文出现的 ``$GMF_PATH`` 为文档中使用的占位符，代表克隆得到的 ``esp-gmf`` 仓库根目录，实际操作时请按本地克隆位置替换。

**方式 B（通过 IDF 组件管理器直接下载示例）：**

在目标目录下执行以下命令，由 IDF 组件管理器直接获取示例工程：

.. code-block:: bash

   idf.py create-project-from-example "espressif/gmf_examples=0.8.0:pipeline_play_embed_music"

执行完成后，当前目录下会生成 ``pipeline_play_embed_music`` 工程文件夹，无需克隆整个 ESP-GMF 仓库。

.. _get-started-start-project:

Step 3. 启动示例工程
--------------------------------------

进入示例工程目录：

**方式 A（克隆仓库）：Linux / macOS**

.. code-block:: bash

   cd $GMF_PATH/gmf_examples/basic_examples/pipeline_play_embed_music

**方式 A（克隆仓库）：Windows**

.. code-block:: batch

   cd %GMF_PATH%\gmf_examples\basic_examples\pipeline_play_embed_music

**方式 B（组件管理器）：**

.. code-block:: bash

   cd pipeline_play_embed_music

.. note::

   ESP-IDF 构建系统不支持路径中包含空格，请确认 ESP-IDF 与工程的完整路径中均不含空格。

.. _get-started-connect:

Step 4. 连接开发板
--------------------------------------

通过 USB 数据线将音频开发板（\ ``pipeline_play_embed_music`` 默认支持 ESP32-S3-Korvo V3 及其他 ESP 音频开发板）连接到 PC，并按 `ESP-IDF 文档：建立串口连接 <https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/establish-serial-connection.html>`__ 确认开发板对应的串口号：

- Linux 下通常为 ``/dev/ttyUSB0`` 或 ``/dev/ttyACM0``
- macOS 下通常为 ``/dev/cu.usbserial-*`` 或 ``/dev/cu.SLAB_USBtoUART``
- Windows 下通常为 ``COM3``\ 、\ ``COM4`` 等

.. note::

   请记下当前开发板对应的串口号，后续 :ref:`get-started-flash` 与 :ref:`get-started-monitor` 步骤需要使用。

.. _get-started-board-manager:

Step 5. 配置硬件
--------------------------------------

ESP-GMF 示例工程通过 `ESP Board Manager <https://github.com/espressif/esp-board-manager>`__ 统一管理开发板的外设描述与板级初始化代码。推荐安装辅助工具 ``esp-bmgr-assist`` 作为默认工具。

在已激活的 ESP-IDF Python 环境下安装（同一环境只需安装一次）：

.. code-block:: bash

   pip install esp-bmgr-assist

如需升级至最新版本：

.. code-block:: bash

   pip install --upgrade esp-bmgr-assist

查看支持的开发板：

.. code-block:: bash

   idf.py bmgr -l

输出示例：

.. code-block:: none

    [1] esp32_c3_lyra
    [2] esp32_lyrat_mini_1_1
    [3] esp32_p4_function_ev_board
    [4] esp32_s31_function_coreboard_1
    [5] esp32_s31_korvo_1
    [6] esp32_s3_box_3
    [7] esp32_s3_box_lite
    [8] esp32_s3_korvo_2_3
    [9] esp32_s3_lcd_ev_board
    [10] esp_vocat_1_0
    [11] esp_vocat_1_2

选择开发板：

.. code-block:: bash

   idf.py bmgr -b <board_index|board_name>

例如选择 ``esp32_s3_korvo_2_3``\ ：

.. code-block:: bash

   idf.py bmgr -b 8
   # 或
   idf.py bmgr -b esp32_s3_korvo_2_3

首次执行 ``idf.py bmgr`` 时，组件会根据本工程 ``main/idf_component.yml`` 中声明的 ``espressif/esp_board_manager`` 依赖自动下载。

.. note::

   - 如需切换为其他 ``esp_board_manager`` 支持的开发板，请按相同步骤执行并替换板型名称/索引。
   - 如需使用未列出的自定义开发板，请参考 `自定义开发板指南 <https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/docs/how_to_customize_board_cn.md>`__。
   - ``esp_board_manager`` 更多信息请参考 `ESP Board Manager 入门指南 <https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README_CN.md>`__。

.. _get-started-configure:

Step 6. 工程配置
--------------------------------------

打开工程配置菜单：

.. code-block:: bash

   idf.py menuconfig

对于 ``pipeline_play_embed_music`` 示例，默认配置即可直接编译运行。如需调整其他工程相关选项，请参考示例工程目录下的 ``README``\ 。

修改完成后按 ``S`` 保存，再按 ``Q`` 退出菜单。

.. _get-started-build:

Step 7. 编译工程
--------------------------------------

执行以下命令开始编译：

.. code-block:: bash

   idf.py build

该命令会按依赖关系编译 ESP-IDF、ESP-GMF 涉及到的所有组件，并生成 bootloader、分区表与应用 bin 文件。首次编译用时较长，后续增量编译会显著加快。

编译成功后，终端会输出类似以下日志，并提示对应的 flash 命令：

.. code-block:: none

   Project build complete. To flash, run:
    idf.py flash
   or
    idf.py -p PORT flash

如出现编译错误，请按错误提示检查 ESP-IDF 版本、:ref:`get-started-board-manager` 中开发板配置是否成功完成、依赖组件是否正确获取等。

.. _get-started-flash:

Step 8. 烧录固件
--------------------------------------

将 ``PORT`` 替换为 :ref:`get-started-connect` 中记录的串口号，运行以下命令完成烧录并打开串口监视：

.. code-block:: bash

   idf.py -p PORT flash monitor

.. note::

   - ``idf.py flash`` 会在烧录前自动重新编译，因此不必单独执行 ``idf.py build``\ 。
   - 默认烧录波特率为 ``460800``\ ，可通过 ``-b BAUD`` 参数调整。
   - 若开发板没有自动复位电路，烧录前请按住 **Boot** 键、短按一次 **Reset** 键后再松开 **Boot** 键，使芯片进入下载模式。
   - 如果开发板使用 USB Serial JTAG，且找不到串口，可以尝试按照上述方式手动进入下载模式后查看串口。

.. _get-started-monitor:

Step 9. 监视输出
--------------------------------------

烧录完成后，开发板会自动复位并运行示例程序。串口监视器中将输出类似以下日志（仅截取关键步骤）：

.. code-block:: none

   I (912)  main_task: Calling app_main()
   I (922)  PLAY_EMBED_MUSIC: [ 1 ] Mount peripheral
   I (1067) PLAY_EMBED_MUSIC: [ 2 ] Register all the elements and set audio information to play codec device
   I (1067) PLAY_EMBED_MUSIC: [ 3 ] Create audio pipeline
   I (1110) PLAY_EMBED_MUSIC: [ 4 ] Start audio_pipeline
   I (1223) PLAY_EMBED_MUSIC: [ 5 ] Wait stop event to the pipeline and stop all the pipeline
   I (23448) PLAY_EMBED_MUSIC: [ 6 ] Destroy all the resources

如果一切正常，连接到开发板的扬声器或耳机会播放一段约 20 秒的内置 MP3 测试音频，播放结束后示例程序释放资源并退出。

使用快捷键 ``Ctrl+]``，可退出串口监视器

后续阅读
--------------------------------------

完成本示例后，您已经掌握 ESP-GMF 工程的基础工作流。建议按以下方向继续探索：

- 参考 :doc:`../gmf-best-practices/index`，结合 ``gmf_examples`` 系统学习 ESP-GMF 在产品级方案中的开发流程与典型应用方式。
- 阅读 :doc:`../gmf-framework/index`，了解 GMF-Core、Elements、Packages 的整体架构与工作机制。
- 查阅 :doc:`../resources` 中的资源链接、:doc:`../glossary` 中的术语解释，以及 :doc:`../contributions-guide` 了解贡献流程。

相关文档
--------------------------------------

- `ESP-IDF 编程指南 <https://docs.espressif.com/projects/esp-idf/zh_CN/latest/index.html>`__
- `ESP 组件管理器 <https://components.espressif.com/>`__
- `ESP 组件管理器使用文档 <https://docs.espressif.com/projects/idf-component-manager/en/latest/>`__
- `ESP Board Manager 入门指南 <https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README_CN.md>`__
- `ESP Board Manager 常见使用问题 <https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README_CN.md#%E6%95%85%E9%9A%9C%E6%8E%92%E9%99%A4>`__
- `ESP-GMF GitHub 仓库 <https://github.com/espressif/esp-gmf>`__
- `ESP-GMF 示例集合 <https://github.com/espressif/esp-gmf/tree/main/gmf_examples>`__

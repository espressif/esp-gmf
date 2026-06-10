词汇表
==========

:link_to_translation:`en:[English]`

ESP-GMF 代码与文档中使用的术语、领域名词与缩写，按字母顺序排列。

A2DP
    Advanced Audio Distribution Profile，蓝牙高级音频分发规约

    经典蓝牙音频传输协议，用于双向音频流。

Acquire-Release
    数据端口（port）的数据访问协议

    读侧 ``acquire_in`` 取数据载体（payload），用完 ``release_in`` 归还；写侧 ``acquire_out`` 取空数据载体，写完 ``release_out`` 提交下游。

AEC
    Acoustic Echo Cancellation，声学回声消除

    从麦克风信号中减去扬声器输出的回采分量。

AFE
    Audio Front-End，音频前端

    一组对原始麦克风数据做处理的算法，含 AEC、NS、VAD、WakeNet 等模块。

ALC
    Automatic Level Control，自动电平控制

    按声道独立调整增益。

AVRCP
    Audio/Video Remote Control Profile，蓝牙音视频远程控制规约

    用于播放控制、元数据、通知。

BMGR / Board Manager
    Board Manager，板级管理器

    基于 YAML 的板级硬件配置系统（\ ``esp_board_manager``\ ），代码生成器把 YAML 翻译为 C 初始化代码。

Cache
    字节缓存

    ``esp_gmf_cache`` 提供的字节级滑动缓冲区，用于把多块数据载体中的字节连续缓存起来，再按任意长度读取。常用于解码器解析协议头或帧头。

Capability
    能力描述

    处理单元对外声明自身能力的机制。每个能力描述节点以 EIGHTCC 标识能力类别，并可附带性能信息和属性范围，供注册池、处理链或上层应用查询与选择处理单元。

Data Bus
    数据总线

    数据端口下层的数据队列，提供 ringbuffer / fifo / block / pbuf 四种实现，负责跨线程同步与缓冲。

DRC
    Dynamic Range Control，动态范围控制

    按拐点曲线压缩信号。

EIGHTCC
    Eight Character Code，八字符代码

    8 字符算法类别标识，定义在 ``esp_gmf_caps_def.h``\ ，描述处理单元的算法种类（如 ``AUDDEC`` / ``AUDEQ``\ ）。

Element
    处理单元

    ESP-GMF 的处理单元，对应一种具体算法。每个处理单元实现 ``open`` / ``process`` / ``close`` 三个固定阶段功能函数。

EQ
    Equalizer，均衡器

    按段调整频响。

FourCC
    Four Character Code，四字符代码

    用 4 个 ASCII 字符表示一种编码或封装格式的 32 位标识，定义在 ``esp_fourcc.h``\ 。

GMF
    General Multimedia Framework，通用多媒体框架

    乐鑫为 IoT 多媒体应用打造的轻量级通用软件框架。

GOP
    Group of Pictures，图像组

    视频编码中两个 I 帧之间的帧数。

HFP
    Hands-Free Profile，蓝牙免提规约

    经典蓝牙免提通话协议。

IO
    外部接口

    处理链首尾连接外部数据源或数据出口的特殊处理单元，基于 ``esp_gmf_io_t`` 实现文件、HTTP、flash、codec、I2S 等读写能力。

is_done
    流结束标志（\ ``esp_gmf_payload_t`` 字段）

    源头外部接口（IO）读到末尾时置 1，下游依次处理后逐级触发 close。

Job
    工作单元

    执行线程（task）调度的具体工作单元，按处理单元的 open / process / close 注册到执行线程的 job 链表，可标记一次性或无限次执行。

Job Stack
    工作单元栈

    执行线程内部保存被 ``TRUNCATE`` 暂存 job 的结构。下游消费数据后，执行线程可回到暂存 job 继续执行。

MBC
    Multi-Band Compressor，多频段压缩器

    每个频段独立压缩。

Method
    运行时方法

    处理单元在 ``open`` / ``process`` / ``close`` 之外暴露的运行时控制动作，由 ``esp_gmf_method_t`` 描述方法名、执行函数与参数说明。应用可按方法名调用，用于调整音量、目标采样率、目标分辨率等参数。

NS
    Noise Suppression，噪声抑制

    AFE 中的子模块。

OAL
    Operating System Abstraction Layer，操作系统抽象层

    封装操作系统与 ESP-IDF 相关的内存、互斥锁、线程和系统统计接口。

Payload
    数据载体

    跨处理单元流动的数据载体（\ ``esp_gmf_payload_t``\ ），含数据缓冲区、有效长度、流结束标志、时间戳等字段。

Pipeline
    处理链

    处理链的编排者，把若干处理单元按顺序连接起来，对外暴露 ``run`` / ``stop`` / ``pause`` / ``resume`` / ``reset`` / ``seek`` 控制接口。

Pool
    注册池

    处理单元与外部接口的模板库（\ ``esp_gmf_pool_t``\ ），按名称实例化处理链（pipeline）。

Port
    数据端口

    处理单元对外暴露的数据读写接口，采用 acquire-release 协议访问数据载体，下层由数据总线（data bus）提供真实数据队列。

PPA
    Pixel Processing Accelerator，像素处理加速器

    ESP32-P4/ESP32-S31 的硬件加速单元，支持颜色转换、缩放、旋转、裁剪。

PTS
    Presentation Time Stamp，显示时间戳

    用于音视频同步。

QP
    Quantization Parameter，量化参数

    视频编码中的量化参数，影响压缩率与画质。

SE
    Speech Enhancement，语音增强

    AFE 中的子模块。

Task
    执行线程

    被框架封装的 FreeRTOS 线程，运行在 ``esp_gmf_task.c``\ ，逐个执行 job。

URL Score
    URL 评分机制

    外部接口选型机制。每个外部接口实现 ``get_score`` 回调评估 URL 匹配度，注册池据此选择评分最高的外部接口。

VAD
    Voice Activity Detection，人声活动检测

    判断当前帧是否包含人声。

VCMD
    Voice Command Detection，语音命令词检测

    基于 ``MultiNet`` 模型识别预设命令短语。

WWE / WakeNet
    Wake Word Engine，唤醒词引擎

    检测特定唤醒词触发设备激活。

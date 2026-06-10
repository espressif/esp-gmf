ESP Bluetooth Audio
================================

:link_to_translation:`en:[English]`

ESP Bluetooth Audio（\ ``esp_bt_audio``\ ）统一管理经典蓝牙（Classic Bluetooth）与 LE Audio 的音频能力，把底层蓝牙协议栈与音频流程整合为统一的初始化、数据流与事件通知接口。组件按配置的角色（role）自动完成对应协议的初始化与管理，将底层回调与状态收敛为单一事件接口，应用无需关心各 profile / host 差异。

主要特性
-------------------------------------

- Classic 蓝牙协议覆盖：

  - A2DP Sink / Source：在手机/PC 与本地音箱、耳机之间收发音频
  - HFP Hands-Free (HF) / Audio Gateway (AG)：通话等语音场景
  - AVRCP Controller / Target：播放控制、元数据、通知
  - PBAP Client Equipment：获取手机通讯录与通话记录
- LE Audio 协议与角色覆盖：

  - BAP Unicast Server：暴露 sink/source ASE，用于 LE 单播媒体或通话音频
  - BAP Broadcast Source / Sink：发送或接收 LC3 广播音频流
  - Scan Delegator：接收 Broadcast Assistant 的广播发现与同步请求
  - TMAP 支持：配置 CT、UMR、BMR、BMS 等电话与媒体角色组合
  - VCP / MCP / MICP / CCP / CSIP：支持音量、媒体控制、麦克风、通话控制与协同组能力
- 统一事件回调：

  - 连接 / 发现状态、设备发现
  - stream 分配 / 启动 / 停止 / 释放
  - 媒体控制命令、播放状态、元数据
  - 绝对 / 相对音量事件
  - 通话状态与电话状态
  - 通讯录、通话记录条目
- Stream 抽象：统一的数据收发接口，可查询 codec 信息、stream 方向与上下文
- 两种数据接入方式：直接读写蓝牙音频数据，或接入 ESP-GMF pipeline

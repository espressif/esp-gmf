ESP Bluetooth Audio
===================

:link_to_translation:`zh_CN:[中文]`

ESP Bluetooth Audio (``esp_bt_audio``) provides unified management of Classic Bluetooth and LE Audio capabilities, integrating the underlying Bluetooth protocol stack and audio workflows into unified initialization, data stream, and event notification interfaces. The component automatically initializes and manages the corresponding protocol based on the configured role, converging lower-level callbacks and states into a single event interface so applications need not handle per-profile or per-host differences.

Key Features
------------

- Classic Bluetooth protocol coverage:

  - A2DP Sink / Source: send and receive audio between phone/PC and local speakers or headphones
  - HFP Hands-Free (HF) / Audio Gateway (AG): voice calling and speech scenarios
  - AVRCP Controller / Target: playback control, metadata, notifications
  - PBAP Client Equipment: retrieve phone contacts and call history
- LE Audio protocol and role coverage:

  - BAP Unicast Server: exposes sink/source ASEs for LE unicast media or call audio
  - BAP Broadcast Source / Sink: send or receive LC3 broadcast audio streams
  - Scan Delegator: receives broadcast discovery and synchronization requests from a Broadcast Assistant
  - TMAP support: configures CT, UMR, BMR, BMS, and other telephony and media role combinations
  - VCP / MCP / MICP / CCP / CSIP: volume control, media control, microphone control, call control, and coordinated set capabilities
- Unified event callbacks:

  - Connection / discovery state, device discovery
  - Stream allocation / start / stop / release
  - Media control commands, playback state, metadata
  - Absolute / relative volume events
  - Call state and phone state
  - Phonebook and call log entries
- Stream abstraction: unified data send/receive interface; query codec information, stream direction, and context
- Two data access modes: direct read/write of Bluetooth audio data, or integration with an ESP-GMF pipeline

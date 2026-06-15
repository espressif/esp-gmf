# Changelog

## v1.0.0

### Features

- Pinned the `esp_board_manager` dependency in the `bt_audio` example to the standalone component at [espressif/esp-board-manager](https://github.com/espressif/esp-board-manager).
- Reworked the example README "Build Preparation" section to use [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) and `idf.py bmgr`; removed the manual `IDF_EXTRA_ACTIONS_PATH` instructions.
- Added clock synchronization check
- Added support for PAST in Auracast
- Added UI support to the `bt_audio` example for Classic Bluetooth and LE Audio

## 0.8.2

### Features

- Support BLE Audio

## 0.8.1

### Features

- Support retrieving album artwork during AVRCP from a paired device
- Support accessing remote phonebook and call history via PBAP

## v0.8.0

### Features

- Initial version of Bluetooth AudioComponents

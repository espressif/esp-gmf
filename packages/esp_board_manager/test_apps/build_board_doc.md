# Build Boards Script

Automates building test apps for all ESP Board Manager boards.

## Usage

```bash
# Build main boards only (default, fast)
# Only scans `esp_board_manager/boards/`
python build_board.py

# Build specific board
python build_board.py -b echoear_core_board_v1_0

# Build all boards (comprehensive)
# Scans `boards/` + `components/*/boards/`
python build_board.py -a

# Use custom boards directory
python build_board.py -p /path/to/boards

# Build all boards including custom boards
python build_board.py -a -p /path/to/boards

# Other options
python build_board.py --skip-build      # Only generate configs
python build_board.py --stop-on-error   # Stop on first error
python build_board.py --save-logs       # Save logs for successful builds too (failed builds always save logs)
```

## How It Works

For each board:
1. Generate board configuration: `idf.py gen-bmgr-config -b <board>`
2. Clean build directory: `idf.py fullclean` (automatic, ensures clean build)
3. Read chip type from `board_info.yaml`
4. Set target chip: `idf.py set-target <chip>`
5. Build project: `idf.py build`
6. Save logs if any step fails

## Prerequisites

- ESP-IDF environment properly set up
- `idf.py` available in PATH
- Python 3.6+
- PyYAML: `pip install pyyaml`

## Output Example

```
ESP Board Manager - Build All Boards
============================================================
Project directory: /path/to/test_apps
Board manager directory: /path/to/esp_board_manager

Scanning for available boards...
Found 9 board(s):
  • echoear_core_board_v1_0
  • esp_box_3
  • esp32_p4_function_ev
  ...

Will process 9 board(s)

============================================================
Building board: echoear_core_board_v1_0
============================================================
  Chip type: esp32s3
  → Generate config for echoear_core_board_v1_0...
  ✓ Generate config completed
  → Cleaning build directory...
  ✓ Build directory cleaned
  → Set target to esp32s3...
  ✓ Set target completed
  → Build echoear_core_board_v1_0...
  ✓ Build completed
  ✓ Successfully processed echoear_core_board_v1_0

...

============================================================
Build Summary
============================================================

Total boards: 9
Successful: 8
Failed: 1
Time elapsed: 245.32 seconds

✓ Successful boards:
  • echoear_core_board_v1_0
  • esp_box_3
  ...

✗ Failed boards:
  • dual_eyes_board_v1_0
    Error (showing last 10 lines):
      ninja: error: ...

📝 Full logs saved for failed boards (in logs/ directory):
  • dual_eyes_board_v1_0: build_dual_eyes_board_v1_0.log
```

## Log Files

Failed steps automatically save logs in the `logs/` directory:
- `logs/config_<board>.log` - Configuration generation errors (auto-saved on failure)
- `logs/set_target_<board>.log` - Target setting errors (auto-saved on failure)
- `logs/build_<board>.log` - Build compilation errors (auto-saved on failure)

The logs directory is automatically created in `test_apps/logs/`.

## Troubleshooting

**No boards found**
- Run from `test_apps` directory
- Check `gen_bmgr_config_codes.py` exists in parent directory
- Try `--all-boards` flag

**Build fails**
- Check saved log file: `build_<board>.log`
- Verify `board_info.yaml` has valid `chip` field
- Try building manually: `python build_board.py --board <name>`
- Build directory is automatically cleaned before each board build

**Import errors**
- Run from `test_apps` directory
- Install PyYAML: `pip install pyyaml`
- Check Python 3.6+ is installed

**Invalid build directory**
- Script automatically cleans build directory before each board build
- Manual cleanup: `rm -rf build/` or `idf.py fullclean`

#!/bin/bash
# Scenario-based integration checks for sdkconfig consistency guard.

set -u -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_APPS_DIR="$(dirname "$SCRIPT_DIR")"
BMGR_DIR="$(dirname "$TEST_APPS_DIR")"
LOG_FILE="$TEST_APPS_DIR/verify_bmgr_sdkconfig_consistency.log"
TARGET_BOARD="${1:-esp32_s3_korvo2_v3}"
TARGET_SYMBOL="CONFIG_ESP_BOARD_PERIPH_I2C_SUPPORT"

PASS_COUNT=0
FAIL_COUNT=0

exec > >(tee "$LOG_FILE") 2>&1

echo "Log: $LOG_FILE"
echo "Board: $TARGET_BOARD"

if [ -f "$HOME/esp/esp-idf/export.sh" ]; then
    # shellcheck disable=SC1090
    . "$HOME/esp/esp-idf/export.sh"
fi

export IDF_EXTRA_ACTIONS_PATH="$BMGR_DIR"
echo "IDF_EXTRA_ACTIONS_PATH=$IDF_EXTRA_ACTIONS_PATH"

cd "$TEST_APPS_DIR"

run_step() {
    echo ""
    echo "=== $1 ==="
}

record_pass() {
    PASS_COUNT=$((PASS_COUNT + 1))
    echo "[PASS] $1"
}

record_fail() {
    FAIL_COUNT=$((FAIL_COUNT + 1))
    echo "[FAIL] $1"
}

run_cmd() {
    local desc="$1"
    shift
    if "$@"; then
        record_pass "$desc"
        return 0
    fi
    record_fail "$desc"
    return 1
}

expected_state_from_defaults() {
    local defaults_file="$TEST_APPS_DIR/components/gen_bmgr_codes/board_manager.defaults"
    if [ -f "$defaults_file" ] && grep -q "^${TARGET_SYMBOL}=y$" "$defaults_file"; then
        echo "y"
    else
        echo "n"
    fi
}

set_symbol_state() {
    local state="$1" # y or n
    if [ ! -f sdkconfig ]; then
        return 1
    fi

    sed -i "/^${TARGET_SYMBOL}=y$/d" sdkconfig
    sed -i "/^# ${TARGET_SYMBOL} is not set$/d" sdkconfig

    if [ "$state" = "y" ]; then
        echo "${TARGET_SYMBOL}=y" >> sdkconfig
    else
        echo "# ${TARGET_SYMBOL} is not set" >> sdkconfig
    fi
}

assert_symbol_state() {
    local state="$1" # y or n
    if [ "$state" = "y" ]; then
        grep -q "^${TARGET_SYMBOL}=y$" sdkconfig
    else
        grep -q "^# ${TARGET_SYMBOL} is not set$" sdkconfig
    fi
}

summary() {
    echo ""
    echo "=== Summary ==="
    echo "PASS: $PASS_COUNT"
    echo "FAIL: $FAIL_COUNT"
    if [ "$FAIL_COUNT" -eq 0 ]; then
        echo "Result: PASS"
        return 0
    fi
    echo "Result: FAIL"
    return 1
}

run_step "A. Clean and baseline generation"
run_cmd "Clean generated config" idf.py gen-bmgr-config -x
run_cmd "Generate config for target board" idf.py gen-bmgr-config -b "$TARGET_BOARD" || { summary; exit 1; }
run_cmd "Reconfigure project" idf.py reconfigure || { summary; exit 1; }

run_step "B. Same board rerun should pass"
run_cmd "Rerun -b with same board" idf.py gen-bmgr-config -b "$TARGET_BOARD" || { summary; exit 1; }

EXPECTED_STATE="$(expected_state_from_defaults)"
if [ "$EXPECTED_STATE" = "y" ]; then
    CORRUPTED_STATE="n"
else
    CORRUPTED_STATE="y"
fi

echo "Expected ${TARGET_SYMBOL} state for ${TARGET_BOARD}: ${EXPECTED_STATE}"
echo "Corrupted state used for test: ${CORRUPTED_STATE}"

run_step "C. Corrupt sdkconfig symbol and verify auto-fix"
if set_symbol_state "$CORRUPTED_STATE"; then
    record_pass "Corrupt sdkconfig symbol"
else
    record_fail "Corrupt sdkconfig symbol"
    summary; exit 1
fi
run_cmd "Run -b after corruption (auto-fix expected)" idf.py gen-bmgr-config -b "$TARGET_BOARD" || { summary; exit 1; }
if assert_symbol_state "$EXPECTED_STATE"; then
    record_pass "Auto-fix restored expected symbol state"
else
    record_fail "Auto-fix did not restore expected symbol state"
    summary; exit 1
fi

run_step "D. Skip consistency check should keep corruption"
if set_symbol_state "$CORRUPTED_STATE"; then
    record_pass "Re-corrupt sdkconfig symbol"
else
    record_fail "Re-corrupt sdkconfig symbol"
    summary; exit 1
fi
run_cmd "Run -b with --skip-sdkconfig-check" idf.py gen-bmgr-config -b "$TARGET_BOARD" --skip-sdkconfig-check || { summary; exit 1; }
if assert_symbol_state "$CORRUPTED_STATE"; then
    record_pass "Skip check preserved corrupted symbol state"
else
    record_fail "Skip check unexpectedly changed symbol state"
    summary; exit 1
fi

run_step "E. Board switch path should delete sdkconfig before check"
run_cmd "List boards" idf.py gen-bmgr-config -l || { summary; exit 1; }
OTHER_BOARD="$(idf.py gen-bmgr-config -l | awk '/^\[[0-9]+\]/{print $2}' | grep -v "^${TARGET_BOARD}$" | head -n 1 || true)"
if [ -n "$OTHER_BOARD" ]; then
    run_cmd "Switch to another board (${OTHER_BOARD})" idf.py gen-bmgr-config -b "$OTHER_BOARD" || { summary; exit 1; }
    if [ -f sdkconfig.bmgr_board.old ]; then
        record_pass "sdkconfig backup exists after board switch"
    else
        record_fail "sdkconfig backup missing after board switch"
        summary; exit 1
    fi
else
    echo "No alternate board found; skipped board-switch scenario"
fi

run_step "F. Restore target board"
run_cmd "Restore target board" idf.py gen-bmgr-config -b "$TARGET_BOARD" || { summary; exit 1; }

summary
exit $?

# ESP Board Manager Test Suite

This directory contains comprehensive pytest-based test suite for the ESP Board Manager Configuration Generator.

## 📁 Directory Structure

```
test_scripts/
├── conftest.py                    # Pytest configuration and fixtures
├── pytest.ini                     # Pytest settings
├── test_board_selection.py        # Board selection tests (name, index, priority)
├── test_list_boards.py            # Board listing tests
├── test_generation_modes.py       # Generation mode tests (kconfig-only, etc.)
├── test_error_handling.py         # Error handling and edge cases
├── test_log_levels.py             # Log level functionality tests
├── run_pytest.sh                  # Test execution script
├── validate_tests.py              # Test validation utility
├── self_test.py                   # Self-test utility
├── diagnose_pytest.py             # Pytest diagnostics tool
└── README.md                      # This file
```

## 🎯 Test Suite

- **Type**: pytest-based tests
- **Tests**: 99 test cases
- **Requirements**: pytest (and optional plugins)
- **Usage**: `./run_pytest.sh`
- **Features**: Detailed reporting, coverage analysis, parallel execution

## 🚀 Quick Start

### Automatic Dependency Installation

The script automatically installs required packages if they are missing:
- **pytest**: Automatically installed if not present
- **pytest-html**: Automatically installed when using `--html` option
- **pytest-cov**: Automatically installed when using `--cov` option

You can also install them manually if preferred:
```bash
pip3 install pytest pytest-html pytest-cov
```

### Run All Tests

```bash
# Basic run
./run_pytest.sh

# Verbose output
./run_pytest.sh -v

# With HTML report (default path: test_reports/report.html)
./run_pytest.sh --html

# With HTML report (custom filename)
./run_pytest.sh --html=report.html

# With coverage report
./run_pytest.sh --cov

# Generate both HTML and coverage reports
./run_pytest.sh --html --cov
```

### Run Specific Test Cases

```bash
# Board selection tests
pytest test_board_selection.py -v

# List boards tests
pytest test_list_boards.py -v

# Generation modes
pytest test_generation_modes.py -v

# Error handling
pytest test_error_handling.py -v

# Log levels
pytest test_log_levels.py -v
```

## 📊 Test Coverage

The test suite covers:

- ✅ Board selection (by name and index)
- ✅ Positional argument support
- ✅ Parameter priority (-b overrides positional)
- ✅ Board listing with numbering
- ✅ Generation modes (kconfig-only, peripherals-only, devices-only)
- ✅ Error handling and boundary conditions
- ✅ Log level functionality
- ✅ Command-line argument validation
- ✅ Special characters and edge cases

## 📈 Test Results

Current Status: **🟢 All Tests Passing**

- Total Tests: 99 ✅
- Passed: 99
- Failed: 0
- Success Rate: 100% 🎊

## 🔧 Test Files

### test_board_selection.py
Tests board selection functionality:
- Board selection by name (positional and -b)
- Board selection by index (positional and -b)
- Parameter priority (-b overrides positional)
- Invalid board names and indices
- Special characters and edge cases

### test_list_boards.py
Tests board listing functionality:
- Basic listing with -l option
- Board numbering display
- Output format validation

### test_generation_modes.py
Tests different generation modes:
- --kconfig-only
- --peripherals-only
- --devices-only

### test_error_handling.py
Tests error handling:
- Invalid board names
- Invalid indices
- Missing configuration files
- Empty parameters

### test_log_levels.py
Tests logging functionality:
- DEBUG, INFO, WARNING, ERROR levels
- Log output validation

## 🛠️ Utilities

### run_pytest.sh
Main test execution script with preset options for convenience.

### validate_tests.py
Validates test suite integrity and coverage.

### self_test.py
Self-test utility to verify test framework functionality.

### diagnose_pytest.py
Diagnostic tool for troubleshooting pytest issues.

## 🔧 Maintenance

When adding new features to ESP Board Manager:

1. Add corresponding test cases
2. Run test suite to verify: `./run_pytest.sh -v`
3. Ensure 100% pass rate
4. Update documentation as needed

## 🐛 Troubleshooting

If tests fail:

1. **Check test output** for detailed error messages
2. **Run individual test** to isolate the issue:
   ```bash
   pytest test_board_selection.py::TestBoardSelectionByName::test_valid_board_name -v
   ```
3. **Use diagnostics**:
   ```bash
   python diagnose_pytest.py
   ```
4. **Review recent changes** to the main script

### Common Issues

**Automatic Installation Fails**
- **When**: Script tries to auto-install packages but fails
- **Possible Causes**:
  - No internet connection
  - pip3 not available
  - Permission issues
- **Solution**: Install manually:
  ```bash
  pip3 install pytest pytest-html pytest-cov
  ```
- **Note**: The script automatically installs missing packages. If auto-installation fails, you'll see an error message with manual installation instructions.

**Permission Errors During Installation**
- **When**: pip3 requires sudo/admin privileges
- **Solution**: Use `pip3 install --user` or install in a virtual environment:
  ```bash
  python3 -m venv venv
  source venv/bin/activate  # On Windows: venv\Scripts\activate
  pip install pytest pytest-html pytest-cov
  ```

## 📝 Requirements

- Python 3.7+
- pytest
- pytest-html (optional, for HTML reports)
- pytest-cov (optional, for coverage reports)

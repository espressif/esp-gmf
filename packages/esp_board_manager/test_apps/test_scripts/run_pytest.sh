#!/bin/bash

# Run pytest-based tests for ESP Board Manager
# This is the Python version 2 of the test suite

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║     ESP Board Manager - Python Test Suite                      ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# Function to check and install package
check_and_install() {
    local package=$1
    local display_name=$2

    if ! python3 -c "import $package" 2>/dev/null; then
        echo -e "${YELLOW}⚠️  $display_name is not installed. Installing...${NC}"
        if pip3 install "$display_name" --quiet; then
            echo -e "${GREEN}✅ $display_name installed successfully${NC}"
        else
            echo -e "${RED}❌ Failed to install $display_name${NC}"
            echo ""
            echo "Please install manually:"
            echo "  pip3 install $display_name"
            echo ""
            exit 1
        fi
    fi
}

# Check if pytest is installed, install if not
if ! command -v pytest &> /dev/null; then
    echo -e "${YELLOW}⚠️  pytest is not installed. Installing...${NC}"
    if pip3 install pytest --quiet; then
        echo -e "${GREEN}✅ pytest installed successfully${NC}"
    else
        echo -e "${RED}Error: Failed to install pytest${NC}"
        echo ""
        echo "Please install manually:"
        echo "  pip3 install pytest"
        echo ""
        exit 1
    fi
fi

echo -e "${CYAN}Test Configuration:${NC}"
echo "  Test Directory: $SCRIPT_DIR"
echo "  Python: $(python3 --version)"
echo "  Pytest: $(pytest --version)"
echo ""

# Parse command line arguments
PYTEST_ARGS=""
VERBOSE=""
REPORT_DIR="test_reports"

while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--verbose)
            VERBOSE="-vv"
            shift
            ;;
        -k|--keyword)
            PYTEST_ARGS="$PYTEST_ARGS -k $2"
            shift 2
            ;;
        -m|--marker)
            PYTEST_ARGS="$PYTEST_ARGS -m $2"
            shift 2
            ;;
        --html)
            PYTEST_ARGS="$PYTEST_ARGS --html=$REPORT_DIR/report.html --self-contained-html"
            mkdir -p "$REPORT_DIR"
            shift
            ;;
        --html=*)
            # Support --html=filename format
            HTML_FILE="${1#*=}"
            PYTEST_ARGS="$PYTEST_ARGS --html=$HTML_FILE --self-contained-html"
            mkdir -p "$(dirname "$HTML_FILE")"
            shift
            ;;
        --cov)
            PYTEST_ARGS="$PYTEST_ARGS --cov=. --cov-report=html:$REPORT_DIR/coverage"
            mkdir -p "$REPORT_DIR"
            shift
            ;;
        --parallel)
            PYTEST_ARGS="$PYTEST_ARGS -n auto"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -v, --verbose          Verbose output"
            echo "  -k, --keyword EXPR     Run tests matching keyword expression"
            echo "  -m, --marker MARK      Run tests with specific marker"
            echo "  --html                 Generate HTML report"
            echo "  --cov                  Generate coverage report"
            echo "  --parallel             Run tests in parallel"
            echo "  -h, --help             Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                                    # Run all tests"
            echo "  $0 -v                                 # Verbose output"
            echo "  $0 -k board_selection                 # Run board selection tests"
            echo "  $0 -m slow                            # Run tests marked as slow"
            echo "  $0 --html --cov                       # Generate reports"
            echo "  $0 --parallel                         # Run in parallel"
            echo ""
            exit 0
            ;;
        *)
            PYTEST_ARGS="$PYTEST_ARGS $1"
            shift
            ;;
    esac
done

# Check for required plugins before running and install if needed
if echo "$PYTEST_ARGS" | grep -q -- "--html"; then
    if ! python3 -c "import pytest_html" 2>/dev/null; then
        check_and_install "pytest_html" "pytest-html"
    fi
fi

if echo "$PYTEST_ARGS" | grep -q -- "--cov"; then
    if ! python3 -c "import pytest_cov" 2>/dev/null; then
        check_and_install "pytest_cov" "pytest-cov"
    fi
fi

# Run pytest
echo "════════════════════════════════════════════════════════════════"
echo ""
echo -e "${BLUE}Running tests...${NC}"
echo ""

pytest $VERBOSE $PYTEST_ARGS test_*.py

EXIT_CODE=$?

echo ""
echo "════════════════════════════════════════════════════════════════"
echo ""

if [ $EXIT_CODE -eq 0 ]; then
    echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║           ✅ ALL TESTS PASSED SUCCESSFULLY! ✅                ║${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
else
    echo -e "${RED}╔════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${RED}║                ❌ SOME TESTS FAILED! ❌                       ║${NC}"
    echo -e "${RED}╚════════════════════════════════════════════════════════════════╝${NC}"
fi

echo ""

# Show report locations if generated
if [ -d "$REPORT_DIR" ]; then
    echo -e "${CYAN}Reports generated:${NC}"
    if [ -f "$REPORT_DIR/report.html" ]; then
        echo "  HTML Report: $SCRIPT_DIR/$REPORT_DIR/report.html"
    fi
    if [ -d "$REPORT_DIR/coverage" ]; then
        echo "  Coverage Report: $SCRIPT_DIR/$REPORT_DIR/coverage/index.html"
    fi
    echo ""
fi

exit $EXIT_CODE


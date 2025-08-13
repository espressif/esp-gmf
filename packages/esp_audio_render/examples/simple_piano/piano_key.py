
import sys
import time
import argparse
import threading
import re
import fcntl
import os
import termios
import tty
import select

# Check and import required packages with helpful error messages
try:
    import serial
    if not hasattr(serial, 'Serial'):
        print("ERROR: Wrong 'serial' module found!")
        print('Please install pyserial with: pip install pyserial')
        print("If you have another 'serial' module, try: pip uninstall serial && pip install pyserial")
        sys.exit(1)
except ImportError:
    print('ERROR: pyserial package not found!')
    print('Please install it with: pip install pyserial')
    sys.exit(1)

# ====== ESP-IDF style logging colors ======
RESET = '\033[0m'
RED = '\033[31m'      # Error
YELLOW = '\033[33m'   # Warning
GREEN = '\033[32m'    # Info
BLUE = '\033[34m'     # Debug
CYAN = '\033[36m'     # ESP32 output
MAGENTA = '\033[35m'  # Piano notes

# Terminal control
CLEAR_LINE = '\033[K'
MOVE_UP = '\033[1A'

# Thread-safe output lock
output_lock = threading.Lock()

def safe_print(msg=''):
    """Thread-safe print function"""
    with output_lock:
        print(msg, flush=True)

def ESP_LOGE(tag, msg):
    safe_print(f'{RED}E ({int(time.time()*1000)}) {tag}: {msg}{RESET}')

def ESP_LOGW(tag, msg):
    safe_print(f'{YELLOW}W ({int(time.time()*1000)}) {tag}: {msg}{RESET}')

def ESP_LOGI(tag, msg):
    safe_print(f'{GREEN}I ({int(time.time()*1000)}) {tag}: {msg}{RESET}')

def ESP_LOGD(tag, msg):
    safe_print(f'{BLUE}D ({int(time.time()*1000)}) {tag}: {msg}{RESET}')

parser = argparse.ArgumentParser(
    description='Piano Key Controller for ESP32 (No Root Required)',
    formatter_class=argparse.RawDescriptionHelpFormatter,
    epilog="""
Piano Key Mapping:
  Numbers 1-7:     Play notes C4, D4, E4, F4, G4, A4, B4 (high octave)
  Letters Q,W,E,R,T,Y,U: Play notes C5, D5, E5, F5, G5, A5, B5 (higher octave)
  M:               Mute song playback
  ESC:             Send ESC command to ESP32
  Ctrl+C:          Exit program

Features:
  - Terminal-based real-time piano playing
  - Smart key handling: Auto-release previous key on new press
  - 400ms timeout for natural key release feel
  - Clean ESP32 log monitoring with colors
  - Thread-safe operation, no root permissions required

Dependencies:
  pip install pyserial

Example:
  python3 piano_key.py --port /dev/ttyUSB0 --baud 115200
"""
)
parser.add_argument(
    '--port', '-p',
    default='/dev/ttyUSB0',
    help='Serial port device (default: /dev/ttyUSB0)'
)
parser.add_argument(
    '--baud', '-b',
    type=int,
    default=115200,
    help='Baud rate (default: 115200)'
)
args = parser.parse_args()

# ====== UART setup ======
ser = None
try:
    ser = serial.Serial(args.port, args.baud, timeout=0)
    ESP_LOGI('PYTHON', f'Opened serial port {args.port} @ {args.baud}')
except Exception as e:
    ESP_LOGE('PYTHON', f'Failed to open serial port {args.port}: {e}')
    sys.exit(1)

# Regular number keys to octave 4 notes
note_mapping = {
    '1': 'C4', '2': 'D4', '3': 'E4', '4': 'F4',
    '5': 'G4', '6': 'A4', '7': 'B4'
}

# QWERTY keys for octave 3 notes (alternative to numpad - no root required)
qwerty_mapping = {
    'q': 'C5', 'w': 'D5', 'e': 'E5', 'r': 'F5',
    't': 'G5', 'y': 'A5', 'u': 'B5'
}

pressed_keys = set()
key_timestamps = {}  # Track when keys were last seen
running = True
RELEASE_TIMEOUT = 0.4  # Release keys after 400ms of no activity

# ====== Terminal-based piano keyboard handler ======
class TerminalPianoKeyboard:
    def __init__(self):
        self.fd = sys.stdin.fileno()
        self.old_settings = None
        self.current_key = None
        self.key_timestamp = 0

    def setup(self):
        """Setup terminal for raw key capture"""
        self.old_settings = termios.tcgetattr(self.fd)
        tty.setcbreak(self.fd)

    def cleanup(self):
        """Restore terminal settings"""
        if self.old_settings:
            termios.tcsetattr(self.fd, termios.TCSADRAIN, self.old_settings)

    def release_current_key(self):
        """Release the currently pressed key"""
        if self.current_key:
            # Send release command
            if self.current_key in note_mapping:
                note = note_mapping[self.current_key]
                send_piano_note('R', note)
                safe_print(f'🎹 {note} (High) ↑')
            elif self.current_key.lower() in qwerty_mapping:
                note = qwerty_mapping[self.current_key.lower()]
                send_piano_note('R', note)
                safe_print(f'🎵 {note} (Low) ↑')

            self.current_key = None
            self.key_timestamp = 0

    def press_new_key(self, key):
        """Press a new key (automatically releases previous key)"""
        # Release any currently pressed key first
        if self.current_key and self.current_key != key:
            self.release_current_key()

        # Press the new key (if not already pressed)
        if self.current_key != key:
            self.current_key = key
            self.key_timestamp = time.time()

            if key in note_mapping:
                note = note_mapping[key]
                send_piano_note('P', note)
                safe_print(f'🎹 {note} (High) ↓')
            elif key.lower() in qwerty_mapping:
                note = qwerty_mapping[key.lower()]
                send_piano_note('P', note)
                safe_print(f'🎵 {note} (Low) ↓')
        else:
            # Same key pressed again - just update timestamp
            self.key_timestamp = time.time()

    def handle_key_events(self):
        """Main key handling loop with improved press/release simulation"""
        global running
        current_time = time.time()

        # Check for new key presses
        if select.select([sys.stdin], [], [], 0.01)[0]:
            try:
                key = sys.stdin.read(1)

                # Handle special keys
                if ord(key) == 27:  # ESC
                    self.release_current_key()
                    send_piano_note('P', 'ESC')
                    safe_print('⏹️  ESC sent - stopping ESP32 piano')
                    return True
                elif ord(key) == 3:  # Ctrl+C
                    self.release_current_key()
                    safe_print('👋 Exiting piano controller...')
                    running = False
                    return False
                if key == 'm':
                    self.release_current_key()
                    send_piano_note('P', 'MUTE') # M to Mute command
                    safe_print('🔇 Mute sent to ESP32 piano')
                    return True

                # Handle piano keys
                elif key in note_mapping or key.lower() in qwerty_mapping:
                    self.press_new_key(key)

            except (OSError, IOError):
                pass

        # Check for key release (timeout-based)
        if (self.current_key and
            current_time - self.key_timestamp > RELEASE_TIMEOUT):
            self.release_current_key()

        return True


# ====== ESP-IDF log parsing ======
def read_serial_output():
    """Read ESP32 output and display with colors (simplified)"""
    global running
    buffer = ''

    while running:
        try:
            if ser and ser.is_open and ser.in_waiting:
                # Read all available data
                data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                buffer += data

                # Process complete lines
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    line = line.strip()
                    if line:
                        safe_print(f'{CYAN}ESP32 {parse_esp_log_line_return(line)}{RESET}')

        except Exception:
            # Silently continue on errors
            pass

        time.sleep(0.05)

def parse_esp_log_line_return(line):
    """Parse ESP-IDF log lines and return colored string"""
    # ESP-IDF log format: LEVEL (timestamp) TAG: message
    log_pattern = r'^([EWIVD])\s*\(\s*\d+\)\s*([^:]+):\s*(.*)$'
    match = re.match(log_pattern, line.strip())

    if match:
        level, tag, message = match.groups()
        if level == 'E':  # Error
            return f'{RED}{line}{RESET}'
        elif level == 'W':  # Warning
            return f'{YELLOW}{line}{RESET}'
        elif level == 'I':  # Info
            return f'{GREEN}{line}{RESET}'
        elif level == 'D' or level == 'V':  # Debug/Verbose
            return f'{BLUE}{line}{RESET}'
    return line  # Return as-is if not a log line

# ====== Piano key handling ======
def send_piano_note(event_type, note):
    """Send piano note command to ESP32"""
    msg = f'{event_type}:{note}\n'
    try:
        ser.write(msg.encode('utf-8'))
    except Exception as e:
        ESP_LOGE('PIANO', f'UART send failed: {e}')

def show_header():
    """Display the clean header interface"""
    safe_print(f'\n')
    safe_print(f'{GREEN}╔══════════════════════════════════════════════╗{RESET}')
    safe_print(f'{GREEN}║          🎹 ESP32 Piano Controller          ║{RESET}')
    safe_print(f'{GREEN}╠══════════════════════════════════════════════╣{RESET}')
    safe_print(f'{GREEN}║ Numbers 1-7: C4-B4 (High octave)            ║{RESET}')
    safe_print(f'{GREEN}║ Letters Q-U: C5-B5 (High octave)            ║{RESET}')
    safe_print(f'{GREEN}║ ESC: Stop ESP32 piano                       ║{RESET}')
    safe_print(f'{GREEN}║ M: Mute playback                            ║{RESET}')
    safe_print(f'{GREEN}║ Ctrl+C: Quit program                        ║{RESET}')
    safe_print(f'{GREEN}╚══════════════════════════════════════════════╝{RESET}')
    safe_print(f'\n{BLUE}🎵 Terminal Piano: Real-time playing!{RESET}')
    safe_print(f'{BLUE}↓ = Press, ↑ = Release (auto 400ms or new key){RESET}\n')

def run_terminal_piano():
    """Run the terminal-based piano with press/release events"""
    keyboard_handler = TerminalPianoKeyboard()

    try:
        keyboard_handler.setup()
        ESP_LOGI('PIANO', 'Terminal keyboard ready - real-time press/release!')

        while running:
            if not keyboard_handler.handle_key_events():
              break
            time.sleep(0.005)  # Small delay to prevent excessive CPU usage

    except Exception as e:
        ESP_LOGE('PIANO', f'Terminal keyboard error: {e}')
    finally:
        keyboard_handler.cleanup()

if __name__ == '__main__':
    try:
        show_header()

        # Start serial output reader thread
        serial_thread = threading.Thread(target=read_serial_output, daemon=True)
        serial_thread.start()

        # Run terminal-based piano
        ESP_LOGI('PIANO', 'Starting terminal piano mode')
        run_terminal_piano()
        safe_print(f'\n{YELLOW}Piano controller stopped.{RESET}')

    except KeyboardInterrupt:
        safe_print(f'\n{YELLOW}Interrupted by user.{RESET}')
    except Exception as e:
        ESP_LOGE('PIANO', f'Error: {e}')
    finally:
        # Cleanup
        running = False
        if ser and ser.is_open:
            ser.close()
            ESP_LOGI('PIANO', 'Serial connection closed')
        safe_print(f'{GREEN}Goodbye! 👋{RESET}')

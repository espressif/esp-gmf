# FFT Spectrum Print

- [中文版](./README_CN.md)
- Difficulty: ⭐

## Example Overview

This example shows basic `gmf_fft` usage. It generates a multi-tone cosine signal, runs a 512-point Q15 real FFT, prints a dB-scale spectrum bar chart in the serial terminal, then runs IFFT and prints the round-trip peak error.

- Spectrum print: generate 3 cosine tones, run real FFT, and print a spectrum bar chart in the serial terminal.
- Round-trip check: run IFFT on the FFT output and print the scaled peak error.

### Typical Use Cases

- Quickly verify `gmf_fft` initialization, forward transform, inverse transform, and resource release.
- Observe spectral peaks at configured frequency bins and confirm the half-spectrum output layout.
- Use as a reference example for integrating Q15 real FFT into user projects.

### Operation

- The example first generates 3 cosine tones at frequency bins 8, 24, and 40, with amplitudes 1000, 5000, and 3000.
- It calls `esp_gmf_fft_forward` to run Q15 real FFT and output `N/2 + 1` half-spectrum frequency bins.
- It converts each frequency bin magnitude to dB and prints a vertical bar chart.
- It calls `esp_gmf_fft_inverse` to restore the time-domain signal and prints the scaled peak error.

## Environment Setup

### Hardware Requirements

- **Development board**: ESP32-S3 or ESP32-P4 development board.
- **Resource requirements**: Download and debug environment capable of viewing serial output.

### Default IDF Branch

This example targets ESP-IDF v5.x. The actual supported range depends on the component manager and project build configuration.

### Prerequisites

Before using this example, it is recommended to understand ESP-IDF example project builds, Q15 fixed-point data format, and real FFT half-spectrum layout.

## Build And Flash

### Build Preparation

Before building this example, make sure the ESP-IDF environment is configured. If it is not configured, run the following scripts from the ESP-IDF root directory. For full instructions, see the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html).

```
./install.sh
. ./export.sh
```

Brief steps:

- Enter the example project directory:

```
cd examples/fft_spectrum_print
```

- Set the target chip:

```
idf.py set-target esp32p4
```

To run on ESP32-S3, set the target to `esp32s3`.

### Project Configuration

By default, this example uses a 512-point FFT and displays the first 56 frequency bins. You can modify the following macros in `examples/fft_spectrum_print/main/fft_spectrum_print.c`:

| Macro | Default | Description |
|-------|---------|-------------|
| `N_FFT` | 512 | FFT size, power-of-two in [32, 8192] |
| `N_TONES` | 3 | Number of generated cosine tones |
| `TONE_BIN[]` | {8, 24, 40} | Frequency bin for each tone |
| `TONE_AMP[]` | {1000, 5000, 3000} | Q15 amplitude for each tone |
| `PLOT_BINS` | 56 | Number of X-axis bins shown in the chart |
| `PLOT_HEIGHT` | 20 | Number of Y-axis rows |
| `DB_FLOOR` | -60 | Lower dB limit |

### Build And Flash

- Build the example:

```
idf.py build
```

- Flash the example and run monitor to view serial output (replace PORT with your port name):

```
idf.py -p PORT flash monitor
```

- Exit monitor with `Ctrl-]`

## How To Use The Example

### Functionality And Usage

After the example starts, it runs the following steps:

1. Allocate an aligned buffer with `ESP_GMF_FFT_BUFFER_SIZE(N_FFT)` `int16_t` elements.
2. Generate 3 cosine tones and save the original input.
3. Initialize `esp_gmf_fft_handle_t` and run `esp_gmf_fft_forward`.
4. Print the spectrum chart, then run `esp_gmf_fft_inverse`.
5. Print the round-trip peak error and release resources.

### Log Output

The key output is shown below:

```text
  esp_gmf_fft  N=512  |  signal: cos(bin=8,A=1000) + cos(bin=24,A=5000) + cos(bin=40,A=3000)
  Peak: bin=24  mag=...

  -3 |        #               #              #              |
  -6 |        #               #              #              |
     ...
     +--------------------------------------------------------+
      0       8      16      24      32      40      48
      Bin

  Round-trip peak error after scaling by N/4: ...
```

The three configured frequency bins should show clear peaks, while other bins stay close to the noise floor.

## Troubleshooting

### Initialization Failed

If `esp_gmf_fft_init` fails, make sure `N_FFT` is a power of two and is in the [32, 8192] range.

### Unexpected Spectrum Peaks

Make sure each frequency bin in `TONE_BIN[]` is smaller than `N_FFT / 2`, and avoid overflow from the sum of multiple tone amplitudes beyond the Q15 `int16_t` range.

## Technical Support

Please use the following links for technical support:

- For technical support, visit the [esp32.com](https://esp32.com/viewforum.php?f=20) forum
- For bug reports or feature requests, create a [GitLab issue](https://gitlab.espressif.cn/adf/gmf_fft/-/issues)

We will reply as soon as possible.

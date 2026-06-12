GMF FFT
=======

:link_to_translation:`zh_CN:[中文]`

GMF FFT is a fixed-point Q15 real FFT/IFFT component for ESP-IDF. It uses the ``int16_t`` Q15 data format to reduce computational and storage overhead. The implementation includes a generic C version and PIE vector assembly optimizations for Xtensa / RISC-V, achieving lower latency than a pure-software DIT at typical sizes. By exploiting the Hermitian conjugate symmetry of real signals, the forward transform retains only ``N/2 + 1`` frequency bins, halving the frequency-domain storage and computation overhead. Suitable for embedded scenarios without FPU or with tight latency requirements.

Key Features
------------

- Real FFT / IFFT: power-of-2 lengths from 32 to 8192 points
- Q15 fixed-point data: ``int16_t`` format, reducing computational and storage cost
- Real-signal optimization: Hermitian conjugate symmetry; the forward transform outputs only ``N/2 + 1`` frequency bins
- Hardware acceleration: PIE vector assembly optimizations for Xtensa / RISC-V; latency at typical lengths is lower than pure-software implementations
- Generic fallback: a C version is included in the package and runs on SoCs without PIE
- Thread-safe: the same handle can be shared by multiple threads as long as each thread uses its own buffer
- Use cases: real-time signal processing without FPU or with tight latency requirements

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0
"""
Decode esp_capture frame dump files to WAV.

The audio_capture dual-sink test (audio_capture_run_dual_sink) writes concatenated
encoded frames to SD card:

  aud_0.raw - AAC with ADTS headers (16 kHz, stereo)  [sink 0]
  aud_1.raw - G.711 A-law raw frames (8 kHz, mono)    [sink 1]

Each file is a byte stream of encoder output frames with no extra container.

Examples:
  # Decode both dual-sink dumps (auto-detect from aud_<N>.raw filename)
  python decode_capture_frames.py aud_0.raw aud_1.raw

  # Decode a single file with explicit format
  python decode_capture_frames.py --format aac --sample-rate 16000 --channels 2 aud_0.raw -o out.wav
  python decode_capture_frames.py --format g711a --sample-rate 8000 --channels 1 aud_1.raw -o out.wav

  # Prefer pure-Python G.711 decoder (AAC still needs ffmpeg)
  python decode_capture_frames.py --backend python aud_1.raw
"""

from __future__ import annotations

import argparse
import audioop
import re
import shutil
import struct
import subprocess
import sys
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

# Defaults from examples/audio_capture/main/settings.h and audio_capture.c
DUAL_SINK_PRESETS = {
    0: {
        'format': 'aac',
        'sample_rate': 16000,
        'channels': 2,
    },
    1: {
        'format': 'g711a',
        'sample_rate': 8000,
        'channels': 1,
    },
}

AUD_RAW_RE = re.compile(r'aud_(\d+)\.raw$', re.IGNORECASE)


@dataclass(frozen=True)
class AudioParams:
    format: str
    sample_rate: int
    channels: int
    aac_no_adts: bool = False


def _alaw_decode_sample(a_val: int) -> int:
    """Decode one G.711 A-law byte to signed 16-bit PCM."""
    a_val ^= 0x55
    t = (a_val & 0x0F) << 4
    seg = (a_val & 0x70) >> 4
    if seg:
        t = (t + 0x108) << (seg - 1)
    else:
        t += 8
    if a_val & 0x80:
        return t
    return -t


def decode_g711a_python(data: bytes) -> bytes:
    """Decode G.711 A-law bytes to 16-bit little-endian PCM."""
    try:
        return audioop.alaw2lin(data, 2)
    except AttributeError:
        # audioop removed in Python 3.13+
        samples = [_alaw_decode_sample(b) for b in data]
        return struct.pack(f'<{len(samples)}h', *samples)


def write_wav(
    pcm: bytes,
    output_path: Path,
    sample_rate: int,
    channels: int,
    sample_width: int = 2,
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(output_path), 'wb') as wav_file:
        wav_file.setnchannels(channels)
        wav_file.setsampwidth(sample_width)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(pcm)


def decode_g711a_to_wav(data: bytes, output_path: Path, params: AudioParams) -> None:
    pcm = decode_g711a_python(data)
    write_wav(pcm, output_path, params.sample_rate, params.channels)


def _ffmpeg_bin() -> Optional[str]:
    return shutil.which('ffmpeg')


def decode_with_ffmpeg(
    input_path: Path,
    output_path: Path,
    params: AudioParams,
) -> None:
    ffmpeg = _ffmpeg_bin()
    if ffmpeg is None:
        raise RuntimeError('ffmpeg not found in PATH')

    output_path.parent.mkdir(parents=True, exist_ok=True)

    if params.format == 'g711a':
        cmd = [
            ffmpeg,
            '-hide_banner',
            '-loglevel',
            'error',
            '-y',
            '-f',
            'alaw',
            '-ar',
            str(params.sample_rate),
            '-ac',
            str(params.channels),
            '-i',
            str(input_path),
            str(output_path),
        ]
    elif params.format == 'aac':
        cmd = [
            ffmpeg,
            '-hide_banner',
            '-loglevel',
            'error',
            '-y',
        ]
        if params.aac_no_adts:
            # Raw AAC-LC access units without ADTS sync word.
            cmd.extend(
                [
                    '-f',
                    'aac',
                    '-ar',
                    str(params.sample_rate),
                    '-ac',
                    str(params.channels),
                ]
            )
        cmd.extend(['-i', str(input_path), str(output_path)])
    else:
        raise ValueError(f'Unsupported format for ffmpeg: {params.format}')

    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        err = (proc.stderr or proc.stdout or '').strip()
        raise RuntimeError(f'ffmpeg failed ({proc.returncode}): {err}')


def decode_file(
    input_path: Path,
    output_path: Optional[Path],
    params: AudioParams,
    backend: str,
) -> Path:
    if not input_path.is_file():
        raise FileNotFoundError(f'Input file not found: {input_path}')

    if output_path is None:
        output_path = input_path.with_suffix('.wav')

    data = input_path.read_bytes()
    if not data:
        raise ValueError(f'Input file is empty: {input_path}')

    use_ffmpeg = backend == 'ffmpeg' or (backend == 'auto' and params.format == 'aac')
    if use_ffmpeg:
        decode_with_ffmpeg(input_path, output_path, params)
    elif params.format == 'g711a':
        decode_g711a_to_wav(data, output_path, params)
    else:
        raise RuntimeError(
            f'Format {params.format!r} requires ffmpeg; install ffmpeg or use --backend ffmpeg'
        )

    print(f'{input_path} -> {output_path} ({params.format}, {params.sample_rate} Hz, {params.channels} ch)')
    return output_path


def infer_params_from_filename(path: Path) -> Optional[AudioParams]:
    match = AUD_RAW_RE.search(path.name)
    if not match:
        return None
    sink_idx = int(match.group(1))
    preset = DUAL_SINK_PRESETS.get(sink_idx)
    if preset is None:
        return None
    return AudioParams(
        format=preset['format'],
        sample_rate=preset['sample_rate'],
        channels=preset['channels'],
    )


def build_params(args: argparse.Namespace, input_path: Path) -> AudioParams:
    inferred = infer_params_from_filename(input_path)
    fmt = args.format or (inferred.format if inferred else None)
    if fmt is None:
        raise ValueError(
            f'Cannot infer format for {input_path.name}; pass --format aac|g711a'
        )

    sample_rate = args.sample_rate
    channels = args.channels
    if sample_rate is None:
        sample_rate = inferred.sample_rate if inferred else None
    if channels is None:
        channels = inferred.channels if inferred else None

    if sample_rate is None or channels is None:
        raise ValueError(
            f'Missing sample rate/channels for {input_path.name}; '
            'pass --sample-rate and --channels'
        )

    return AudioParams(
        format=fmt.lower(),
        sample_rate=sample_rate,
        channels=channels,
        aac_no_adts=args.aac_no_adts,
    )


def parse_args(argv: Optional[Iterable[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description='Decode esp_capture AAC / G.711A frame dump files to WAV.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        'inputs',
        nargs='+',
        type=Path,
        help='Input .raw frame dump file(s), e.g. aud_0.raw aud_1.raw',
    )
    parser.add_argument(
        '-o',
        '--output',
        type=Path,
        help='Output WAV path (only valid with a single input file)',
    )
    parser.add_argument(
        '--format',
        choices=('aac', 'g711a'),
        help='Encoded format (auto-detected for aud_<N>.raw dual-sink dumps)',
    )
    parser.add_argument(
        '--sample-rate',
        type=int,
        help='Sample rate in Hz (defaults: AAC 16000, G.711A 8000 for aud_<N>.raw)',
    )
    parser.add_argument(
        '--channels',
        type=int,
        help='Channel count (defaults: AAC 2, G.711A 1 for aud_<N>.raw)',
    )
    parser.add_argument(
        '--aac-no-adts',
        action='store_true',
        help='AAC input has no ADTS headers (raw AAC access units)',
    )
    parser.add_argument(
        '--backend',
        choices=('auto', 'ffmpeg', 'python'),
        default='auto',
        help='Decoder backend: auto uses ffmpeg for AAC and python/ffmpeg for G.711A',
    )
    return parser.parse_args(list(argv) if argv is not None else None)


def main(argv: Optional[Iterable[str]] = None) -> int:
    args = parse_args(argv)

    if args.output is not None and len(args.inputs) != 1:
        print('error: --output can only be used with one input file', file=sys.stderr)
        return 2

    if args.backend == 'python' and args.format == 'aac':
        print('error: AAC decoding requires ffmpeg (--backend auto|ffmpeg)', file=sys.stderr)
        return 2

    try:
        for input_path in args.inputs:
            params = build_params(args, input_path)
            output_path = args.output if len(args.inputs) == 1 else None
            decode_file(input_path, output_path, params, args.backend)
    except (FileNotFoundError, ValueError, RuntimeError) as exc:
        print(f'error: {exc}', file=sys.stderr)
        return 1

    return 0


if __name__ == '__main__':
    raise SystemExit(main())

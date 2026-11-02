#!/usr/bin/env python3

import array
import pathlib
import struct
import sys
import wave

if len(sys.argv) != 3:
    raise SystemExit(f"Usage: {sys.argv[0]} INPUT.bin OUTPUT.wav")

source = pathlib.Path(sys.argv[1])
destination = pathlib.Path(sys.argv[2])

with source.open("rb") as file:
    header = file.read(24)
    if len(header) != 24:
        raise SystemExit("File is too short to contain a Music Box header")

    # Raspberry Pi Music Box files are little-endian.
    magic, sample_rate, reserved, frames = struct.unpack("<8sIIQ", header)

    if magic != b"MBSAMP1\0":
        raise SystemExit(f"Unsupported sample magic: {magic!r}")

    channel_bytes = frames * 4
    left_bytes = file.read(channel_bytes)
    right_bytes = file.read(channel_bytes)

    if len(left_bytes) != channel_bytes or len(right_bytes) != channel_bytes:
        raise SystemExit("Sample payload is truncated")

    if file.read(1):
        raise SystemExit("Sample file contains unexpected trailing data")

left = array.array("f")
right = array.array("f")
left.frombytes(left_bytes)
right.frombytes(right_bytes)

# array('f') uses native byte order. This is only needed if the conversion
# happens on a big-endian computer rather than on the Raspberry Pi.
if sys.byteorder != "little":
    left.byteswap()
    right.byteswap()

pcm = array.array("h")
for left_sample, right_sample in zip(left, right):
    left_sample = max(-1.0, min(1.0, left_sample))
    right_sample = max(-1.0, min(1.0, right_sample))
    pcm.append(round(left_sample * 32767.0))
    pcm.append(round(right_sample * 32767.0))

with wave.open(str(destination), "wb") as output:
    output.setnchannels(2)
    output.setsampwidth(2)
    output.setframerate(sample_rate)
    output.writeframes(pcm.tobytes())

print(
    f"Wrote {destination}: stereo, {sample_rate} Hz, "
    f"{frames} frames, {frames / sample_rate:.3f} seconds"
)

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Dict

MAGIC = 0x52444152
MAGIC_BYTES = struct.pack("<I", MAGIC)
VERSION = 1
HEADER_SIZE = 64
PAYLOAD_SIZE = 20_000
FRAME_SIZE = HEADER_SIZE + PAYLOAD_SIZE
SAMPLES_PER_SLOPE = 10_000

FLAG_NAMES: Dict[int, str] = {
    0: "DROPPED",
    1: "DCMI_OVR",
    2: "USB_LATE",
    3: "MUX_MISSING",
    4: "ALIGN_LOST",
}

HEADER_STRUCT = struct.Struct("<IHHIIIHHIIHhQI16s")
assert HEADER_STRUCT.size == HEADER_SIZE


@dataclass(frozen=True)
class RadarHeader:
    magic: int
    version: int
    header_len: int
    frame_len: int
    slope_seq: int
    triangle_seq: int
    slope_id: int
    flags: int
    sample_rate_hz: int
    samples_per_slope: int
    adc_bits: int
    pipeline_offset: int
    timestamp_us: int
    muxout_count: int

    @property
    def flag_names(self) -> list[str]:
        return [name for bit, name in FLAG_NAMES.items() if self.flags & (1 << bit)]

    def validate(self) -> None:
        if self.magic != MAGIC:
            raise ValueError(f"bad magic 0x{self.magic:08x}")
        if self.version != VERSION:
            raise ValueError(f"unsupported protocol version {self.version}")
        if self.header_len != HEADER_SIZE:
            raise ValueError(f"bad header length {self.header_len}")
        if self.frame_len != FRAME_SIZE:
            raise ValueError(f"bad frame length {self.frame_len}")
        if self.samples_per_slope != SAMPLES_PER_SLOPE:
            raise ValueError(f"unexpected samples_per_slope {self.samples_per_slope}")


def parse_header(buf: bytes | bytearray | memoryview) -> RadarHeader:
    if len(buf) < HEADER_SIZE:
        raise ValueError("buffer shorter than FMCW header")
    fields = HEADER_STRUCT.unpack_from(buf)
    return RadarHeader(*fields[:-1])


def pack_header(
    *,
    slope_seq: int,
    slope_id: int | None = None,
    flags: int = 0,
    timestamp_us: int = 0,
    muxout_count: int = 0,
) -> bytes:
    if slope_id is None:
        slope_id = slope_seq & 1
    return HEADER_STRUCT.pack(
        MAGIC,
        VERSION,
        HEADER_SIZE,
        FRAME_SIZE,
        slope_seq,
        slope_seq // 2,
        slope_id,
        flags,
        10_000_000,
        SAMPLES_PER_SLOPE,
        12,
        3,
        timestamp_us,
        muxout_count,
        b"\x00" * 16,
    )


def find_magic(buf: bytes | bytearray, start: int = 0) -> int:
    return bytes(buf).find(MAGIC_BYTES, start)


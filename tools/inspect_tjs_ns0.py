#!/usr/bin/env python3
"""Temporary inspector for PackinOne TJS/ns0 structured-data packs."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import struct
from pathlib import Path

MASK32 = 0xFFFFFFFF


def rol(value: int, amount: int) -> int:
    return ((value << amount) | (value >> (32 - amount))) & MASK32


def xxh32(data: bytes, seed: int) -> int:
    prime1, prime2 = 0x9E3779B1, 0x85EBCA77
    prime3, prime4, prime5 = 0xC2B2AE3D, 0x27D4EB2F, 0x165667B1

    def rnd(acc: int, word: int) -> int:
        return (rol((acc + word * prime2) & MASK32, 13) * prime1) & MASK32

    pos = 0
    if len(data) >= 16:
        values = [
            (seed + prime1 + prime2) & MASK32,
            (seed + prime2) & MASK32,
            seed,
            (seed - prime1) & MASK32,
        ]
        while pos <= len(data) - 16:
            for i in range(4):
                values[i] = rnd(values[i], struct.unpack_from("<I", data, pos)[0])
                pos += 4
        result = rol(values[0], 1) + rol(values[1], 7)
        result += rol(values[2], 12) + rol(values[3], 18)
        result &= MASK32
    else:
        result = (seed + prime5) & MASK32
    result = (result + len(data)) & MASK32
    while pos + 4 <= len(data):
        result = (result + struct.unpack_from("<I", data, pos)[0] * prime3) & MASK32
        result = (rol(result, 17) * prime4) & MASK32
        pos += 4
    while pos < len(data):
        result = (result + data[pos] * prime5) & MASK32
        result = (rol(result, 11) * prime1) & MASK32
        pos += 1
    result ^= result >> 15
    result = (result * prime2) & MASK32
    result ^= result >> 13
    result = (result * prime3) & MASK32
    result ^= result >> 16
    return result & MASK32


def qr(state: list[int], a: int, b: int, c: int, d: int) -> None:
    state[a] = (state[a] + state[b]) & MASK32
    state[d] = rol(state[d] ^ state[a], 16)
    state[c] = (state[c] + state[d]) & MASK32
    state[b] = rol(state[b] ^ state[c], 12)
    state[a] = (state[a] + state[b]) & MASK32
    state[d] = rol(state[d] ^ state[a], 8)
    state[c] = (state[c] + state[d]) & MASK32
    state[b] = rol(state[b] ^ state[c], 7)


def chacha8_block(source: list[int]) -> bytes:
    state = source.copy()
    for _ in range(4):
        qr(state, 0, 4, 8, 12)
        qr(state, 1, 5, 9, 13)
        qr(state, 2, 6, 10, 14)
        qr(state, 3, 7, 11, 15)
        qr(state, 0, 5, 10, 15)
        qr(state, 1, 6, 11, 12)
        qr(state, 2, 7, 8, 13)
        qr(state, 3, 4, 9, 14)
    return struct.pack("<16I", *[((a + b) & MASK32) for a, b in zip(state, source)])


def decrypt(payload: bytes, seed: int, iv: bytes) -> bytes:
    key = hashlib.blake2s(iv, key=struct.pack("<I", seed), digest_size=32).digest()
    words = [0x61707865, 0x3320646E, 0x79622D32, 0x6B206574]
    words.extend(struct.unpack("<8I", key))
    words.extend([0, 0, xxh32(iv, seed), seed])
    zero_fallback = (seed ^ words[14]) & MASK32
    if zero_fallback == 0:
        zero_fallback = seed or MASK32
    output = bytearray(payload)
    position = 0
    while position < len(output):
        stream = bytearray(1024)
        stream[:64] = chacha8_block(words)
        for offset in range(64, 1024, 4):
            value = struct.unpack_from("<I", stream, offset - 64)[0]
            value ^= (value << 13) & MASK32
            value ^= value >> 17
            value ^= (value << 5) & MASK32
            value &= MASK32
            if value == 0:
                value = zero_fallback
            struct.pack_into("<I", stream, offset, value)
        size = min(1024, len(output) - position)
        for index in range(size):
            output[position + index] ^= stream[index]
        position += size
        words[12] = (words[12] + 1) & MASK32
        if words[12] == 0:
            words[13] = (words[13] + 1) & MASK32
    return bytes(output)


def lz4_library() -> ctypes.CDLL:
    for name in ("/opt/homebrew/lib/liblz4.dylib", "liblz4.dylib", "liblz4.so.1"):
        try:
            library = ctypes.CDLL(name)
            library.LZ4_decompress_safe.argtypes = [
                ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int
            ]
            library.LZ4_decompress_safe.restype = ctypes.c_int
            return library
        except OSError:
            pass
    raise RuntimeError("liblz4 not found")


def decompress_packinone(payload: bytes) -> bytes:
    library = lz4_library()
    using_dictionary = library.LZ4_decompress_safe_usingDict
    using_dictionary.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                  ctypes.c_int, ctypes.c_int,
                                  ctypes.c_void_p, ctypes.c_int]
    using_dictionary.restype = ctypes.c_int
    output = bytearray()
    position = 0
    while position < len(payload):
        if position + 2 > len(payload):
            raise ValueError("truncated LZ4 block length")
        size = struct.unpack_from("<H", payload, position)[0]
        position += 2
        block = payload[position:position + size]
        if not block or len(block) != size:
            raise ValueError("invalid LZ4 block")
        capacity = 65536
        while capacity <= 256 * 1024 * 1024:
            destination = ctypes.create_string_buffer(capacity)
            source = ctypes.create_string_buffer(block)
            dictionary_bytes = bytes(output[-65536:])
            if dictionary_bytes:
                dictionary = ctypes.create_string_buffer(dictionary_bytes)
                decoded = using_dictionary(source, destination, len(block),
                                           capacity, dictionary,
                                           len(dictionary_bytes))
            else:
                decoded = library.LZ4_decompress_safe(source, destination,
                                                      len(block), capacity)
            if decoded >= 0:
                output.extend(destination.raw[:decoded])
                break
            capacity *= 2
        else:
            raise ValueError("cannot decompress LZ4 block")
        position += size
    return bytes(output)


class Checker:
    def __init__(self, seed: int):
        self.seed = ((seed & 0xFFFFFF00) | ((seed ^ (seed >> 24)) & 0xFF)) & MASK32

    @staticmethod
    def rnd(data: bytearray) -> None:
        doubled = (data[0] * 2) & 0xFF
        a = data[0] ^ doubled
        b = (a >> 2) ^ data[2]
        b = (b >> 3) ^ data[2] ^ a
        data[0], data[1], data[2] = data[1], data[2], b & 0xFF

    def next(self, kind: int) -> int:
        data = bytearray(struct.pack("<I", self.seed))
        if kind == 0:
            return data[2]
        self.rnd(data)
        self.seed = struct.unpack("<I", data)[0]
        return data[2]

    def final(self) -> int:
        data = bytearray(struct.pack("<I", self.seed))
        for _ in range(3):
            self.rnd(data)
        data[0], data[2] = data[2], data[0]
        return struct.unpack("<I", data)[0]


class Reader:
    def __init__(self, data: bytes, checker: Checker):
        self.data = data
        self.pos = 0
        self.checker = checker

    def take(self, size: int) -> bytes:
        value = self.data[self.pos:self.pos + size]
        if len(value) != size:
            raise ValueError("truncated value")
        self.pos += size
        return value

    def u16(self) -> int:
        return struct.unpack("<H", self.take(2))[0]

    def u32(self) -> int:
        return struct.unpack("<I", self.take(4))[0]

    def string(self) -> str:
        size = self.u32()
        return self.take(size * 2).decode("utf-16le", errors="strict")

    def value(self, depth: int = 0):
        if depth > 128:
            raise ValueError("nesting too deep")
        encoded = self.u16()
        kind, check = encoded & 0xFF, encoded >> 8
        expected = self.checker.next(kind)
        if check != expected:
            raise ValueError(f"byte check failed at {self.pos - 2}: {check:#x} != {expected:#x}")
        if kind == 0:
            return None
        if kind == 2:
            return self.string()
        if kind == 4:
            return struct.unpack("<q", self.take(8))[0]
        if kind == 5:
            return struct.unpack("<d", self.take(8))[0]
        if kind == 0x81:
            return [self.value(depth + 1) for _ in range(self.u32())]
        if kind == 0xC1:
            return {self.string(): self.value(depth + 1) for _ in range(self.u32())}
        raise ValueError(f"unknown value kind {kind:#x}")


def load(path: Path):
    data = path.read_bytes()
    if len(data) < 16 or data[:4] != b"TJS/" or data[5:8] != b"s0\0":
        raise ValueError("not a TJS/ns0 data pack")
    compression, seed, crypt, iv_size = chr(data[4]), *struct.unpack_from("<IHH", data, 8)
    iv = data[16:16 + iv_size]
    payload = data[16 + iv_size:]
    if crypt == 1:
        payload = decrypt(payload, seed, iv)
    elif crypt != 0:
        raise ValueError(f"unsupported encryption {crypt}")
    if compression == "4":
        payload = decompress_packinone(payload)
    elif compression != "n":
        raise ValueError(f"unsupported compression {compression!r}")
    checker = Checker(seed)
    reader = Reader(payload, checker)
    value = reader.value()
    final = reader.u32()
    if (final & 0xFFFFFF) != (checker.final() & 0xFFFFFF):
        raise ValueError("checksum mismatch")
    return value


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--compact", action="store_true")
    args = parser.parse_args()
    for path in args.paths:
        value = load(path)
        print(f"=== {path} ===")
        print(json.dumps(value, ensure_ascii=False,
                         indent=None if args.compact else 2,
                         separators=(",", ":") if args.compact else None))


if __name__ == "__main__":
    main()

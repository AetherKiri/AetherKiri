"""Opt-in frame and input bridge for Ren'Py 8.5.x.

The hook targets ``renpy.display.core.Interface.draw_screen`` (the stable
instance method used by the SDK), captures the rendered ``surftree`` after the
real draw, and registers an input pump as a periodic callback so input is
consumed even when the interaction is idle.
"""
from __future__ import annotations

import json
import os
import struct
import tempfile
import ctypes
from pathlib import Path

MAGIC = b"AKRF1\0\0\0"


class Overlay:
    def __init__(self) -> None:
        frame = os.environ.get("AETHERKIRI_RENPY_FRAME")
        input_path = os.environ.get("AETHERKIRI_RENPY_INPUT")
        self.frame_path = Path(frame) if frame else None
        self.input_path = Path(input_path) if input_path else None
        error = os.environ.get("AETHERKIRI_RENPY_ERROR")
        self.error_path = Path(error) if error else None
        self.serial = 0
        self._input_offset = 0

    def _report(self, where: str, error: BaseException) -> None:
        """Report bridge failures without taking down the Ren'Py game."""
        if self.error_path is None:
            return
        try:
            self.error_path.parent.mkdir(parents=True, exist_ok=True)
            with self.error_path.open("a", encoding="utf-8") as stream:
                stream.write(f"{where}: {type(error).__name__}: {error}\n")
        except Exception:
            pass

    @staticmethod
    def _surface_rgba(surface) -> tuple[int, int, bytes]:
        """Copy a Ren'Py pygame surface into tightly packed RGBA bytes.

        Ren'Py's pygame compatibility layer does not provide pygame.image
        ``tostring``. ``Surface._pixels_address`` is the low-level pointer
        exposed by the 8.5.x surface type; copy it while the renderer-owned
        surface is alive, then unpack according to its channel masks.
        """
        width, height = surface.get_size()
        bytesize = int(surface.get_bytesize())
        pitch = int(surface.get_pitch())
        address = getattr(surface, "_pixels_address", 0)
        if width <= 0 or height <= 0 or bytesize <= 0 or pitch < width * bytesize:
            raise ValueError("invalid Ren'Py surface geometry")
        if not isinstance(address, int) or address <= 0:
            raise ValueError("Ren'Py surface has no readable pixel address")
        raw = ctypes.string_at(address, pitch * height)
        masks = tuple(int(v) for v in surface.get_masks())
        shifts = tuple(int(v) for v in surface.get_shifts())
        losses = tuple(int(v) for v in surface.get_losses())

        def channel(pixel: int, index: int, default: int) -> int:
            mask = masks[index] if index < len(masks) else 0
            if not mask:
                return default
            shift = shifts[index] if index < len(shifts) else 0
            loss = losses[index] if index < len(losses) else 0
            value = (pixel & mask) >> shift
            bits = max(1, 8 - loss)
            maximum = (1 << bits) - 1
            return (value * 255 + maximum // 2) // maximum

        rgba = bytearray(width * height * 4)
        for y in range(height):
            row = y * pitch
            out = y * width * 4
            for x in range(width):
                offset = row + x * bytesize
                pixel = int.from_bytes(raw[offset:offset + bytesize], "little")
                rgba[out:out + 4] = bytes((
                    channel(pixel, 0, 0),
                    channel(pixel, 1, 0),
                    channel(pixel, 2, 0),
                    channel(pixel, 3, 255),
                ))
                out += 4
        return width, height, bytes(rgba)

    def _publish(self, surface) -> None:
        if self.frame_path is None:
            return
        try:
            width, height, pixels = self._surface_rgba(surface)
            self.serial += 1
            payload = MAGIC + struct.pack("<IIQI", width, height, self.serial, len(pixels)) + pixels
            self.frame_path.parent.mkdir(parents=True, exist_ok=True)
            fd, temporary = tempfile.mkstemp(prefix=".aether-frame-", dir=str(self.frame_path.parent))
            try:
                with os.fdopen(fd, "wb") as stream:
                    stream.write(payload)
                    stream.flush()
                    os.fsync(stream.fileno())
                os.replace(temporary, self.frame_path)
            except BaseException:
                try:
                    os.unlink(temporary)
                except OSError:
                    pass
                raise
        except Exception as error:
            self._report("frame", error)
            return

    def _inject(self) -> None:
        if self.input_path is None or not self.input_path.exists():
            return
        try:
            import renpy.pygame as pygame
            with self.input_path.open("rb") as stream:
                stream.seek(self._input_offset)
                while True:
                    raw = stream.readline()
                    if not raw:
                        break
                    self._input_offset = stream.tell()
                    event = json.loads(raw.decode("utf-8"))
                    attrs = dict(event.get("attributes", {}))
                    event_type = int(event["type"])
                    # The engine ABI has no SDL scancode/repeat fields. Fill
                    # the Ren'Py pygame defaults so a native provider can send
                    # just key/modifier/unicode data.
                    if event_type in (pygame.KEYDOWN, pygame.KEYUP):
                        attrs.setdefault("mod", 0)
                        attrs.setdefault("unicode", "")
                        attrs.setdefault("scancode", 0)
                        attrs.setdefault("repeat", False)
                    pygame.event.post(pygame.event.Event(event_type, attrs))
        except Exception as error:
            self._report("input", error)
            return

    def _draw_screen(self, interface, original, *args, **kwargs):
        result = original(interface, *args, **kwargs)
        try:
            # Interface.draw_screen stores the actual rendered tree on itself.
            # Ren'Py exposes the active renderer as the ``draw`` attribute on
            # the ``renpy.display`` module.  It is not a ``renpy.display.draw``
            # importable submodule (8.5.3 raises ModuleNotFoundError here).
            import renpy.display
            renderer = getattr(renpy.display, "draw", None)
            if renderer is not None and interface.surftree is not None:
                self._publish(renderer.screenshot(interface.surftree))
        except Exception as error:
            self._report("draw", error)
            pass
        return result

    def install(self) -> None:
        import renpy.config
        import renpy.display.core

        klass = renpy.display.core.Interface
        original = klass.draw_screen
        if getattr(original, "_aetherkiri_overlay", False):
            return

        def draw_screen(interface, *args, **kwargs):
            return self._draw_screen(interface, original, *args, **kwargs)

        draw_screen._aetherkiri_overlay = True
        klass.draw_screen = draw_screen
        # PERIODIC events run while the interface waits, unlike a draw hook.
        renpy.config.periodic_callbacks.append(self._inject)


def install() -> None:
    if os.environ.get("AETHERKIRI_RENPY_OVERLAY") == "1":
        Overlay().install()

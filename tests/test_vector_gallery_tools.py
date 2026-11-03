"""Actual showcase SVG cards must not paint text or vectors into column gaps."""
from pathlib import Path
import os
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "out/build/x64-release/Release/pui_showcase_vulkan.exe"


def card_gaps_are_background(filename):
    data = Path(filename).read_bytes()
    if data[:2] != b"BM":
        raise AssertionError("Expected a BMP capture")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, signed_height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if bits not in (24, 32) or compression not in (0, 3):
        raise AssertionError("Expected uncompressed RGB/bitfield BMP")
    height = abs(signed_height)
    stride = ((width * bits + 31) // 32) * 4
    pixel_bytes = bits // 8

    def pixel(x, y):
        row = height - 1 - y if signed_height > 0 else y
        at = offset + row * stride + x * pixel_bytes
        return data[at:at + pixel_bytes]

    scan = int(height * .8)
    background = pixel(1, scan)
    spans = []
    start = None
    for x in range(width):
        painted = pixel(x, scan) != background
        if painted and start is None:
            start = x
        if not painted and start is not None:
            spans.append((start, x))
            start = None
    if start is not None:
        spans.append((start, width))
    if len(spans) != 3:
        raise AssertionError(f"Expected three isolated card surfaces, got {spans}")
    for left, right in zip(spans, spans[1:]):
        center = (left[0] + left[1]) // 2
        top = scan
        while top > 0 and pixel(center, top - 1) != background:
            top -= 1
        for x in range(left[1] + 2, right[0] - 2):
            for y in range(top + 2, scan + 1):
                if pixel(x, y) != background:
                    raise AssertionError(f"Card content spills into a column gap at {x},{y}")


class ShowcaseVectorGallery(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt" and EXE.exists(), "requires the built native showcase")
    def test_real_gallery_isolates_card_pixels_at_narrow_and_wide_sizes(self):
        generated = ROOT / "out/vector-gallery-checks"
        generated.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="run-", dir=generated) as scratch:
            self.assertTrue(Path(scratch).resolve().is_relative_to(generated.resolve()))
            for width in (520, 640, 960):
                with self.subTest(width=width):
                    capture = Path(scratch) / f"gallery-{width}.bmp"
                    result = subprocess.run([str(EXE), "--vectors", "--selftest", "--frames", "6",
                                             "--no-script", "--size", f"{width}x1024",
                                             "--screenshot", str(capture)], cwd=ROOT,
                                            capture_output=True, timeout=45)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(b"ok ", result.stdout)
                    card_gaps_are_background(capture)


if __name__ == "__main__":
    unittest.main()

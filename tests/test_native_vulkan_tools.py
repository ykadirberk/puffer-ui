"""The native branch ships four graphics stages and reproducible shader receipts."""
import hashlib
import json
from pathlib import Path
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeShaderContract(unittest.TestCase):
    def test_only_native_graphics_entry_points_are_embedded(self):
        header = (ROOT / "include/pufferui/impl/vulkan_shaders.inl").read_text()
        arrays = re.findall(r"vulkan_(\w+)\[\]\s*=\s*\{(.*?)\};", header, re.S)
        self.assertEqual({name for name, _ in arrays}, {"ui_vert", "ui_frag", "text_vert", "text_frag"})
        receipt = json.loads((ROOT / "src/shaders/vulkan/manifest.json").read_text())
        for name, body in arrays:
            words = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", body)]
            self.assertEqual(words[0], 0x07230203)
            entries = []
            cursor = 5
            while cursor < len(words):
                length, opcode = words[cursor] >> 16, words[cursor] & 0xFFFF
                self.assertGreater(length, 0)
                self.assertLessEqual(cursor + length, len(words))
                if opcode == 15:  # OpEntryPoint: 0=Vertex, 4=Fragment, 5=GLCompute
                    entries.append(words[cursor + 1])
                cursor += length
            self.assertEqual(entries, [0 if name.endswith("vert") else 4])
            source = name.replace("_", ".")
            self.assertEqual(hashlib.sha256(struct.pack("<" + "I" * len(words), *words)).hexdigest(),
                             receipt["shaders"][source]["spirv_sha256"])
            self.assertEqual(hashlib.sha256((ROOT / "src/shaders/vulkan" / source).read_bytes()).hexdigest(),
                             receipt["shaders"][source]["source_sha256"])
        slug = ROOT / "src/shaders/vulkan/harfbuzz_slug.glsl"
        self.assertEqual(hashlib.sha256(slug.read_bytes()).hexdigest(), receipt["includes"][slug.name])


if __name__ == "__main__":
    unittest.main()

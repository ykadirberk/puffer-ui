"""Independent font-free path stages keep reproducible source/SPIR-V receipts."""
import hashlib
import json
from pathlib import Path
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]


class VectorShaderContract(unittest.TestCase):
    def test_path_graphics_and_receipts_are_separate_from_existing_stages(self):
        header = (ROOT / "include/pufferui/impl/vulkan_vector_shaders.inl").read_text()
        arrays = re.findall(r"vulkan_(\w+)\[\]\s*=\s*\{(.*?)\};", header, re.S)
        self.assertEqual({name for name, _ in arrays}, {"path_vert", "path_frag"})
        receipt = json.loads((ROOT / "src/shaders/vulkan/path_manifest.json").read_text())
        self.assertEqual(set(receipt["shaders"]), {"path.vert", "path.frag"})
        for name, body in arrays:
            words = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", body)]
            self.assertEqual(words[0], 0x07230203)
            entries = []
            cursor = 5
            while cursor < len(words):
                length, opcode = words[cursor] >> 16, words[cursor] & 0xFFFF
                self.assertGreater(length, 0)
                self.assertLessEqual(cursor + length, len(words))
                if opcode == 15:
                    entries.append(words[cursor + 1])
                cursor += length
            self.assertEqual(entries, [0 if name.endswith("vert") else 4])
            source = name.replace("_", ".")
            data = struct.pack("<" + "I" * len(words), *words)
            self.assertEqual(hashlib.sha256(data).hexdigest(), receipt["shaders"][source]["spirv_sha256"])
            self.assertEqual(hashlib.sha256((ROOT / "src/shaders/vulkan" / source).read_bytes()).hexdigest(),
                             receipt["shaders"][source]["source_sha256"])
        slug = ROOT / "src/shaders/vulkan/harfbuzz_slug.glsl"
        self.assertEqual(hashlib.sha256(slug.read_bytes()).hexdigest(), receipt["includes"][slug.name])


if __name__ == "__main__":
    unittest.main()

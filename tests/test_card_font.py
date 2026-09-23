"""Check the generated bitmap/cmap inventory; board rendering is a separate gate."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CardFontTest(unittest.TestCase):
    def test_inventory_has_bitmaps_and_character_maps(self):
        source = (ROOT / "assets/fonts/card_font_18.c").read_text()
        inventory = json.loads((ROOT / "assets/fonts/card_font_18.json").read_text())
        expected = set(inventory["codepoints"])
        bitmap_entries = {int(cp, 16) for cp in re.findall(r'/\* U\+([0-9A-Fa-f]+) ', source)}
        self.assertEqual(expected, bitmap_entries)
        arrays = {}
        for name, values in re.findall(r'static const uint(?:8|16)_t (\w+)\[\] = \{(.*?)\};', source, re.S):
            arrays[name] = [int(value, 0) for value in re.findall(r'0x[0-9a-fA-F]+|\d+', values)]
        mapped = set()
        maps = source.split('static const lv_font_fmt_txt_cmap_t cmaps[] =', 1)[1].split('};', 1)[0]
        for block in re.findall(r'\{([^{}]+)\}', maps):
            start = int(re.search(r'\.range_start = (\d+)', block)[1])
            length = int(re.search(r'\.range_length = (\d+)', block)[1])
            if 'CMAP_SPARSE_TINY' in block:
                name = re.search(r'\.unicode_list = (\w+)', block)[1]
                mapped.update(start + offset for offset in arrays[name])
            elif 'CMAP_FORMAT0_FULL' in block:
                name = re.search(r'\.glyph_id_ofs_list = (\w+)', block)[1]
                mapped.update(start + offset for offset, glyph in enumerate(arrays[name]) if glyph or offset == 0)
            elif 'CMAP_FORMAT0_TINY' in block:
                mapped.update(range(start, start + length))
            else:
                self.fail('Unexpected cmap format; inspect the converter change')
        self.assertEqual(expected, mapped)
        self.assertTrue(set(map(ord, '名片钱包收款地址资产估值返回连接中文昵称')) <= mapped)
        self.assertNotIn(0x1F680, mapped)  # Deliberately unsupported emoji.
        self.assertEqual((inventory['size'], inventory['bpp']), (18, 2))
        ui = (ROOT / 'main/passport_ui.c').read_text() + (ROOT / 'main/main.c').read_text()
        for literal in re.findall(r'"(?:[^"\\]|\\.)*"', ui):
            for char in json.loads(literal):
                if char in '\r\n\t':
                    continue
                self.assertIn(ord(char), mapped, f'Missing UI codepoint U+{ord(char):04X}')


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
"""Generate the reproducible 18px GB2312 card font with lv_font_conv 1.5.3."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", default="lv_font_conv")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source_dir = root / "managed_components/lvgl__lvgl/scripts/built_in_font"
    source = source_dir / "SourceHanSansSC-Normal.otf"
    output = root / "assets/fonts"
    output.mkdir(parents=True, exist_ok=True)
    symbols = set(chr(i) for i in range(32, 127))
    symbols.add("\u00b7")
    for high in range(0xA1, 0xF8):
        for low in range(0xA1, 0xFF):
            try:
                symbols.add(bytes((high, low)).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    symbols = "".join(sorted(symbols))
    version = subprocess.check_output([args.converter, "--version"], text=True).strip()
    if version != "1.5.3":
        raise SystemExit("Expected lv_font_conv 1.5.3, received " + version)
    subprocess.run([
        args.converter, "--font", str(source), "--symbols", symbols,
        "--size", "18", "--bpp", "2", "--format", "lvgl", "--no-compress",
        "--no-kerning", "--lv-font-name", "card_font_18", "--lv-include", "lvgl.h",
        "--output", str(output / "card_font_18.c"),
    ], check=True)
    # Generated comments include the local executable/input paths. Replace only
    # those known paths so the committed artifact has no developer directories.
    generated = output / "card_font_18.c"
    text = generated.read_text()
    text = text.replace(str(root) + "/", "").replace(args.converter, "lv_font_conv")
    text = re.sub(r" \* Opts:.*", " * Reproduce: tools/generate_card_font.py; inventory: card_font_18.json", text)
    generated.write_text(text)
    inventory = {"converter": version, "size": 18, "bpp": 2,
                 "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                 "codepoints": [ord(c) for c in symbols]}
    (output / "card_font_18.json").write_text(json.dumps(inventory, indent=2) + "\n")
    shutil.copyfile(source_dir / "font_license/SourceHanSansSC/LICENSE.txt",
                    output / "SourceHanSansSC-LICENSE.txt")
    print(f"Generated {len(symbols)} requested glyphs; verify cmap coverage before use.")


if __name__ == "__main__":
    main()

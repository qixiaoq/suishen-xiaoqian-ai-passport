#!/usr/bin/env python3
"""Regenerate the 16px Source Han Serif subset used by the device UI."""

from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/fortune_app.c"
OTF = ROOT / "assets/fonts/XiaoqianSerifSubset.otf"
GLYPHS = ROOT / "assets/fonts/ui-glyphs.txt"
ALMANAC_GLYPHS = ROOT / "assets/fonts/almanac-glyphs.txt"
OUTPUT = ROOT / "main/fortune_font.c"
SMALL_OUTPUT = ROOT / "main/fortune_font_12.c"
TITLE_OUTPUT = ROOT / "main/fortune_title_font.c"
TITLE_SYMBOLS = "随身小签今日一签一念成卦六掷问事生辰小笺联网与校时六爻成卦扫码写下所问掌中签馆打开配网页暂离此卦淇见微缓行守正择机"


def main() -> None:
    converter = shutil.which("lv_font_conv")
    if not converter:
        raise SystemExit("Install lv_font_conv and put it on PATH first")
    symbols = "".join(sorted(
        {chr(code) for code in range(32, 127)}
        | {char for char in SOURCE.read_text(encoding="utf-8") if ord(char) >= 128 and not char.isspace()}
        | {char for char in ALMANAC_GLYPHS.read_text(encoding="utf-8") if ord(char) >= 128 and not char.isspace()}
    ))
    GLYPHS.write_text(symbols, encoding="utf-8")
    subprocess.run([
        converter, "--font", str(OTF.relative_to(ROOT)), "--size", "16", "--bpp", "4",
        "--format", "lvgl", "--symbols", symbols, "--no-kerning",
        "--lv-font-name", "fortune_font_16", "--lv-include", "lvgl.h",
        "--output", str(OUTPUT.relative_to(ROOT)),
    ], check=True, cwd=ROOT)
    subprocess.run([
        converter, "--font", str(OTF.relative_to(ROOT)), "--size", "12", "--bpp", "4",
        "--format", "lvgl", "--symbols", symbols, "--no-kerning",
        "--lv-font-name", "fortune_font_12", "--lv-include", "lvgl.h",
        "--output", str(SMALL_OUTPUT.relative_to(ROOT)),
    ], check=True, cwd=ROOT)
    subprocess.run([
        converter, "--font", str(OTF.relative_to(ROOT)), "--size", "24", "--bpp", "4",
        "--format", "lvgl", "--symbols", TITLE_SYMBOLS, "--no-kerning",
        "--lv-font-name", "fortune_title_font_24", "--lv-include", "lvgl.h",
        "--output", str(TITLE_OUTPUT.relative_to(ROOT)),
    ], check=True, cwd=ROOT)
    print(f"Generated 12/16px body fonts with {len(symbols)} glyphs and {TITLE_OUTPUT}")


if __name__ == "__main__":
    main()

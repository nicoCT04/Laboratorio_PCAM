#!/usr/bin/env python3
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONT_CANDIDATES = [
    "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-Regular.ttf",
    "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/System/Library/Fonts/Menlo.ttc",
]
FONT_SIZE = 22
LINE_GAP = 8
PADDING = 28
TITLE_BAR = 44
BACKGROUND = "#1e1f22"
TITLE_BACKGROUND = "#2b2d31"
TEXT = "#d7d7d2"
PROMPT = "#7fc87f"
COMMAND = "#ffffff"
HIGHLIGHT = "#f0c674"
DOTS = ["#ff5f57", "#febc2e", "#28c840"]
HIGHLIGHT_PREFIXES = (
    "Mode:", "N:", "Threads requested:", "Schedule:", "Elapsed:", "Checksum:",
    "Processors reported", "Max threads reported", "Max abs diff:", "Tolerance:",
)


def load_font():
    for candidate in FONT_CANDIDATES:
        if Path(candidate).exists():
            return ImageFont.truetype(candidate, FONT_SIZE)
    return ImageFont.load_default()


def line_color(line):
    if line.startswith("$ "):
        return None
    if line.startswith(HIGHLIGHT_PREFIXES):
        return HIGHLIGHT
    return TEXT


def render(text_path, png_path, title, prompt):
    lines = Path(text_path).read_text().rstrip("\n").expandtabs(4).splitlines()
    font = load_font()
    char_width = font.getlength("M")
    line_height = FONT_SIZE + LINE_GAP
    longest = max(len(prompt) + len(line) for line in lines)
    width = int(max(longest * char_width, 900) + PADDING * 2)
    height = TITLE_BAR + PADDING * 2 + line_height * len(lines)

    image = Image.new("RGB", (width, height), BACKGROUND)
    draw = ImageDraw.Draw(image)
    draw.rectangle([0, 0, width, TITLE_BAR], fill=TITLE_BACKGROUND)
    for k, color in enumerate(DOTS):
        cx = 24 + k * 24
        draw.ellipse([cx - 7, TITLE_BAR / 2 - 7, cx + 7, TITLE_BAR / 2 + 7], fill=color)
    title_width = font.getlength(title)
    draw.text(((width - title_width) / 2, (TITLE_BAR - FONT_SIZE) / 2 - 2), title, font=font,
              fill="#a9aaad")

    y = TITLE_BAR + PADDING
    for line in lines:
        color = line_color(line)
        if color is None:
            draw.text((PADDING, y), prompt, font=font, fill=PROMPT)
            draw.text((PADDING + font.getlength(prompt), y), line[2:], font=font, fill=COMMAND)
        else:
            draw.text((PADDING, y), line, font=font, fill=color)
        y += line_height
    image.save(png_path)


def main():
    if len(sys.argv) < 4:
        print(f"Uso: {sys.argv[0]} salida.txt captura.png \"titulo\" [prompt]", file=sys.stderr)
        return 2
    prompt = sys.argv[4] if len(sys.argv) > 4 else "$ "
    render(sys.argv[1], sys.argv[2], sys.argv[3], prompt)
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Render the OLED pages of examples/lilygo-lora32-oled.yaml with sample values.

The pages mirror the display lambda of the example. Usage:
    python render_oled_pages.py Roboto-Regular.ttf materialdesignicons-webfont.ttf output_dir
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

WIDTH, HEIGHT, SCALE = 128, 64, 4
SMALL, BIG = 13, 24

THERMOMETER = "\U000F050F"
HUMIDITY = "\U000F058E"
WIND = "\U000F059D"
COMPASS = "\U000F018C"
RAIN = "\U000F0596"
UV = "\U000F0F37"
LIGHT = "\U000F00DF"
RADIO = "\U000F043B"
WIFI = "\U000F05A9"
BATTERY = "\U000F0079"
CLOCK = "\U000F0150"
STATIONS = "\U000F0352"

# Page: title, status icon, status text, big value row or None, rows (y, icon, label, value).
PAGES = {
    "klima": ("Klima", CLOCK, "12s", (THERMOMETER, "-12.3 °C"), [(49, HUMIDITY, "Feuchte", "87 %")]),
    "wind": ("Wind", CLOCK, "12s", (WIND, "12.3 m/s"), [(49, COMPASS, "NW 315°", "Böe 15.1")]),
    "regen-licht": ("Regen & Licht", CLOCK, "59m", None, [
        (17, RAIN, "Regen", "1234.5 mm"), (33, UV, "UV-Index", "1.5"), (49, LIGHT, "Licht", "12500 lx")]),
    "umgebung": ("Umgebung", STATIONS, "3", None, [
        (17, THERMOMETER, "Temp.", "9.1 °C"), (33, HUMIDITY, "Feuchte", "84 %"), (49, WIND, "Wind", "2.7 m/s")]),
    "status": ("Status", CLOCK, "12s", None, [
        (17, RADIO, "Funk", "-71 dBm"), (33, WIFI, "WLAN", "-65 dBm"), (49, BATTERY, "Batterie", "OK")]),
}


def render(page, fonts):
    title, status_icon, status, big_row, rows = page
    small, big, icon_small, icon_big = fonts
    image = Image.new("1", (WIDTH, HEIGHT))
    draw = ImageDraw.Draw(image)
    draw.fontmode = "1"
    right = WIDTH - 1
    draw.text((0, 0), title, 1, small, anchor="la")
    draw.text((right, 0), status, 1, small, anchor="ra")
    draw.text((right - small.getlength(status) - 15, 0), status_icon, 1, icon_small, anchor="la")
    draw.line((0, 15, right, 15), 1)
    if big_row:
        draw.text((0, 18), big_row[0], 1, icon_big, anchor="la")
        draw.text((right, 16), big_row[1], 1, big, anchor="ra")
    for y, icon, label, value in rows:
        draw.text((0, y), icon, 1, icon_small, anchor="la")
        draw.text((17, y), label, 1, small, anchor="la")
        draw.text((right, y), value, 1, small, anchor="ra")
    return image.resize((WIDTH * SCALE, HEIGHT * SCALE), Image.NEAREST)


def main():
    text_font, icon_font, output = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    fonts = (
        ImageFont.truetype(text_font, SMALL),
        ImageFont.truetype(text_font, BIG),
        ImageFont.truetype(icon_font, 14),
        ImageFont.truetype(icon_font, BIG),
    )
    output.mkdir(parents=True, exist_ok=True)
    for name, page in PAGES.items():
        render(page, fonts).save(output / f"{name}.png")


if __name__ == "__main__":
    main()

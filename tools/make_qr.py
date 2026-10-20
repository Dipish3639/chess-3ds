#!/usr/bin/env python3
"""Make FBI "Remote Install -> Scan QR code" images for the release CIA.

Usage: python3 tools/make_qr.py <branch> <output.png>
Requires: pip install qrcode pillow
"""
import sys

import qrcode
from PIL import Image, ImageDraw, ImageFont

REPO = "Dipish3639/chess-3ds"
CIA_PATH = "release/Chess3DS.cia"


def main():
    branch = sys.argv[1] if len(sys.argv) > 1 else "main"
    out = sys.argv[2] if len(sys.argv) > 2 else "release/qr-code.png"
    url = f"https://raw.githubusercontent.com/{REPO}/{branch}/{CIA_PATH}"

    qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M, box_size=8, border=4)
    qr.add_data(url)
    qr.make(fit=True)
    code = qr.make_image(fill_color="black", back_color="white").convert("RGB")

    # Add a caption so people know what they're scanning
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
    small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13)
    w, h = code.size
    img = Image.new("RGB", (w, h + 60), "white")
    img.paste(code, (0, 0))
    d = ImageDraw.Draw(img)
    d.text((w / 2, h + 4), "Chess 3DS - scan with FBI", font=font, fill="black", anchor="ma")
    d.text((w / 2, h + 34), "FBI > Remote Install > Scan QR Code", font=small, fill="#444", anchor="ma")
    img.save(out)
    print(url)


if __name__ == "__main__":
    main()

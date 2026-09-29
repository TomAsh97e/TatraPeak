#!/usr/bin/env python3
"""Render deterministic TatraPeak replay result JSON files as a small animated GIF."""

import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw, ImageFont


COLORS = {
    "VisibleInModel": "#35c759",
    "OccludedInModel": "#ff453a",
    "Uncertain": "#ffd60a",
    "MissingTerrain": "#ff9f0a",
}


def load_font(size):
    candidate = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
    return ImageFont.truetype(str(candidate), size) if candidate.exists() else ImageFont.load_default()


def frame_size(frame_directory):
    root = ET.parse(frame_directory / "overlay.svg").getroot()
    return int(root.attrib["width"]), int(root.attrib["height"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("replay", type=Path, help="directory written by 'tatra replay'")
    parser.add_argument("output", type=Path)
    parser.add_argument("--scale", type=float, default=0.5)
    parser.add_argument("--frame-ms", type=int, default=700)
    args = parser.parse_args()
    if not 0 < args.scale <= 1 or args.frame_ms <= 0:
        parser.error("scale must be in (0,1] and frame-ms must be positive")

    report = json.loads((args.replay / "report.json").read_text())
    records = report["frames"]
    if not records or any(record["status"] != "completed" for record in records):
        raise ValueError("the demo requires a non-empty replay with completed frames only")

    font = load_font(15)
    small_font = load_font(12)
    images = []
    for record in records:
        directory = args.replay / Path(record["result_path"]).parent
        source_width, source_height = frame_size(directory)
        width, height = round(source_width * args.scale), round(source_height * args.scale)
        result = json.loads((args.replay / record["result_path"]).read_text())
        image = Image.new("RGB", (width, height), "#18212b")
        draw = ImageDraw.Draw(image)
        draw.line((0, height * 0.62, width, height * 0.62), fill="#465666", width=2)
        timestamp_s = record["timestamp_ns"] / 1_000_000_000
        draw.text((14, 12), f"TatraPeak replay   t={timestamp_s:.1f} s", font=font, fill="white")

        visible_count = 0
        for observation in result["observations"]:
            projection = observation["projection"]
            pixel = projection["pixel"]
            if projection["state"] != "InFrame" or pixel is None:
                continue
            x, y = pixel["u"] * args.scale, pixel["v"] * args.scale
            visibility = observation["visibility"]
            state = visibility["state"] if visibility else "NotEvaluated"
            color = COLORS.get(state, "#8e8e93")
            draw.ellipse((x - 4, y - 4, x + 4, y + 4), fill=color, outline="#111111", width=1)
            draw.text((x + 7, y - 13), observation["name"], font=small_font, fill="white",
                      stroke_width=2, stroke_fill="#111111")
            visible_count += 1
        draw.text((14, height - 28), f"markers in frame: {visible_count}", font=small_font,
                  fill="#d5dbe1")
        images.append(image)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    images[0].save(args.output, save_all=True, append_images=images[1:],
                   duration=args.frame_ms, loop=0, disposal=2, optimize=False)
    print(f"Written {args.output} ({len(images)} frames)")


if __name__ == "__main__":
    main()

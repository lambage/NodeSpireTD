"""Extract individual talent icons from the generated archer contact sheet."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


ICON_NAMES = (
    "archer_quickdraw_rig",
    "archer_hardened_draw",
    "archer_stone_specialization",
    "archer_metal_specialization",
    "archer_electric_specialization",
    "archer_stone_shatter",
    "archer_stone_penetrator_1",
    "archer_stone_penetrator_2",
    "archer_metal_overdraw",
    "archer_metal_serrated",
    "archer_electric_chain",
    "archer_electric_focus",
    "archer_metal_triple_shot",
    "archer_metal_precision",
    "archer_electric_ricochet",
    "archer_electric_static_field",
    "archer_stone_metal_hybrid",
    "archer_stone_aftershock",
)

# Pixel geometry measured from the 1024x576 generated sheet. Coordinates are
# scaled for copies of the same image saved at a different resolution.
REFERENCE_SIZE = (1024, 576)
LEFT = 12
TOP = 9
ICON_SIZE = 156
COLUMN_PITCH = 169
ROW_PITCH = 191
COLUMNS = 6
EDGE_INSET = 2


def extract_icons(sheet_path: Path, output_dir: Path, overwrite: bool) -> None:
    with Image.open(sheet_path) as sheet:
        sheet = sheet.convert("RGBA")
        scale_x = sheet.width / REFERENCE_SIZE[0]
        scale_y = sheet.height / REFERENCE_SIZE[1]
        icon_size = round(ICON_SIZE * scale_y)
        inset = max(1, round(EDGE_INSET * scale_y))
        output_dir.mkdir(parents=True, exist_ok=True)

        for index, name in enumerate(ICON_NAMES):
            row, column = divmod(index, COLUMNS)
            left = round((LEFT + column * COLUMN_PITCH) * scale_x) + inset
            top = round((TOP + row * ROW_PITCH) * scale_y) + inset
            right = left + icon_size - 2 * inset
            bottom = top + icon_size - 2 * inset
            destination = output_dir / f"{name}.png"

            if destination.exists() and not overwrite:
                print(f"skip  {destination} (already exists)")
                continue

            sheet.crop((left, top, right, bottom)).save(destination)
            print(f"saved {destination}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract the 18 icons from the 6x3 archer contact sheet."
    )
    parser.add_argument("sheet", type=Path, help="Path to the contact-sheet image")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("assets/images/talents/archer/extracted"),
        help="Destination directory (default: %(default)s)",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Replace PNG files that already exist",
    )
    return parser.parse_args()


if __name__ == "__main__":
    arguments = parse_args()
    extract_icons(arguments.sheet, arguments.output_dir, arguments.overwrite)
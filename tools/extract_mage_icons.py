"""Extract individual talent icons from the generated mage contact sheet."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


ICON_NAMES = (
    "mage_resonant_attunement",
    "mage_focused_casting",
    "mage_fire_well",
    "mage_ice_well",
    "mage_arcane_well",
    "mage_ember_cascade",
    "mage_cinder_wick",
    "mage_frostbite",
    "mage_glacial_lance",
    "mage_volatile_matrix",
    "mage_overweave",
    "mage_thermal_shock",
    "mage_wildfire_surge",
    "mage_frozen_precision",
    "mage_inferno",
    "mage_absolute_zero",
    "mage_archons_focus",
    "mage_primordial_convergence",
)

# Pixel geometry measured from the 1024x559 generated sheet. Coordinates are
# scaled for copies of the same image saved at a different resolution.
REFERENCE_SIZE = (1024, 559)
LEFT = 14
TOP = 9
ICON_SIZE = 145
COLUMN_PITCH = 170
ROW_PITCH = 185
COLUMNS = 6


def extract_icons(sheet_path: Path, output_dir: Path, overwrite: bool) -> None:
    with Image.open(sheet_path) as source:
        sheet = source.convert("RGBA")
        scale_x = sheet.width / REFERENCE_SIZE[0]
        scale_y = sheet.height / REFERENCE_SIZE[1]
        output_dir.mkdir(parents=True, exist_ok=True)

        for index, name in enumerate(ICON_NAMES):
            row, column = divmod(index, COLUMNS)
            left = round((LEFT + column * COLUMN_PITCH) * scale_x)
            top = round((TOP + row * ROW_PITCH) * scale_y)
            right = round((LEFT + column * COLUMN_PITCH + ICON_SIZE) * scale_x)
            bottom = round((TOP + row * ROW_PITCH + ICON_SIZE) * scale_y)
            destination = output_dir / f"{name}.png"

            if destination.exists() and not overwrite:
                print(f"skip  {destination} (already exists)")
                continue

            sheet.crop((left, top, right, bottom)).save(destination)
            print(f"saved {destination}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract the 18 icons from the 6x3 mage contact sheet."
    )
    parser.add_argument("sheet", type=Path, help="Path to the contact-sheet image")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("assets/images/talents/mage"),
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
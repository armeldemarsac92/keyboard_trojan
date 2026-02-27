#!/usr/bin/env python3
import argparse
from pathlib import Path

CHUNK_SIZE = 16
LINE_TAG = "BYTE_LINE"

DEFAULT_INPUT_FILE = Path("output/build/time_tracker.exe")
DEFAULT_OUTPUT_FILE = Path("output/artifacts/nice_text.txt")


def main() -> int:
    parser = argparse.ArgumentParser(description="Export a binary file as tagged hex lines.")
    parser.add_argument("input_file", nargs="?", default=str(DEFAULT_INPUT_FILE))
    parser.add_argument("output_file", nargs="?", default=str(DEFAULT_OUTPUT_FILE))
    args = parser.parse_args()

    input_path = Path(args.input_file)
    output_path = Path(args.output_file)

    if not input_path.exists():
        raise FileNotFoundError(f"Input file not found: {input_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    binary_data = input_path.read_bytes()

    with output_path.open("w", encoding="utf-8") as out:
        for offset in range(0, len(binary_data), CHUNK_SIZE):
            chunk = binary_data[offset : offset + CHUNK_SIZE]
            formatted_chunk = " ".join(f"{byte:02X}" for byte in chunk)
            out.write(f"{LINE_TAG}: {formatted_chunk}\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

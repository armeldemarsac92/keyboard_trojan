INPUT_FILE = "time_tracker.exe"
OUTPUT_FILE = "nice_text.txt"
CHUNK_SIZE = 16
LINE_TAG = "BYTE_LINE"


with open(INPUT_FILE, "rb") as f:
    binary_data = f.read()

with open(OUTPUT_FILE, "w", encoding="utf-8") as out:
    for offset in range(0, len(binary_data), CHUNK_SIZE):
        chunk = binary_data[offset : offset + CHUNK_SIZE]
        formatted_chunk = " ".join(f"{byte:02X}" for byte in chunk)
        out.write(f"{LINE_TAG}: {formatted_chunk}\n")

#!/usr/bin/env python3
import sys

def convert_to_c_array(file_path):
    try:
        with open(file_path, "rb") as f:
            byte_array = f.read()

        c_array = "const uint8_t bindata[] PROGMEM = {"
        for i, byte in enumerate(byte_array):
            if i % 16 == 0:
                c_array += "\n  "
            c_array += f"0x{byte:02x}, "
        c_array = c_array.rstrip(", ") + "\n};\n"

        print(f"// file name : {file_path}")
        print(f"// file size : {len(byte_array)} bytes\n")
        print(c_array)
    except FileNotFoundError:
        print(f"File not found: {file_path}")
    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py <path_to_binary_file>")
    else:
        convert_to_c_array(sys.argv[1])

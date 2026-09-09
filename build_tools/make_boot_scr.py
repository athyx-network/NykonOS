import sys
import struct
import zlib
import time

def make_uboot_script(input_path, out_path):
    with open(input_path, "rb") as f:
        body = f.read()
    data_size = len(body)
    data_crc = zlib.crc32(body) & 0xffffffff
    t = int(time.time())
    name = b'NykonOS Boot'
    name = name.ljust(32, b'\x00')
    header_no_crc = struct.pack('>IIIIIIIBBBB32s', 0x27051956, 0, t, data_size, 0, 0, data_crc, 6, 2, 6, 0, name)
    header_crc = zlib.crc32(header_no_crc) & 0xffffffff
    header = struct.pack('>IIIIIIIBBBB32s', 0x27051956, header_crc, t, data_size, 0, 0, data_crc, 6, 2, 6, 0, name)
    with open(out_path, 'wb') as f:
        f.write(header + body)
    print(f"Generated U-Boot script: {out_path}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python make_boot_scr.py <boot.cmd> <boot.scr>")
        sys.exit(1)
    make_uboot_script(sys.argv[1], sys.argv[2])

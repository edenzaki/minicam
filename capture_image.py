#!/usr/bin/env python3

# this script depends on pyserial; install with:
#    pip install pyserial

try:
    import serial
    print("pyserial is installed, proceeding...")
except ImportError:
    print("Error: pyserial is not installed. Run 'pip install pyserial' and try again.")
    sys.exit(1)

import base64, os, sys

# usage: python capture_image.py [COM_PORT] [DEST_DIR]
# default COM_PORT is COM3, default DEST_DIR is a folder named "esp_images" on the user's desktop

port = sys.argv[1] if len(sys.argv) > 1 else 'COM3'
dest_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.expanduser('~'), 'Documents', '2026', 'ESP32_CAM', 'minicam', 'esp_images')

os.makedirs(dest_dir, exist_ok=True)
out_path = os.path.join(dest_dir, 'capture.jpg')

print(f"Opening {port}, saving to {out_path}")
ser = serial.Serial(port, 115200, timeout=10)

buffer = b''
while True:
    line = ser.readline().decode(errors='ignore').strip()
    if line == 'BEGIN_IMAGE':
        data = ser.readline().strip()
        buffer = base64.b64decode(data)
        break

with open(out_path, 'wb') as f:
    f.write(buffer)

print("Saved", out_path)

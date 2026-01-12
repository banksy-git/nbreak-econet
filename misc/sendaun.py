import socket
import struct

SOURCE_PORT = 32769
DEST_IP = "10.222.10.141"
DEST_PORT = 32768
MESSAGE = struct.pack("<BBBBI", 2, 0x1, 0x80, 0, 1)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Bind to the desired source port
sock.bind(("", SOURCE_PORT))  # "" means all local interfaces

sock.sendto(MESSAGE, (DEST_IP, DEST_PORT))

sock.close()

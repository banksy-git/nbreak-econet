import socket
import struct
from dataclasses import dataclass

AUN_TYPE_BROADCAST = 0x01
AUN_TYPE_DATA = 0x02
AUN_TYPE_ACK = 0x03
AUN_TYPE_NACK = 0x04
AUN_TYPE_IMM = 0x05
AUN_TYPE_IMM_REPLY = 0x06

ECONET_CTRL_PEEK = 0x81         # Scout->, <-Data
ECONET_CTRL_POKE = 0x82         # Scout->, <-Ack, Data->, <-Ack
ECONET_CTRL_JSR = 0x83          # Scout->, <-Ack, Data->, <-Ack
ECONET_CTRL_USERPROC = 0x84     # Scout->, <-Ack, Data->, <-Ack
ECONET_CTRL_OSPROC = 0x85       # Scout->, <-Ack, Data->, <-Ack
ECONET_CTRL_HALT = 0x86         # Scout->, <-Ack
ECONET_CTRL_CONTINUE = 0x87     # Scout->, <-Ack
ECONET_CTRL_MACHINETYPE = 0x88  # Scout->, <-Data
ECONET_CTRL_GETREGISTERS = 0x89 # Scout->, <-Data

@dataclass
class AunHeader:
    transaction_type: int
    econet_port: int
    econet_control: int
    sequence: int

    def encode(self) -> bytes:
        return struct.pack("<BBBBI", self.transaction_type, self.econet_port,
                           self.econet_control, 0, self.sequence)

    @classmethod
    def decode(cls, data: bytes) -> "AunHeader":
        transaction_type, econet_port, econet_control, _, sequence = struct.unpack("<BBBBI", data[:8])
        return cls(transaction_type, econet_port, econet_control, sequence)


SOURCE_PORT = 32769
DEST_IP = "10.222.10.143"
DEST_PORT = 32768

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("", SOURCE_PORT))


addr = 0x7C00
end_addr = addr + 16
#sock.sendto(AunHeader(AUN_TYPE_IMM, 0, ECONET_CTRL_PEEK, 4).encode()+struct.pack("<II", addr, end_addr), (DEST_IP, DEST_PORT))
#sock.sendto(AunHeader(AUN_TYPE_IMM, 0, ECONET_CTRL_HALT, 8).encode(), (DEST_IP, DEST_PORT))
#sock.sendto(AunHeader(AUN_TYPE_IMM, 0, ECONET_CTRL_CONTINUE, 12).encode(), (DEST_IP, DEST_PORT))
sock.sendto(AunHeader(AUN_TYPE_IMM, 0, ECONET_CTRL_MACHINETYPE, 12).encode(), (DEST_IP, DEST_PORT))

reply = sock.recv(32768)
hdr = AunHeader.decode(reply)
print(hdr, reply[8:])

sock.close()

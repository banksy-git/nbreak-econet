##
## Econet Sigrok Decoder
##
## Copyright (C) 2025 Paul G. Banks <https://paulbanks.org/projects/econet/>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, see <http://www.gnu.org/licenses/>.
##

import sigrokdecode as srd

def crc16_x25(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    return crc ^ 0xFFFF

class ChannelError(Exception):
    pass

class Decoder(srd.Decoder):
    api_version = 3
    id = 'econet'
    name = 'Econet'
    longname = 'Acorn Econet'
    desc = 'Full-duplex, synchronous, serial network bus.'
    license = 'gplv3+'
    inputs = ['logic']
    outputs = ['spi']
    tags = ['Retro']
    channels = (
        {'id': 'clk', 'name': 'CLK', 'desc': 'Clock'},
        {'id': 'data', 'name': 'DATA', 'desc': 'Data'},
    )
    optional_channels = (
        {'id': 'data2', 'name': 'DATA2', 'desc': 'Driven data'},
        {'id': 'data2en', 'name': 'DATA2EN', 'desc': 'Driven data enable'},
    )
    annotations = (
        ('decode', 'decode'),
        ('flag', 'Flag'),
        ('frame', 'Frame'),
        ('abort', 'Abort'),
        ('idle', 'Idle'),

    )
    annotation_rows = (
        ('data', 'Data', (0,)),
        ('state', 'Decoder state', (1, 2, 3, 4)),
    )
    binary = (
        ('data', 'DATA'),
        ('data2', 'DATA2'),
        ('data2en', 'DATA2EN'),

    )

    def __init__(self):
        self.reset()

    def reset(self):
        self.is_reset = 1

    def start(self):
        self.out_python = self.register(srd.OUTPUT_PYTHON)
        self.out_ann = self.register(srd.OUTPUT_ANN)
        self.out_binary = self.register(srd.OUTPUT_BINARY)
 

    def reset_decoder_state(self):
        self.last_state_at = 0
        self.last_state = "UNDEFINED"
        self.bitcount = 0
        self.shiftreg = 0
        self._recv_data_bit = 0
        self.rx_frame_len = 0
        self.rx_crc = 0xFFFF
        self.is_frame_active = 0
        self._recv_data_shift_in = 0
        self.idle_counter = 0

    def _begin_frame(self):
        self.frame_start_at = self.samplenum
        self._recv_data_bit = 0
        self.rx_crc = 0xFFFF
        self.is_frame_active = 1
        self._recv_data_shift_in = 0
        self.frame_bytes = []
        self.bit_start = 0

    def _complete_frame(self):

        if len(self.frame_bytes)<6:
            self.put(self.frame_start_at, self.edge_at(-7), self.out_ann, [2, [ f"Invalid short frame"]])
            return
        
        crc_valid = "CRC:OK" if crc16_x25(self.frame_bytes[0:-2])==self.frame_bytes[-2] | (self.frame_bytes[-1] <<8) else "CRC:FAIL"


        self.put(self.frame_start_at, self.edge_at(-7), self.out_ann, [2, [ f"Frame {len(self.frame_bytes)} bytes {crc_valid}"]])
        self.is_frame_active = 0

    def handle_bit(self, bit):

        bit = bit & 1

        if bit:
            if self.idle_counter<15:
                self.idle_counter+=1
                if self.idle_counter==15:
                    self.put(self.edge_at(-14), self.samplenum, self.out_ann, [4, [ f"IDLE"]])
        else:
            self.idle_counter = 0

        self.shiftreg = ((self.shiftreg << 1) | bit) & 0xff

        if self.shiftreg==0x7e:

            self.put(self.edge_at(-7), self.edge_at(0), self.out_ann, [1, ["FLAG"]])

            if not self.is_frame_active:
                self._begin_frame()
            else:
                if len(self.frame_bytes)>1:
                    self._complete_frame()
                else:
                    self._begin_frame()
            return
        
        if not self.is_frame_active:
            return
        
        # Abort
        if self.shiftreg==0x7f:
            self.put(self.edge_at(-7), self.edge_at(0), self.out_ann, [3, ["ABORT"]])
            self.is_frame_active = 0
            return
        
        # Bit stuffing
        if self.shiftreg & 0x3f==0x3e:
            return
        
        # Data handling
        if self._recv_data_bit==0:
            self.bit_start = self.samplenum
        self._recv_data_shift_in = (self._recv_data_shift_in >> 1) | (bit << 7) # LSB first
        self._recv_data_shift_in = self._recv_data_shift_in & 0xFF
        self._recv_data_bit += 1
        if self._recv_data_bit==8:
            self._recv_data_bit = 0
            self.frame_bytes.append(self._recv_data_shift_in)
            self.put(self.bit_start, self.samplenum, self.out_ann, [0, [f"{self._recv_data_shift_in:02x}"]])


    def edge_at(self, edge_no):
        return self.edges[-1+edge_no]

    def decode(self):

        self.reset_decoder_state()
        self.edges = [0]*16

        wait_cond = [{0: 'r'}]

        while True:
            _, data, data2, data2en = self.wait(wait_cond)
            if data2en==1:
                data = data2

            # Keep record of edge positions
            self.edges.append(self.samplenum)
            if len(self.edges)>16:
                self.edges.pop(0)

            self.handle_bit(data)

            

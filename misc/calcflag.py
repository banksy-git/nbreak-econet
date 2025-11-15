
FLAG=0x7e

bits = ""
c = FLAG
for _ in range(8):
    bits += "1"
    bits += "1" if c & 1 else "0"
    bits += "0"
    bits += "0"
    c>>=1

for i in range(0, len(bits), 8):
    print(hex(int(bits[i:i+8], 2)))


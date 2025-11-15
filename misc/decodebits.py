enbits = ""
databits = ""


# for by in bytes.fromhex("bffeffefaaaaaffefaaaabbfebaffbffb80000"):
#     for _ in range(4):
#         enbits += '1' if by&0x80 else '0'
#         databits += '1' if by&0x40 else '0'
#         by<<=2

for by in bytes.fromhex("bbffffeeffffeeffaaaaaaaaaaffffeeffaaaaaaaabbbbffeebbaaffffbbffffee00000000"):
    for _ in range(2):
        enbits += '1' if by&0x80 else '0'
        databits += '1' if by&0x40 else '0'
        by<<=2

print(enbits)
print(databits)
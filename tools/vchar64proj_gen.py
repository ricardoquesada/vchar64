#!/usr/bin/python3

import getopt
import os
import struct
import sys

def run(charsetfile, mapfile, map_w, map_h, filename):
    with open(filename, 'wb') as fd, open(charsetfile,'rb') as rc, open(mapfile, 'rb') as rm:
        fd.write(bytes("VChar","utf-8"))  # byte 5: id
        fd.write(b'\x03')                   # byte: version
        fd.write(bytes([9,0,15,13]))        # byte 4: colors
        fd.write(b'\x03')                   # byte: vic res: 1 == multi color
        fd.write(bytes([0,1]))      # word: num chars
        fd.write(b'\x01')           # byte: tile width
        fd.write(b'\x01')           # byte: tile height
        fd.write(b'\x01')           # byte: char interleaved
        fd.write(b'\x00')           # byte: color mode:  0 == global
        fd.write(bytes([map_w&0xff,map_w>>8]))      # word: map width
        fd.write(bytes([map_h&0xff,map_h>>8]))      # word: map height
        fd.write(bytes([0,0x40]))   # word: charset export address 
        fd.write(bytes([0,0x80]))   # word: map export address
        fd.write(bytes([0,0]))      # word: address attributes
        fd.write(b'\x00')           # byte: what features should be exported
        fd.write(b'\x00')           # byte: export format: 0 == raw
        fd.write(bytes(3))          # byte 3: reserved
        fd.write(rc.read())         # byte 2048: charset
        fd.write(bytes(256))        # byte 256: color
        fd.write(rm.read())         # variable


def help():
    print("%s v0.1 - Utility to generate VChar64 projects from a charset and map\n" % os.path.basename(sys.argv[0]))
    print("Example:\n%s -c charset.bin -m map.bin -w 40 -h 25 myproj.vchar64proj" % os.path.basename(sys.argv[0]))
    sys.exit(-1)


if __name__ == "__main__":
    if len(sys.argv) == 1:
        help()

    charsetfile = None
    mapfile = None
    map_w = 0
    map_h = 0

    argv = sys.argv[1:]
    try:
        opts, args = getopt.getopt(argv, "c:m:h:w:", ["charset=", "map=", "map_h=", "map_w="])
        for opt, arg in opts:
            if opt in ("-c", "--charset"):
                charsetfile = arg
            elif opt in ("-m", "--map"):
                mapfile = arg
            elif opt in ("-h", "--map_height"):
                map_h= int(arg)
            elif opt in ("-w", "--map_width"):
                map_w = int(arg)
        if not len(args) == 1:
            help()
    except getopt.GetoptError as err:
        print(err)

    run(charsetfile, mapfile, map_w, map_h, args[0])
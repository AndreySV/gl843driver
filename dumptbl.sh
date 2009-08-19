#!/bin/sh

# Written by Andreas Robinson in 2009. This software is in the public domain.

if [ $# != 3 ]; then
    echo "\n`basename "$0"` - "
    echo "Print a 16-bit motor acceleration or gamma table as a list of decimal numbers."

    echo "Usage: `basename $0` <start> <len> <file>"
    echo "  start: start offset within the file (can be hex with a 0x prefix)"
    echo "  len:   number of bytes to read"
    echo "  file:  binary usb dump"
    exit
fi

hexdump -s $1 -n $2 -e '1/2 "%d\n"' -v $3

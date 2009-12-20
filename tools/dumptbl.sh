#!/bin/sh

# Written by Andreas Robinson in 2009. This software is in the public domain.

if [ $# != 3 ]; then
    echo "\n`basename "$0"` - "
    echo "Print a 16-bit motor acceleration or gamma table as a series of decimal numbers."

    echo "Usage: `basename $0` <file> <start> <len>"
    echo "  file:  binary usb dump"
    echo "  start: start offset within the file (can be hex with a 0x prefix)"
    echo "  len:   number of bytes to read (decimal only)"
    exit
fi

hexdump -s $2 -n $3 -e '1/2 "%u "' -v $1
echo

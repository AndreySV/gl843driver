#!/bin/sh

# Dump all motor tables in $LOGFILE, one table per line.
# The first column is A$dst, where $dst is the destination address
# in the GL843 motor SRAM

LOGFILE="all.log"

DUMP=`./parsedump.pl ${LOGFILE} | grep "MTRTBL = 1" -A 3`
DSTS=`echo "${DUMP}" | grep "wr_r 5c" | cut -d ' ' -f 9 | sed s/,//g`
LENS=`echo "${DUMP}" | grep "wr_b" | cut -d ' ' -f 4`
BUFS=`echo "${DUMP}" | grep "wr_b" | cut -d ' ' -f 7`

TFP="/tmp/.tmp$$"	# Temp-file prefix

echo "${DSTS}" > ${TFP}_dsts
echo "${LENS}" > ${TFP}_lens
echo "${BUFS}" > ${TFP}_bufs

paste -d '   ' ${TFP}_dsts ${TFP}_lens ${TFP}_bufs > ${TFP}_tables

cat ${TFP}_tables | while read t
do
	dst=`echo $t | cut -d ' ' -f 1`
	len=`echo $t | cut -d ' ' -f 2`
	buf=`echo $t | cut -d ' ' -f 3`
	tbl=`./dumptbl.sh ${LOGFILE} ${buf} ${len}`
	echo A$dst "$tbl"
done;

rm ${TFP}*

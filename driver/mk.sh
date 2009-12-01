#!/bin/sh
gcc low.c util.c scan.c cs4400f.c test.c -lusb-1.0 -lm -Wall

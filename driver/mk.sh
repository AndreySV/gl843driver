#!/bin/sh
gcc low.c util.c image.c scan.c motor.c cs4400f.c test.c -lusb-1.0 -lm

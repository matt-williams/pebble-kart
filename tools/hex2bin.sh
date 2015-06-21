#!/bin/sh
xxd -r -p < $1 > resources/$(basename $1 .hex).dat

#!/bin/sh
{ echo 'convert(' ; cat $1.json ; echo ');' ; } > $(basename $1 .json).js

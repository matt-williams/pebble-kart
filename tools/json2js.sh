#!/bin/sh
{ echo 'convert(' ; cat $1 ; echo ');' ; } > $(basename $1 .json).js

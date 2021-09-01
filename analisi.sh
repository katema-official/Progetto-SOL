#!/bin/bash

if [ -f "file_di_log.txt" ]; then
	cat ./file_di_log.txt
else
	echo "$0: Errore nella lettura del file di log finale" 1>&2
fi

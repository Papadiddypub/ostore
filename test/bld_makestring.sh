#!/bin/bash

gcc -Wall -I ../inc ../src/ostore_internal.c ../src/ostore.c ./makestring.c -o test_makestring
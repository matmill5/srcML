#!/bin/bash

# test framework
source $(dirname "$0")/framework_test.sh

exit 1

# test
srcml_old = f.read()
f.close()

f = open('diff/b.cpp' 'r')
src_new = f.read()
f.close()

f = open('diff/b.cpp.xml' 'r')

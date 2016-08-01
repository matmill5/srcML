#!/bin/bash

# test framework
source $(dirname "$0")/framework_test.sh

set +e

# test
define xpath_error <<- 'STDOUT'
	Error Parsing: Start tag expected, '<' not found
	STDOUT

define xpath_empty <<- 'STDOUT'
	<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
	<unit xmlns="http://www.srcML.org/srcML/src" revision="REVISION"/>
	STDOUT

define output <<- 'STDOUT'
	<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
	<unit xmlns="http://www.srcML.org/srcML/src" revision="REVISION"/>
	STDOUT

xmlcheck "$xpath_empty"
xmlcheck "$output"
createfile sub/a.cpp.xml ""


srcml2src --xpath=src:unit sub/a.cpp.xml
check_exit 1

srcml2src -l C++ --xpath=src:unit sub/a.cpp.xml -o sub/b.cpp.xml
check_exit 1

srcml2src -l C++ --xpath=src:unit -o sub/b.cpp.xml sub/a.cpp.xml
check_exit 1

# TODO: issue #973
srcml2src -l C++ --xpath=src:unit < sub/a.cpp.xml
check_exit 1

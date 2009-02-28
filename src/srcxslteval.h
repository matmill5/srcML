/*
  xpathproc

  A simple XPath expression evaluator.

  Usage:  xpathproc xpath-expression xml-file
*/

#ifndef INCLUDED_SRCXSLTEVAL_H
#define INCLUDED_SRCXSLTEVAL_H

#include <libxml/xmlreader.h>

int srcxslteval(const char* xpath, xmlTextReaderPtr reader, const char* ofilename, const char* params[], int paramcount);

#endif

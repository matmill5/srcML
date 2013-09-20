/*
  UnitDOM.cpp

  Copyright (C) 2011  SDML (www.srcML.org)

  This file is part of the srcML Toolkit.

  The srcML Toolkit is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  The srcML Toolkit is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the srcML Toolkit; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef INCLUDED_UNITDOM_HPP
#define INCLUDED_UNITDOM_HPP

#include <libxml/SAX2.h>

#include "ProcessUnit.hpp"

class UnitDOM : public ProcessUnit {
public :

    UnitDOM(int options) : rootsize(0), found(false), options(options), error(false) {}

    virtual ~UnitDOM() {}

    virtual int getOptions() const { return options; }

    /*
      Called exactly once at beginnning of document  Override for intended behavior.
    */
    virtual void startOutput(void* ctx) = 0;

    /*
      Called exactly once for each unit.  For an archive, not called on the root unit

      Formed unit combines namespaces from root and individual unit.  Full DOM of
      individual unit is provided.  Cleanup of DOM unit is automatic.
    */
    virtual bool apply(void* ctx) = 0;

    /*
      Called exactly once at end of document.  Override for intended behavior.
    */
    virtual void endOutput(void* ctx) = 0;

    // start creating the document and setup output for the units
    virtual void startDocument(void* ctx) {

        // apparently endDocument() can be called without startDocument() for an
        // empty element
        found = true;

        // setup output
        startOutput(ctx);
    }

    // collect namespaces from root unit.  Start to build the tree if OPTION_XSLT_ALL
    virtual void startRootUnit(void* ctx, const xmlChar* localname, const xmlChar* prefix, const xmlChar* URI,
                               int nb_namespaces, const xmlChar** namespaces, int nb_attributes, int nb_defaulted,
                               const xmlChar** attributes) {

        // if we are building the entire tree, start now
        if (isoption(options, OPTION_XSLT_ALL)) {
            xmlSAX2StartDocument(ctx);
            xmlSAX2StartElementNs(ctx, localname, prefix, URI, nb_namespaces, namespaces, nb_attributes,
                                  nb_defaulted, attributes);
            return;
        }

        // record namespaces in an extensible list so we can add the per unit
        for (int i = 0; i < nb_namespaces; ++i) {
            data.push_back(namespaces[i * 2]);
            data.push_back(namespaces[i * 2 + 1]);
        }
        rootsize = data.size();

    }

    // start to create an individual unit, merging namespace details from the root (if it exists)
    virtual void startUnit(void* ctx, const xmlChar* localname, const xmlChar* prefix, const xmlChar* URI,
                           int nb_namespaces, const xmlChar** namespaces, int nb_attributes, int nb_defaulted,
                           const xmlChar** attributes) {

        /*

          Passing the prefix was causing an error when set.
          Hack change localname to have prefix and set prefix to null
          Using std::string it some cases seems to cause incorrect access of memory
          since it does not seem to duplicate the localname.
          Dynamically allocate for now.

        */
        std::string full_name = "";
        if(prefix) {
          full_name = (const char *)prefix;
          full_name += ":";
        }
        full_name += (const char *)localname;

        prefix_name = (const xmlChar *)strdup(full_name.c_str());

        // if applying to entire archive, then just build this node
        if (isoption(options, OPTION_XSLT_ALL)) {

            xmlSAX2StartElementNs(ctx, localname, prefix_name, 0, nb_namespaces, namespaces, nb_attributes,
                                  nb_defaulted, attributes);

            return;
        }

        // start the document for this unit
        xmlSAX2StartDocument(ctx);

        // remove per-unit namespaces
        data.resize(rootsize);

        // combine namespaces from root and local to this unit
        for (int i = 0; i < nb_namespaces; ++i) {

            // make sure not already in
            bool found = false;
            for (unsigned int j = 0; j < data.size() / 2; ++j)
                if (xmlStrEqual(data[j * 2], namespaces[i * 2]) &&
                    xmlStrEqual(data[j * 2 + 1], namespaces[i * 2 + 1])) {
                    found = true;
                    break;
                }
            if (found)
                continue;

            data.push_back(namespaces[i * 2]);
            data.push_back(namespaces[i * 2 + 1]);
        }


        // start the unit (element) at the root using the merged namespaces
        xmlSAX2StartElementNs(ctx, prefix_name, 0, URI, data.size() / 2,
                              &data[0], nb_attributes, nb_defaulted, attributes);

    }

    // build start element nodes in unit tree
    virtual void startElementNs(void* ctx, const xmlChar* localname, const xmlChar* prefix, const xmlChar* URI,
                                int nb_namespaces, const xmlChar** namespaces, int nb_attributes, int nb_defaulted,
                                const xmlChar** attributes) {

      xmlSAX2StartElementNs(ctx, localname, prefix, URI, nb_namespaces, namespaces, nb_attributes, nb_defaulted, attributes);
    }

    // build end element nodes in unit tree
    virtual void endElementNs(void *ctx, const xmlChar *localname, const xmlChar *prefix, const xmlChar *URI) {

        xmlSAX2EndElementNs(ctx, localname, prefix, URI);
    }

    // characters in unit tree
    virtual void characters(void* ctx, const xmlChar* ch, int len) {

        xmlSAX2Characters(ctx, ch, len);
    }

    // CDATA block in unit tree
    virtual void cdatablock(void* ctx, const xmlChar* ch, int len) {

        xmlSAX2CDataBlock(ctx, ch, len);
    }

    // comments in unit tree
    virtual void comments(void* ctx, const xmlChar* ch) {

        xmlSAX2Comment(ctx, ch);
    }

    // end the construction of the unit tree, apply processing, and delete
    virtual void endUnit(void *ctx, const xmlChar *localname, const xmlChar *prefix, const xmlChar *URI) {

        xmlParserCtxtPtr ctxt = (xmlParserCtxtPtr) ctx;
        SAX2ExtractUnitsSrc* pstate = (SAX2ExtractUnitsSrc*) ctxt->_private;

        // finish building the unit tree
        xmlSAX2EndElementNs(ctx, localname, prefix, URI);
        xmlSAX2EndDocument(ctx);

        // apply the necessary processing
        if (error = !apply(ctx))
            pstate->stopUnit(ctx);

        // free up the document that has this particular unit
	xmlFreeDoc(ctxt->myDoc);
	ctxt->myDoc = 0;

        free((void *)prefix_name);
        prefix_name = 0;
/*
        // unhook the unit tree from the document, leaving an empty document
        xmlNodePtr onode = xmlDocGetRootElement(ctxt->myDoc);
        xmlUnlinkNode(onode);
        xmlFreeNode(onode);
        ctxt->node = 0;
*/
    }

    virtual void endDocument(void *ctx) {

        // endDocument can be called, even if startDocument was not for empty input
        if (!found || error)
            return;

        // end the entire input document
        if (isoption(options, OPTION_XSLT_ALL))
            xmlSAX2EndDocument(ctx);

        // end the output
        endOutput(ctx);
    }

protected:
    std::vector<const xmlChar*> data;
    int rootsize;
    bool found;
    int options;
    bool error;
    const xmlChar * prefix_name;
};

#endif

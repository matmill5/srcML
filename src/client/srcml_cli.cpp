/**
 * @file srcml_cli.cpp
 *
 * @copyright Copyright (C) 2014 srcML, LLC. (www.srcML.org)
 *
 * This file is part of the srcml command-line client.
 *
 * The srcML Toolkit is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The srcML Toolkit is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the srcml command-line client; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <srcml_cli.hpp>
#include <src_prefix.hpp>
#include <stdlib.h>
#include <SRCMLStatus.hpp>
#include <algorithm>

// tell cli11 to use boost optional
#define CLI11_BOOST_OPTIONAL 1
// warning in CLI11.hpp
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <CLI11.hpp>
#pragma GCC diagnostic pop

const char* SRCML_HEADER = R"(Usage: srcml [options] <src_infile>... [-o <srcML_outfile>]
       srcml [options] <srcML_infile>... [-o <src_outfile>]

Translates C, C++, C#, and Java source code to and from the XML
source-code representation srcML. Also supports querying and transformation of srcML.

Source-code input can be from text, standard input, a file,
a directory, or an archive file, i.e., tar, cpio, and zip. Multiple files
are stored in a srcML archive.
)";

const char* SRCML_FOOTER = R"(
Have a question or need to report a bug?
Contact us at http://www.srcml.org/support.html
www.srcML.org)";

class srcMLClI : public CLI::App {
public:
    srcMLClI(std::string app_description, std::string app_name) 
        : CLI::App(app_description, app_name) {

        set_help_flag("-h,--help", "Output this help message and exit")->group("GENERAL OPTIONS");
        footer_ = SRCML_FOOTER;

        // custom error message
        failure_message_ = [](const CLI::App *, const CLI::Error &e) {
            return std::string("srcml: ") + e.what() + "\n";
        };
    }
};

class srcMLFormatter : public CLI::Formatter {
public:
    // user our own usage at the top, included in the header
    std::string make_usage(const CLI::App *, std::string) const override { return ""; }

    // remove NEEDS and ... from help
    inline std::string make_option_opts(const CLI::Option *opt) const override {
        std::stringstream out;

        if (opt->get_type_size() != 0) {
            if (!opt->get_type_name().empty())
                out << " " << get_label(opt->get_type_name());
        }

        return out.str();
    }

    // convert from default 2 blank lines between groups to a single newline
    inline std::string make_group(std::string group, bool is_positional, std::vector<const CLI::Option *> opts) const override {
        std::stringstream out;

        out << /* "\n" << */ group << ":\n";
        for (const auto* opt : opts) {
            out << make_option(opt, is_positional);
        }

        return out.str();
    }
};

srcml_request_t parseCLI11(int argc, char* argv[]) {

    srcml_request_t srcml_request;

    // does this need to be an optional?
    srcml_request.markup_options = 0;

    // Cleanup the arguments for special cases:
    //      xmlns prefix: --xmlns:pre="URL" -> --xmlns=pre="URL"
    //      empty strings on long options, e.g., --text="" -> --text ""
    std::vector<std::string> commandline;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg.substr(0, 8) == "--xmlns:") {
            arg[7] = '=';
        }

        if (arg.back() == '=') {
            commandline.push_back(arg.substr(0, arg.size() - 1));
            commandline.push_back("");
        } else {
            commandline.push_back(arg);
        }
    }
    std::reverse(commandline.begin(), commandline.end());

    srcMLClI app{SRCML_HEADER, "srcml"};
    app.formatter(std::make_shared<srcMLFormatter>());
    app.get_formatter()->column_width(32);

    /*
        Option setup:
        * Assumes check() is defined before each() (execution order follows declaration order)
        * All error handling is through CLI11, or explicit in the lambda (no error messages in called functions)
        * Options/flags are added, and can be searched, by name on command line, e.g., --language
    */

    // positional arguments, i.e., input files
    app.add_option_function<std::vector<std::string>>("InputFiles", [&](const std::vector<std::string>&) {}, "")
        ->group("")
        ->each([&](std::string filename) {

            // record the position of stdin
            if (filename == "-" || filename == "stdin://-")
                srcml_request.stdindex = (int) srcml_request.input_sources.size();

            srcml_input_src input(filename);

            // xslt transformation file
            if (input.extension == ".xsl") {
                srcml_request.transformations.push_back(src_prefix_add_uri("xslt", input.filename));
                return;
            }

            // relaxng transformation file
            if (input.extension == ".rng") {
                srcml_request.transformations.push_back(src_prefix_add_uri("relaxng", input.filename));
                return;
            }

            srcml_request.input_sources.push_back(input);
        });

    // general 
    app.add_flag_callback("--version,-V", [&]() { srcml_request.command |= SRCML_COMMAND_VERSION; },
        "Output version number and exit")
        ->group("GENERAL OPTIONS");

    app.add_flag_callback("--verbose,-v", [&]() { srcml_request.command |= SRCML_COMMAND_VERBOSE; },
        "Conversion and status information to stderr")
        ->group("GENERAL OPTIONS");

    app.add_flag_callback("--quiet,-q",   [&]() { srcml_request.command |= SRCML_COMMAND_QUIET; },
        "Suppress status messages")
        ->group("GENERAL OPTIONS");

    auto output =
    app.add_option("-o,--output",
        "Write output to FILE")
        ->type_name("FILE")
        ->group("GENERAL OPTIONS")
        ->each([&](std::string value) {
            srcml_request.output_filename = srcml_output_dest(value);

            if (srcml_request.output_filename.isdirectory || (srcml_request.output_filename.extension.empty()
                && srcml_request.output_filename.filename.back() == '/')) {

                srcml_request.command |= SRCML_COMMAND_TO_DIRECTORY;
                srcml_request.command |= SRCML_COMMAND_NOARCHIVE;
            }
        });

    srcml_request.max_threads = 4;
    app.add_option("--jobs,-j", srcml_request.max_threads,
        "Allow up to NUM threads for source parsing")
        ->type_name("NUM")
        ->group("GENERAL OPTIONS");

    // src2srcml_options "CREATING SRCML"
    auto text =
    app.add_option("--text,-t", 
        "Input source code from STRING, e.g., --text=\"int a;\"")
        ->type_name("STRING")
        ->group("CREATING SRCML")
        ->expected(1)
        ->check([&](std::string value) {
            if (!value.empty() && value[0] == '-') {
                std::cerr << "srcml: --text: 1 required STRING missing";
                exit(CLI_STATUS_ERROR);
            }
            return "";
        })
        ->each([&](std::string text) { 
            srcml_request.input_sources.push_back(src_prefix_add_uri("text", text));
        });

    app.add_flag_callback("--no-tabs",      [&]() { /* *srcml_request.markup_options |= SRCML_HASH; */},
        "Expand tabs on input")
        ->group("CREATING SRCML");

    auto language =
    app.add_option("--language,-l", srcml_request.att_language,
        "Set the source-code language to C, C++, C#, or Java. Required for --text option")
        ->type_name("LANG")
        ->group("CREATING SRCML")
        ->expected(1)
        ->check([&](std::string lang) {
            if (lang.empty() || srcml_check_language(lang.c_str()) == 0) {
                SRCMLstatus(ERROR_MSG, "srcml: invalid language \"%s\"", lang);
                exit(CLI_STATUS_ERROR);
            }
            return "";
        });

    app.add_option("--files-from", 
        "Input source-code filenames from FILE")
        ->type_name("FILE")
        ->group("CREATING SRCML")
        ->type_size(-1)
        ->each([&](const std::string& value) {
            srcml_request.files_from.push_back(value);
            srcml_request.input_sources.push_back(src_prefix_add_uri("filelist", value));
        });
    
    app.add_option("--register-ext", srcml_request.language_ext,
        "Register file extension EXT for source-code language LANG, e.g., --register-ext h=C++")
        ->type_name("EXT=LANG")
        ->group("CREATING SRCML");
    
    app.add_option("--src-encoding", srcml_request.src_encoding,
        "Set the input source-code encoding")->type_name("ENCODING")
        ->group("CREATING SRCML");
    
    app.add_flag_callback("--archive,-r",      [&]() { *srcml_request.markup_options |= SRCML_ARCHIVE; },
        "Create a srcML archive, default for multiple input files")
        ->group("CREATING SRCML");

    auto output_xml =
    app.add_flag_callback("--output-srcml,-X",   [&]() { srcml_request.command |= SRCML_COMMAND_XML; },
        "Output in XML instead of text")
        ->group("CREATING SRCML");
    
    auto output_xml_outer =
    app.add_flag_callback("--output-srcml-outer",[&]() { 
        srcml_request.command |= SRCML_COMMAND_XML_FRAGMENT;
        srcml_request.command |= SRCML_COMMAND_XML;
    },
        "Output an individual unit from an XML archive")
        ->group("CREATING SRCML");
    
    auto output_xml_inner =
    app.add_flag_callback("--output-srcml-inner",[&]() {
        srcml_request.command |= SRCML_COMMAND_XML_RAW;
        srcml_request.command |= SRCML_COMMAND_XML;
    },
        "Output contents of XML unit")
        ->group("CREATING SRCML");

    // markup options
    app.add_flag_callback("--position",        [&]() { *srcml_request.markup_options |= SRCML_OPTION_POSITION; },
        "Include start and end attributes with line/column of each element")
        ->group("MARKUP OPTIONS");

    // default tabs
    srcml_request.tabs = 8;
    app.add_option("--tabs", srcml_request.tabs,
        "Set tab stop at every NUM characters, default of 8")
        ->type_name("NUM")
        ->group("MARKUP OPTIONS")
        ->check(CLI::Range(1, 8))
        ->each([&](std::string){ *srcml_request.markup_options |= SRCML_OPTION_POSITION; });

#if 0
    // check tabstop
    if (value < 1) {
        SRCMLstatus(ERROR_MSG, "srcml: %d is an invalid tab stop. Tab stops must be 1 or higher.", value);
        exit(1); //ERROR CODE TBD
    }
#endif
    app.add_flag_callback("--cpp",              [&]() { *srcml_request.markup_options |= SRCML_OPTION_CPP; },
        "Enable preprocessor parsing and markup (default for C/C++/C#)")
        ->group("MARKUP OPTIONS");

    app.add_flag_callback("--cpp-markup-if0",   [&]() { *srcml_request.markup_options |= SRCML_OPTION_CPP_MARKUP_IF0; },
        "Markup preprocessor #if 0 regions")
        ->group("MARKUP OPTIONS");

    app.add_flag_callback("--cpp-nomarkup-else",[&]() { *srcml_request.markup_options |= SRCML_OPTION_CPP_TEXT_ELSE; },
        "Do not markup preprocessor #else/#elif regions")
        ->group("MARKUP OPTIONS");

    // xml_form
    // @todo Why not check in the client?
    srcml_request.att_xml_encoding = "UTF-8";
    app.add_option("--xml-encoding", srcml_request.att_xml_encoding,
        "Set output XML encoding. Default is UTF-8")
        ->type_name("ENCODING")
        ->group("ENCODING")
        ->check([&](const std::string &value) { 

            if (value.empty() || srcml_check_encoding(value.c_str()) == 0) {
                return std::string("invalid xml encoding \"") + value + "\"";
            }

            return std::string("");
        });

    app.add_flag_callback("--no-xml-declaration",[&]() { *srcml_request.markup_options |= SRCML_OPTION_NO_XML_DECL; },
        "Do not output the XML declaration")
        ->type_name("XML")
        ->group("ENCODING");

    app.add_option("--xmlns", "Set the default namespace URI, or declare the PRE for namespace URI")
        ->type_name("URI, PRE=URI")
        ->group("ENCODING")
        ->expected(1)
        ->each([&](const std::string& value) {  
            auto delim = value.find("=");
            if (delim == std::string::npos) {
                srcml_request.xmlns_namespaces[""] = value;
                srcml_request.xmlns_namespace_uris[value] = "";
            } else {
                srcml_request.xmlns_namespaces[value.substr(0, delim)] = value.substr(delim + 1);
                srcml_request.xmlns_namespace_uris[value.substr(delim + 1)] = value.substr(0, delim);
            }
        });

    // metadata
    app.add_flag_callback("--list,-L",        [&]() { srcml_request.command |= SRCML_COMMAND_LIST; },
        "List all files in the srcML archive and exit")
        ->group("METADATA OPTIONS");

    app.add_flag_callback("--info,-i",        [&]() { srcml_request.command |= SRCML_COMMAND_INFO; },
        "Output most metadata except srcML file count and exit")
        ->group("METADATA OPTIONS");

    app.add_flag_callback("--full-info,-I",   [&]() { srcml_request.command |= SRCML_COMMAND_LONGINFO; },
        "Output all metadata including srcML file count and exit")
        ->group("METADATA OPTIONS");

    app.add_flag_callback("--show-language",  [&]() { srcml_request.command |= SRCML_COMMAND_DISPLAY_SRCML_LANGUAGE; },
        "Output source language and exit")
        ->group("METADATA OPTIONS");

    app.add_flag_callback("--show-url",       [&]() { srcml_request.command |= SRCML_COMMAND_DISPLAY_SRCML_URL; },
        "Output source url and exit")
        ->group("METADATA OPTIONS");

    app.add_flag_callback("--show-filename",  [&]() { srcml_request.command |= SRCML_COMMAND_DISPLAY_SRCML_FILENAME; },
        "Output source filename and exit")
        ->group("METADATA OPTIONS");

    app.add_flag_callback("--show-src-version",   [&]() { srcml_request.command |= SRCML_COMMAND_DISPLAY_SRCML_SRC_VERSION; },
        "Output source version and exit")
        ->group("METADATA OPTIONS");

    app.add_flag_callback("--show-timestamp", [&]() { srcml_request.command |= SRCML_COMMAND_DISPLAY_SRCML_TIMESTAMP; },
        "Output source timestamp and exit")
        ->group("METADATA OPTIONS");

    app.add_flag_callback("--show-hash",      [&]() { srcml_request.command |= SRCML_COMMAND_DISPLAY_SRCML_HASH; },
        "Output source hash and exit")
        ->group("METADATA OPTIONS");

    app.add_flag_callback("--show-encoding",  [&]() { srcml_request.command |= SRCML_COMMAND_DISPLAY_SRCML_ENCODING; },
        "Output xml encoding and exit")
        ->group("METADATA OPTIONS");

    app.add_flag_callback("--show-unit-count",[&]() { srcml_request.command |= SRCML_COMMAND_UNITS; },
        "Output number of srcML files and exit")
        ->group("METADATA OPTIONS");

    app.add_option("--show-prefix", srcml_request.xmlns_prefix_query, 
        "Output prefix of namespace URI and exit")
        ->type_name("URI")
        ->group("METADATA OPTIONS");

    auto filename =
    app.add_option("--filename,-f", srcml_request.att_filename,
        "Set the filename attribute")
        ->type_name("FILENAME")
        ->group("METADATA OPTIONS");

    app.add_option("--url", srcml_request.att_url,
        "Set the url attribute")
        ->type_name("URL")
        ->group("METADATA OPTIONS");

    app.add_option("--src-version,-s", srcml_request.att_version, 
        "Set the version attribute")
        ->type_name("VERSION")
        ->group("METADATA OPTIONS");

    app.add_flag_callback("--hash",      [&]() { *srcml_request.markup_options |= SRCML_HASH; },
        "Include generated hash attribute")
        ->group("METADATA OPTIONS");

    app.add_flag_callback("--timestamp", [&]() { srcml_request.command |= SRCML_COMMAND_TIMESTAMP; },
        "Include generated timestamp attribute")
        ->group("METADATA OPTIONS");

    // srcml2src
    app.add_option("--unit,-U", srcml_request.unit, 
        "Extract the source code for an individual unit at position NUM in a srcML archive")
        ->type_name("NUM")
        ->group("EXTRACTING SOURCE CODE");

    app.add_option_function<std::string>("--eol", [&](std::string value) {

        std::transform(value.begin(), value.end(), value.begin(), ::tolower);

        if (value == "auto") {
            srcml_request.eol = SOURCE_OUTPUT_EOL_AUTO;
        } else if (value == "lf" || value == "unix") {
            srcml_request.eol = SOURCE_OUTPUT_EOL_LF;
        } else if (value == "cr") {
            srcml_request.eol = SOURCE_OUTPUT_EOL_CR;
        } else if (value == "crlf" || value == "windows") {
            srcml_request.eol = SOURCE_OUTPUT_EOL_CRLF;
        } else {
            SRCMLstatus(ERROR_MSG, "srcml: EOL must be (default) AUTO, UNIX or LF, Windows or CRLF, or CR");
            exit(SRCML_STATUS_INVALID_ARGUMENT);
        }

        return true;
    },
        "Set the output source EOL: AUTO (default), UNIX or LF, Windows or CRLF, or CR")->type_name("EOL")
        ->group("EXTRACTING SOURCE CODE");

    auto output_src =
    app.add_flag_callback("--output-src,-S",  [&]() { srcml_request.command |= SRCML_COMMAND_SRC; },
        "Output source code instead of srcML")
        ->group("EXTRACTING SOURCE CODE");

    auto todir =
    app.add_option_function<std::string>("--to-dir", [&](const std::string& value) {

        srcml_request.output_filename = srcml_output_dest(value);
        srcml_request.command |= SRCML_COMMAND_TO_DIRECTORY;
        srcml_request.command |= SRCML_COMMAND_NOARCHIVE;
        return true;
    },
        "Extract source-code files to a DIRECTORY")
        ->type_name("DIRECTORY")
        ->group("EXTRACTING SOURCE CODE");

    // query/transform
    auto xpath =
    app.add_option("--xpath", 
        "Apply XPATH expression to each individual srcML unit")
        ->type_name("XPATH")
        ->group("QUERY & TRANSFORMATION")
        ->each([&](std::string value) {
            srcml_request.transformations.push_back(src_prefix_add_uri("xpath", value));
            srcml_request.xpath_query_support.push_back(std::make_pair(boost::none,boost::none));
        });

    app.add_option("--attribute", 
        "Insert attribute PRE:NAME=\"VALUE\" into element results of XPath query in original unit")
        ->type_name("PRE:NAME=\"VALUE\"")
        ->group("QUERY & TRANSFORMATION")
        ->needs(xpath)
        ->check([&](std::string value) {
            if (srcml_request.xpath_query_support.empty()) {

                SRCMLstatus(ERROR_MSG, "srcml: attribute option must follow an --xpath option");
                exit(SRCML_STATUS_INVALID_ARGUMENT);
            }

            // Attribute must have a value
            if (value.find("=") == std::string::npos) {
                SRCMLstatus(ERROR_MSG, "srcml: the attribute %s is missing a value", value);
                exit(SRCML_STATUS_INVALID_ARGUMENT);
            }

            // Missing prefix requires an element with a prefix
            if (value.find(":") == std::string::npos && !(srcml_request.xpath_query_support.back().first)) {
                SRCMLstatus(ERROR_MSG, "srcml: the attribute %s is missing a prefix or an element with a prefix", value);
                exit(SRCML_STATUS_INVALID_ARGUMENT);
            }

            return "";
        })
        ->each([&](std::string value) {

            auto attrib_colon = value.find(":");
            auto attrib_equals = value.find("=");

            attribute attrib;

            if (attrib_colon != std::string::npos) {
                // PREFIX:NAME=VALUE
                attrib.prefix = value.substr(0, attrib_colon);
                attrib.name = value.substr(attrib_colon + 1, attrib_equals - attrib_colon - 1);
            } else {
                // NAME=VALUE
                attrib.prefix = srcml_request.xpath_query_support.back().first->prefix;
                attrib.name = value.substr(0, attrib_equals);
            }

            // value starts after '='
            auto attrib_value_start = attrib_equals + 1;

            // value may be wrapped with quotes that need to be removed
            // ="VALUE", ='VALUE'
            if (value[attrib_value_start] == '\'' || value[attrib_value_start] == '"')
                ++attrib_value_start;
            auto attrib_value_size = value.size() - attrib_value_start;
            if (value.back() == '\'' || value.back() == '"')
                --attrib_value_size;

            attrib.value = value.substr(attrib_value_start, attrib_value_size);

            srcml_request.xpath_query_support.back().second = attrib;
        });

    app.add_option("--element", 
        "Insert element PRE:NAME around each element result of XPath query in original unit")
        ->type_name("PRE:NAME")
        ->group("QUERY & TRANSFORMATION")
        ->needs(xpath)
        ->check([&](std::string value) {
            if (srcml_request.xpath_query_support.empty()) {
                SRCMLstatus(ERROR_MSG, "srcml: element option must follow an --xpath option");
                exit(SRCML_STATUS_INVALID_ARGUMENT);
            }

            if (value.find(":") == std::string::npos) {
                SRCMLstatus(ERROR_MSG, "srcml: element uri's require a prefix");
                exit(SRCML_STATUS_INVALID_ARGUMENT);
            }
            return "";
        })
        ->each([&](std::string value) { 
            auto elemn_index = value.find(":");
            srcml_request.xpath_query_support.back().first = element{ value.substr(0, elemn_index), value.substr(elemn_index + 1) };
        });

    auto xslt =
    app.add_option("--xslt", 
        "Apply the XSLT program FILE to each unit, where FILE can be a url")
        ->type_name("FILE")
        ->group("QUERY & TRANSFORMATION")
        ->each([&](std::string value) {
            srcml_request.transformations.push_back(src_prefix_add_uri("xslt", value));
        });

    app.add_option("--xslt-param", 
        "Passes the string parameter NAME with UTF-8 encoded string VALUE to the XSLT program")
        ->type_name("NAME=\"VALUE\"")
        ->group("QUERY & TRANSFORMATION")
        ->needs(xslt)
        ->each([&](std::string value) {
            srcml_request.transformations.push_back(src_prefix_add_uri("xslt-param", value));
        });

    app.add_option("--relaxng", 
        "Output individual units that match the RelaxNG pattern FILE, where FILE can be a url")
        ->type_name("FILE")
        ->group("QUERY & TRANSFORMATION")
        ->each([&](std::string value) {
            srcml_request.transformations.push_back(src_prefix_add_uri("relaxng", value));
        });

    // debug
    app.add_flag_callback("--dev",          [&]() { srcml_request.command |= SRCML_DEBUG_MODE; },
        "Enable developer debug mode")
        ->group("");

    app.add_flag_callback("--timing",       [&]() { srcml_request.command |= SRCML_TIMING_MODE; },
        "Enable developer timing mode")
        ->group("");

    // experimental_options
    app.add_flag_callback("--cat",          [&]() { srcml_request.command |= SRCML_COMMAND_CAT_XML; },
        "Cat all the XML units into a single unit")
        ->group("");

    app.add_flag("--revision", srcml_request.revision, 
        "Extract the given revision (0 = original, 1 = modified)")
        ->group("");

    app.add_flag_callback("--update",       [&]() { srcml_request.command |= SRCML_COMMAND_UPDATE; },
        "Output and update existing srcml")
        ->group("");

    app.add_flag("--xml-processing", srcml_request.xml_processing,
        "Add XML processing instruction")
        ->group("");
    
    app.add_flag("--pretty", srcml_request.pretty_format,
        "Custom formatting for output")
        ->group("");
    
    app.add_flag("--external", srcml_request.external,
        "Run a user defined external script or application on srcml client output")
        ->group("");
    
    app.add_flag_callback("--parser-test",      [&]() {

        srcml_request.command |= SRCML_COMMAND_PARSER_TEST; 
        srcml_request.command |= SRCML_TIMING_MODE; 
    },
        "Run parser tests on the input files")
        ->group("");

    app.add_flag_callback("--no-color",   [&]() { srcml_request.command |= SRCML_COMMAND_NO_COLOR; },
        "Suppress colorization of output")
        ->group("");

    output_src->excludes(output_xml);
    output_src->excludes(output_xml_inner);
    output_src->excludes(output_xml_outer);
    output->excludes(todir);
    output_xml->excludes(output_xml_inner);
    output_xml->excludes(output_xml_outer);
    output_xml_outer->excludes(output_xml_inner);

    try {
        app.parse(commandline);
    } catch (const CLI::CallForHelp &e) {
        app.exit(e);
        exit(CLI_STATUS_OK);
    } catch (const CLI::ExtrasError &e) {
        app.exit(e);
        exit(CLI_STATUS_ERROR);
    } catch (const CLI::ParseError &e) {
        app.exit(e);
        exit(CLI_STATUS_ERROR);
    }

    // make sure --text has an indication of language
    if (!text->empty() && language->empty() && filename->empty()) {
        SRCMLstatus(ERROR_MSG, "srcml: --text requires --language or --filename to determine source language");
        exit(CLI_STATUS_ERROR);
    }
    
    if (srcml_request.output_filename == "")
        srcml_request.output_filename = "stdout://-";

    // if no input given, then artificially put a "-" into the list of input files
    if (srcml_request.input_sources.empty()) {
        srcml_request.stdindex = 0;
        srcml_request.input_sources.push_back(srcml_input_src("stdin://-"));
    }

    if (srcml_request.input_sources.size() == 1 && srcml_request.input_sources[0].isdirectory) {
        auto url = srcml_request.input_sources[0].resource;
        while (url.length() > 0 && (url[0] == '.' || url[0] == '/')) {
            url.erase(0,1);
        }
        srcml_request.att_url = url;
    }

    // check user-provided namespaces as a set compared to standard namespaces
    // @todo Not sure of this, and that it catches everything
    for (size_t i = 0; i < srcml_get_namespace_size(); ++i) {
        std::string std_prefix = srcml_get_namespace_prefix(i);
        std::string std_uri = srcml_get_namespace_uri(i);

        auto& user_namespaces = srcml_request.xmlns_namespaces;
        auto& user_uris = srcml_request.xmlns_namespace_uris;

        // A reserved std_prefix wasn't used or it was set to the same std_uri
        if (user_namespaces.find(std_prefix) == user_namespaces.end() || user_namespaces[std_prefix] == std_uri) {
            continue;
        }

        // A reserved uri is set to a different prefix
        if (user_uris.find(std_uri) != user_namespaces.end() && user_uris[std_uri] != std_prefix) {
            continue;
        }

        SRCMLstatus(ERROR_MSG, "srcml: prefix \"" + std_prefix + "\" assigned multiple URIs \"" + std_uri + "\", \"" + user_namespaces[std_prefix] + "\"");
        exit(CLI_STATUS_ERROR);
    }

    return srcml_request;
}

// @todo Do we need to deprecate --units, -n?

#if 0
// option src encoding
template <>
void option_field<&srcml_request_t::src_encoding>(const std::string& value) {

    if (value.empty() /* || srcml_check_encoding(value.c_str()) == 0 */) {
        SRCMLstatus(ERROR_MSG, "srcml: invalid src encoding \"" + value + "\"");
        exit(CLI_ERROR_INVALID_ARGUMENT);
    }
    srcml_request.src_encoding = value;
}

// option xml encoding attribute
template <>
void option_field<&srcml_request_t::att_xml_encoding>(const std::string& value) {

    // required since an error occurs if checked in client
    if (value.empty() || srcml_check_encoding(value.c_str()) == 0) {
        SRCMLstatus(ERROR_MSG, "srcml: invalid xml encoding \"%s\"", value);
        exit(CLI_ERROR_INVALID_ARGUMENT);
    }
    srcml_request.att_xml_encoding = value;
}
#endif

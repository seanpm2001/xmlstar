/*  $Id: xml_trans.c,v 1.35 2004/09/10 02:02:21 mgrouch Exp $  */

/*

XMLStarlet: Command Line Toolkit to query/edit/check/transform XML documents

Copyright (c) 2002 Mikhail Grushinskiy.  All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include "config.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "trans.h"

/*
 *  TODO:
 *        1. proper command line arguments handling
 *        2. review and clean up all code (free memory)
 *        3. check embedded stylesheet support
 *        4. exit values on errors
 */

/*
 * usage string chunk : 509 char min on ISO C90
 */
static const char trans_usage_str_1[] =
"XMLStarlet Toolkit: Transform XML document(s) using XSLT\n"
"Usage: xml tr [<options>] <xsl-file> {-p|-s <name>=<value>} [<xml-file>...]\n"
"where\n"
"  <xsl-file>      - main XSLT stylesheet for transformation\n"
"  <xml-file>      - input XML document file/URL (stdin is used if missing)\n";

static const char trans_usage_str_2[] =
"  <name>=<value>  - name and value of the parameter passed to XSLT processor\n"
"  -p              - parameter is XPATH expression (\"'string'\" to quote string)\n"
"  -s              - parameter is a string literal\n"
"<options> are:\n"
"  --omit-decl     - omit xml declaration <?xml version=\"1.0\"?>\n"
"  --show-ext      - show list of extensions\n";

static const char trans_usage_str_3[] =
"  --val           - allow validate against DTDs or schemas\n"
"  --net           - allow fetch DTDs or entities over network\n"
#ifdef LIBXML_XINCLUDE_ENABLED
"  --xinclude      - do XInclude processing on document input\n"
#endif
"  --maxdepth val  - increase the maximum depth\n"
#ifdef LIBXML_HTML_ENABLED
"  --html          - input document(s) is(are) in HTML format\n"
#endif
#ifdef LIBXML_CATALOG_ENABLED
"  --catalogs      - use SGML catalogs from $SGML_CATALOG_FILES\n"
"                    otherwise XML catalogs starting from\n"
"                    file:///etc/xml/catalog are activated by default\n\n";
#endif

/**
 *  Display usage syntax
 */
void
trUsage(int argc, char **argv)
{
    extern const char more_info[];
    extern const char libxslt_more_info[];
    FILE* o = stderr;
    fprintf(o, "%s", trans_usage_str_1);
    fprintf(o, "%s", trans_usage_str_2);
    fprintf(o, "%s", trans_usage_str_3);
    fprintf(o, "%s", more_info);
    fprintf(o, "%s", libxslt_more_info);
    exit(1);
}

/**
 *  Parse global command line options
 */
int
trParseOptions(xsltOptionsPtr ops, int argc, char **argv)
{
    int i;
    
    if (argc <= 2) return argc;
    for (i=2; i<argc; i++)
    {
        if (argv[i][0] == '-')
        {
            if (!strcmp(argv[i], "--help"))
            {
                trUsage(argc, argv);
            }
            else if (!strcmp(argv[i], "--show-ext"))
            {
                ops->show_extensions = 1;
            }
            else if (!strcmp(argv[i], "--val"))
            {
                ops->noval = 0;
            }
            else if (!strcmp(argv[i], "--net"))
            {
                ops->nonet = 0;
            }
            else if (!strcmp(argv[i], "--omit-decl"))
            {
                ops->omit_decl = 1;
            }
            else if (!strcmp(argv[i], "--maxdepth"))
            {
                int value;
                i++;
                if (i >= argc) trUsage(0, NULL);
                if (sscanf(argv[i], "%d", &value) == 1)
                    if (value > 0) xsltMaxDepth = value;
            }
#ifdef LIBXML_XINCLUDE_ENABLED
            else if (!strcmp(argv[i], "--xinclude"))
            {
                ops->xinclude = 1;
            }
#endif
#ifdef LIBXML_HTML_ENABLED
            else if (!strcmp(argv[i], "--html"))
            {
                ops->html = 1;
            }
#endif
        }
        else
            break;
    }

    return i;
}

/**
 *  Cleanup memory
 */
void
trCleanup()
{
    xsltCleanupGlobals();
    xmlCleanupParser();
#if 0
    xmlMemoryDump();
#endif
}

/**
 *  Parse command line for XSLT parameters
 */
int
trParseParams(const char** params, int* plen,
              int count, char **argv)
{
    int i;
    *plen = 0;
    params[0] = 0;

    for (i=0; i<count; i++)
    {
        if (argv[i][0] == '-')
        {
            if (!strcmp(argv[i], "-p"))
            {
                int j;
                xmlChar *name, *value;
                
                i++;
                if (i >= count) trUsage(0, NULL);

                for(j=0; argv[i][j] && (argv[i][j] != '='); j++);
                if (argv[i][j] != '=') trUsage(0, NULL);
                
                name = xmlStrndup((const xmlChar *) argv[i], j);
                value = xmlStrdup((const xmlChar *) argv[i]+j+1);

                if (*plen >= MAX_PARAMETERS)
                {
                    fprintf(stderr, "too many params increase MAX_PARAMETERS\n");
                    exit(2);
                }

                params[*plen] = (char *)name;
                (*plen)++;
                params[*plen] = (char *)value;
                (*plen)++;                
                params[*plen] = 0;
            }
            else if (!strcmp(argv[i], "-s"))
            {
                int j, len;
                const xmlChar *string;
                xmlChar *name, *value;

                i++;
                if (i >= count) trUsage(0, NULL);

                for(j=0; argv[i][j] && (argv[i][j] != '='); j++);
                if (argv[i][j] != '=') trUsage(0, NULL);

                name = xmlStrndup((const xmlChar *)argv[i], j);
                string = xmlStrdup((const xmlChar *)argv[i]+j+1);

                len = xmlStrlen(string);
                if (xmlStrchr(string, '"'))
                {
                    if (xmlStrchr(string, '\''))
                    {
                        fprintf(stderr,
                            "string parameter contains both quote and double-quotes\n");
                        exit(8);
                    }
                    value = xmlStrdup((const xmlChar *)"'");
                    value = xmlStrcat(value, string);
                    value = xmlStrcat(value, (const xmlChar *)"'");
                }
                else
                {
                    value = xmlStrdup((const xmlChar *)"\"");
                    value = xmlStrcat(value, string);
                    value = xmlStrcat(value, (const xmlChar *)"\"");
                }

                if (*plen >= MAX_PARAMETERS)
                {
                    fprintf(stderr, "too many params increase MAX_PARAMETERS\n");
                    exit(2);
                }

                params[*plen] = (char *)name;
                (*plen)++;
                params[*plen] = (char *)value;
                (*plen)++;
                params[*plen] = 0;
            }
        }
        else
            break;
    }

    return i;    
}

/**
 *  Cleanup memory allocated by XSLT parameters
 */
void
trCleanupParams(const char **xsltParams)
{
    const char **p = xsltParams;

    while (*p)
    {
        xmlFree((char *)*p);
        p++;
    }
}

/**
 *  This is the main function for 'tr' option
 */
int
trMain(int argc, char **argv)
{
    static xsltOptions ops;
    static const char *xsltParams[2 * MAX_PARAMETERS + 1];

    int errorno = 0;
    int start, xslt_ind;
    int pCount;
    
    if (argc <= 2) trUsage(argc, argv);

    xsltInitOptions(&ops);
    start = trParseOptions(&ops, argc, argv);
    xslt_ind = start;
    xsltInitLibXml(&ops);

    /* set parameters */
    start += trParseParams(xsltParams, &pCount, argc-start-1, argv+start+1);
                            
    /* run transformation */
    errorno = xsltRun(&ops, argv[xslt_ind], xsltParams,
                      argc-start-1, argv+start+1);

    /* free resources */
    trCleanupParams(xsltParams);
    trCleanup();
    
    return errorno;                                                
}

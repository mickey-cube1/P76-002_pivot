/* *****************************************************************************
Copyright (c) 2016, mickey.cube1+pivot AT gmail.com
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met: 

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer. 
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution. 

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************** */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

typedef struct {
	unsigned long *fieldno;
	unsigned long count;
	unsigned long nalloc;
} FIELDLIST_T;

typedef struct {
	char *buffer;
	size_t balloc;
	char **fields;
	unsigned long count;
	unsigned long nalloc;
} LINE_T;

static LINE_T gLine[2];

static FIELDLIST_T gKeyFieldList;
static FIELDLIST_T gValueFieldList;
static const char *gDelimChars = " \t";
static int gSpanMultiDelim = 1;
static const char *gEmptyField;
static int gReverseMode = 0;
static int gIgnoreCase = 0;
static unsigned long gNumOfKeyFields;
static unsigned long gNumOfValueFields;

static void
enomem(void)
{
	errx(1, "no memory");
}

#if 0
static void
dbg_fieldlist(const FIELDLIST_T *aFieldList)
{
	int i;

	printf("count:%lu nalloc:%lu\n[ ", aFieldList->count, aFieldList->nalloc);
	for (i = 0 ; i < aFieldList->count ; i++) {
		printf("%lu ", aFieldList->fieldno[i]);
	}
	printf("]\n");
}

static void
dbg_line(const LINE_T *aLine)
{
	int i;

	printf("count:%lu nalloc:%lu\n[ ", aLine->count, aLine->nalloc);
	for (i = 0 ; i < aLine->count ; i++) {
		printf("[%s] ", aLine->fields[i]);
	}
	printf("]\n");
}
#endif

static void
out_fields(FILE *aFp, const LINE_T *aLine, const FIELDLIST_T *aFieldList, int aOffset, int aNeedSepa)
{
	int i;

	for (i = 0 ; i < aFieldList->count ; i++) {
		if (aNeedSepa) {
			fprintf(aFp, "%c", *gDelimChars);
		}
		aNeedSepa++;
		
		if (aLine->count <= aFieldList->fieldno[i] + aOffset) {
			/* empty field */
			if (gEmptyField != NULL) {
				fprintf(aFp, "%s", gEmptyField);
			}
		} else {
			fprintf(aFp, "%s", aLine->fields[aFieldList->fieldno[i] + aOffset]);
		}
	}
}

static void
make_fieldlist(FIELDLIST_T *aFieldList, const char *aOptName, const char *aOpt)
{
	char *optstr = strdup(aOpt);
	char *option = optstr;
	char *token;
	char *end;
	unsigned long *n;
	long fieldno;

	while ((token = strsep(&option, ", \t")) != NULL) {
		if (*token == '\0') {
			continue;
		}
		fieldno = strtol(token, &end, 10);
		if (*end) {
			errx(1, "malformed -%s option field", aOptName);
		}
		if (fieldno <= 0) {
			errx(1, "field numbers are 1 based");
		}
		if (aFieldList->count == aFieldList->nalloc) {
			n = realloc(aFieldList->fieldno, (aFieldList->nalloc + 50) * sizeof(unsigned long));
			if (n == NULL) {
				enomem();
			}
			aFieldList->fieldno = n;
			aFieldList->nalloc += 50;
		}
		aFieldList->fieldno[aFieldList->count] = fieldno - 1;
		++aFieldList->count;
	}

	free(optstr);
}

static unsigned long
estimate_maxfieldno(const FIELDLIST_T *aFieldList)
{
	int i;
	unsigned long v = 0;

	for (i = 0 ; i < aFieldList->count ; i++) {
		if (v < aFieldList->fieldno[i]) {
			v = aFieldList->fieldno[i];
		}
	}

	return v + 1;
}

int
cmp_keys(const LINE_T *aL1, const LINE_T *aL2, const FIELDLIST_T *aKeys)
{
	int	i;
	int	cv;

	for (i = 0 ; i < aKeys->count ; i++) {
		if (aL1->count <= aKeys->fieldno[i]) {
			if (aL2->count <= aKeys->fieldno[i]) {
				continue;
			} else {
				return 1;
			}
		}
		if (aL2->count <= aKeys->fieldno[i]) {
			return -1;
		}
		if (gIgnoreCase != 0) {
			cv = strcasecmp(aL1->fields[i], aL2->fields[i]);
		} else {
			cv = strcmp(aL1->fields[i], aL2->fields[i]);
		}
		if (cv != 0) {
			return cv;
		}
	}

	return 0;
}

ssize_t
read_line(LINE_T *aLine, FILE *aFp)
{
	ssize_t len;
	char *cp;
	char *fieldp;

	len = getline(&aLine->buffer, &aLine->balloc, aFp);
	if (len != -1) {
		/* Replace trailing newline, if it exists. */ 
		if (aLine->buffer[len - 1] == '\n') {
			aLine->buffer[len - 1] = '\0';
		}

		aLine->count = 0;
		cp = aLine->buffer;
		while ((fieldp = strsep(&cp, gDelimChars)) != NULL) {
			if (gSpanMultiDelim != 0 && *fieldp == '\0') {
				continue;
			}
			if (aLine->count == aLine->nalloc) {
				char **n;
				n = realloc(aLine->fields, (aLine->nalloc + 50) * sizeof(char *));
				if (n == NULL) {
					enomem();
				}
				aLine->fields = n;
				aLine->nalloc += 50;
			}
			aLine->fields[aLine->count] = fieldp;
			++aLine->count;
		}
	}

	return len;
}

void
do_pivot(FILE *inFp, FILE *outFp)
{
	ssize_t len;
	int cv;
	int needlf = 0;
	int current = 0;

	while ((len = read_line(&gLine[current], inFp)) != -1) {

		cv = cmp_keys(&gLine[1-current], &gLine[current], &gKeyFieldList);
		if (cv) {
			if (needlf) {
				fprintf(outFp, "\n");
			}
			needlf = 1;
			out_fields(outFp, &gLine[current], &gKeyFieldList, 0, 0);
			out_fields(outFp, &gLine[current], &gValueFieldList, 0, 1);
			current = 1 - current;
		} else {
			out_fields(outFp, &gLine[current], &gValueFieldList, 0, 1);
		}
	}
	if (needlf) {
		fprintf(outFp, "\n");
	}
	
	return;
}

void
do_unpivot(FILE *inFp, FILE *outFp)
{
	ssize_t len;
	int current = 0;
	unsigned long vfspos;

	while ((len = read_line(&gLine[current], inFp)) != -1) {

		for (vfspos = gNumOfKeyFields ; vfspos < gLine[current].count ; vfspos += gNumOfValueFields) {
			out_fields(outFp, &gLine[current], &gKeyFieldList, 0, 0);
			out_fields(outFp, &gLine[current], &gValueFieldList, vfspos, 1);
			fprintf(outFp, "\n");
		}
	}
	
	return;
}

static void 
usage(void) 
{ 
	fprintf(stderr,  
		"usage: pivot [OPTION]... [IN-FILE [OUT-FILE]]\n"
		"\n"
		"OPTION:\n"
		" -k FIELD-LIST        key field list\n"
		" -K NUM-OF-FIELDS     number of key fields\n"
		" -o FIELD-LIST        value field list\n"
		" -O NUM-OF-FIELDS     number of value fields\n"
		" -t CHAR              separator\n"
		" -e STRING            use STRING as empty-field\n"
		" -i                   ignore case\n" 
		" -R                   reverse opration aka. unpivot\n"
		" -v                   show version\n"
		" -h                   show this help\n"
	);
	exit(1); 
} 

static void 
version(void) 
{ 
	fprintf(stderr,  
		PACKAGE_VERSION "\n"
	);
	exit(1); 
} 

int
main(int argc, char *argv[])
{
	int ch;
	char *endp;
	long nfval;
	unsigned long nn;
	FILE *ifp;
	FILE *ofp;

	while ((ch = getopt(argc, argv, "Rk:o:t:e:iK:O:vh")) != -1) {
		switch (ch) {
		case 'R':
			gReverseMode = 1;
			break;
		case 'k':
			make_fieldlist(&gKeyFieldList, "k", optarg);
			break;
		case 'o':
			make_fieldlist(&gValueFieldList, "o", optarg);
			break;
		case 't':
			gSpanMultiDelim = 0;
			gDelimChars = optarg;
			if (strlen(gDelimChars) != 1) {
				errx(1, "illegal tab character specification");
			}
			break;
		case 'e':
			gEmptyField = strdup(optarg);
			break;
		case 'i':
			gIgnoreCase = 1;
			break;
		case 'K':
			nfval = strtol(optarg, &endp, 10);
			if (nfval < 1) {
				errx(1, "-K option: number of fields less than 1");
			} else if (*endp != 0) {
				errx(1, "-K option: illegal number -- %s", optarg);
			}
			gNumOfKeyFields = (unsigned long)nfval;
			break;
		case 'O':
			nfval = strtol(optarg, &endp, 10);
			if (nfval < 1) {
				errx(1, "-O option: number of fields less than 1");
			} else if (*endp != 0) {
				errx(1, "-O option: illegal number -- %s", optarg);
			}
			gNumOfValueFields = (unsigned long)nfval;
			break;
		case 'v':
			version();
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 2) {
		usage();
	}

	ifp = stdin;
	ofp = stdout;

	if (argc > 0) {
		ifp = fopen(argv[0], "r");
		if (ifp == NULL) {
			err(1, "%s", argv[0]);
		}
	}

	if (argc > 1) {
		ofp = fopen(argv[1], "w");
		if (ofp == NULL) {
			err(1, "%s", argv[1]);
		}
	}
	

	if (gKeyFieldList.count == 0) {
		make_fieldlist(&gKeyFieldList, "k", "1");
	}

	if (gValueFieldList.count == 0) {
		make_fieldlist(&gValueFieldList, "o", gReverseMode == 0 ? "2" : "1");
	}

	nn = estimate_maxfieldno(&gKeyFieldList);
	if (gNumOfKeyFields == 0) {
		gNumOfKeyFields = nn;
	} else if (gNumOfKeyFields < nn) {
		errx(1, "-K option: conflict with -k option");
	}

	nn = estimate_maxfieldno(&gValueFieldList);
	if (gNumOfValueFields == 0) {
		gNumOfValueFields = nn;
	} else if (gNumOfValueFields < nn) {
		errx(1, "-O option: conflict with -o option");
	}

	if (gReverseMode == 0) {
		do_pivot(ifp, ofp);
	} else {
		do_unpivot(ifp, ofp);
	}

	return 0;
}

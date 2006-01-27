/*
 * Copyright (c) 2005, 2006 Tama Communications Corporation
 *
 * This file is part of GNU GLOBAL.
 *
 * GNU GLOBAL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * GNU GLOBAL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>

#include "die.h"
#include "gparam.h"
#include "dbop.h"
#include "split.h"
#include "strbuf.h"
#include "strlimcpy.h"
#include "tagsort.h"
#include "varray.h"

/*
 * Internal sort filter.
 *
 * 1. ctags [-x] format
 *
 * 'gtags --sort [--unique]' is equivalent with
 * 'sort -k 1,1 -k 3,3 -k 2,2n [-u]',
 * and 'gtags --ctags --sort [--unique]' is equivalent with
 * 'sort -k 1,1 -k 2,2 -k 3,3n [-u]',
 * but does not need temporary files.
 *
 * - Requirement -
 * (1) input must be ctags [-x] format.
 * (2) input must be sorted in alphabetical order by tag name.
 *
 * 2. path name only format
 *
 * 'gtags --sort --format=path' is equivalent with
 * 'sort -u'.
 * The --format=path option always includes --unique option.
 *
 * Usage:
 *
 * TAGSORT *ts = tagsort_open(stdout, format, unique);
 * 
 * while (<getting new line>)
 * 	tagsort_put(ts, <line>);
 * tagsort_close(ts);
 *
 */
/*
 * This entry corresponds to one record of ctags [-x] format.
 */
struct dup_entry {
        int offset;
        SPLIT ptable;
        const char *path;
        int lineno;
};

static int compare_dup_entry(const void *, const void *);
static void put_lines(int, int, char *, struct dup_entry *, int, FILE *);

/*
 * compare_dup_entry: compare function for sorting dup_entries.
 */
static int
compare_dup_entry(const void *v1, const void *v2)
{
	const struct dup_entry *e1 = v1, *e2 = v2;
	int ret;

	if ((ret = strcmp(e1->path, e2->path)) != 0)
		return ret;
	return e1->lineno - e2->lineno;
}
/*
 * put_lines: sort and print duplicate lines
 *
 *	i)	unique	unique or not
 *			0: sort, 1: sort -u
 *	i)	format	tag format
 *			FORMAT_CTAGS_X: ctags -x format
 *			FORMAT_CTAGS: ctags format
 *	i)	lines	ctags stream
 *	i)	entries	sort target
 *	i)	entry_count number of entry of the entries
 *	i)	op	output file
 */
static void
put_lines(int unique, int format, char *lines, struct dup_entry *entries, int entry_count, FILE *op)
{
	int i;
	char last_path[MAXPATHLEN+1];
	int last_lineno;
	int splits, part_lno, part_path;
	/*
	 * ctags format.
	 *
	 * PART_TAG     PART_PATH     PART_LNO
	 * +----------------------------------------------
	 * |main        src/main      227
	 *
	 * ctags -x format.
	 *
	 * PART_TAG     PART_LNO PART_PATH      PART_LINE
	 * +----------------------------------------------
	 * |main             227 src/main       main()
	 *
	 */
	switch (format) {
	case FORMAT_CTAGS:
		splits = 3;
		part_lno = PART_CTAGS_LNO;
		part_path = PART_CTAGS_PATH;
		break;
	case FORMAT_CTAGS_X:
		splits = 4;
		part_lno = PART_LNO;
		part_path = PART_PATH;
		break;
	default:
		die("internal error in put_lines.");
	}
	/*
	 * Parse and sort ctags [-x] format records.
	 */
	for (i = 0; i < entry_count; i++) {
		char *ctags_x = lines + entries[i].offset;
		SPLIT *ptable = &entries[i].ptable;

		if (split(ctags_x, splits, ptable) < splits) {
			recover(ptable);
			die("too small number of parts.\n'%s'", ctags_x);
		}
		entries[i].lineno = atoi(ptable->part[part_lno].start);
		entries[i].path = ptable->part[part_path].start;
	}
	qsort(entries, entry_count, sizeof(struct dup_entry), compare_dup_entry);
	/*
	 * The variables last_xxx has always the value of previous record.
	 * As for the initial value, it must be a value which does not
	 * appear in actual records.
	 */
	last_path[0] = '\0';
	last_lineno = 0;
	/*
	 * write sorted records.
	 */
	for (i = 0; i < entry_count; i++) {
		struct dup_entry *e = &entries[i];
		int skip = 0;

		if (unique) {
			if (!strcmp(e->path, last_path)) {
				if (e->lineno == last_lineno)
					skip = 1;
				else
					last_lineno = e->lineno;
			} else {
				last_lineno = e->lineno;
				strlimcpy(last_path, e->path, sizeof(last_path));
			}
		}
		recover(&e->ptable);
		if (!skip) {
			fputs(lines + e->offset, op);
			fputc('\n', op);
		}
	}
}
/*
 * check_malloc: memory allocator
 */
static void *check_malloc(int size)
{
	void *p = (void *)malloc(size);
	if (p == NULL)
		die("short of memory.");
	return p;
}
/*
 * tagsort_open: open sort filter
 *
 *	i)	op	output
 *	i)	format	tag format
 *			-1: pass through
 *			FORMAT_CTAGS_X: ctags -x format
 *			FORMAT_CTAGS: ctags format
 *			FORMAT_PATH: path name
 *	i)	unique	1: make the output unique.
 *	r)		tagsort structure
 *
 * If it is not necessary to sort the output, please specify -1 for
 * the argument 'format'. In that case, tagsort_put() is equivalent
 * to fputs(op, line + '\n').
 */
TAGSORT *
tagsort_open(FILE *op, int format, int unique)
{
	TAGSORT *ts = (TAGSORT *)check_malloc(sizeof(TAGSORT));

	switch (format) {
	case -1:		/* do nothing */
		break;
	case FORMAT_PATH:
		ts->dbop = dbop_open(NULL, 1, 0600, 0);
		if (!ts->dbop)
			die("cannot make temporary file in tagsort_open().");
		break;
	case FORMAT_CTAGS:
	case FORMAT_CTAGS_X:
		ts->sb = strbuf_open(MAXBUFLEN);
		ts->vb = varray_open(sizeof(struct dup_entry), 100);
		ts->prev[0] = '\0';
		break;
	default:
		die("tagsort_open() unknown format type.");
	}
	ts->format = format;
	ts->op = op;
	ts->unique = unique;

	return ts;
}
/*
 * tagsort_put: sort the output
 *
 *	i)	ts	tagsort structure
 *	i)	line	record
 */
void
tagsort_put(TAGSORT *ts, char *line)
{
	const char *tag;
	struct dup_entry *entry;
	SPLIT ptable;

	switch (ts->format) {
	case -1:
		fputs(line, ts->op);
		fputc('\n', ts->op);
		break;
	case FORMAT_PATH:
		dbop_put(ts->dbop, line, "");
		break;
	case FORMAT_CTAGS:
	case FORMAT_CTAGS_X:
		/*
		 * extract the tag name.
		 */
		if (split(line, 2, &ptable) < 2) {
			recover(&ptable);
			die("too small number of parts.\n'%s'", line);
		}
		/*
		 * collect the records with the same tag, sort and write
		 * using put_lines().
		 */
		tag = ptable.part[PART_TAG].start;
		if (strcmp(ts->prev, tag) != 0) {
			if (ts->prev[0] != '\0') {
				if (ts->vb->length == 1) {
					fputs(strbuf_value(ts->sb), ts->op);
					fputc('\n', ts->op);
				} else
					put_lines(ts->unique,
						ts->format,
						strbuf_value(ts->sb),
						varray_assign(ts->vb, 0, 0),
						ts->vb->length,
						ts->op);
			}
			strlimcpy(ts->prev, tag, sizeof(ts->prev));
			strbuf_reset(ts->sb);
			varray_reset(ts->vb);
		}
		entry = varray_append(ts->vb);
		entry->offset = strbuf_getlen(ts->sb);
		recover(&ptable);
		strbuf_puts0(ts->sb, line);
		break;
	default:
		die("tagsort_put() unknown format type.");
	}
}
/*
 * tagsort_close: close sort filter
 *
 *	i)	ts	tagsort structure
 */
void
tagsort_close(TAGSORT *ts)
{
	const char *path;

	switch (ts->format) {
	case -1:		/* do nothing */
		break;
	case FORMAT_PATH:
		for (path = dbop_first(ts->dbop, NULL, NULL, DBOP_KEY);
		     path != NULL;
		     path = dbop_next(ts->dbop)) {
			fputs(path, ts->op);
			fputc('\n', ts->op);
		}
		dbop_close(ts->dbop);
		break;
	case FORMAT_CTAGS:
	case FORMAT_CTAGS_X:
		if (ts->prev[0] != '\0') {
			if (ts->vb->length == 1) {
				fputs(strbuf_value(ts->sb), ts->op);
				fputc('\n', ts->op);
			} else
				put_lines(ts->unique,
					ts->format,
					strbuf_value(ts->sb),
					varray_assign(ts->vb, 0, 0),
					ts->vb->length,
					ts->op);
		}
		strbuf_close(ts->sb);
		varray_close(ts->vb);
		break;
	default:
		die("tagsort_close() unknown format type.");
	}
	(void)free(ts);
}
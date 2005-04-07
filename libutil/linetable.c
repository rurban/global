/*
 * Copyright (c) 2002, 2004 Tama Communications Corporation
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef STDC_HEADERS
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif
#include <sys/stat.h>

#include "die.h"
#include "linetable.h"
#include "varray.h"
#include "strbuf.h"

/* File buffer */
#define EXPAND 1024
static STRBUF *ib;
static char *filebuf;
static int filesize;

/* File pointer */
static char *curp;
static char *endp;

/* Offset table */
VARRAY *vb;

static void linetable_put(int, int);
/*
 * linetable_open: load whole of file into memory.
 *
 *	i)	path	path
 *	r)		0: normal
 *			-1: cannot open file.
 */
int
linetable_open(path)
	const char *path;
{
	FILE *ip;
	struct stat sb;
	int lineno, offset;

	if (stat(path, &sb) < 0)
		return -1;
	ib = strbuf_open(sb.st_size);
	vb = varray_open(sizeof(int), EXPAND);
	if ((ip = fopen(path, "r")) == NULL)
		return -1;
	lineno = 1;
	offset = 0;
	for (offset = 0;
		(strbuf_fgets(ib, ip, STRBUF_APPEND), offset != strbuf_getlen(ib));
		offset = strbuf_getlen(ib))
	{
		linetable_put(offset, lineno++);
	}
	fclose(ip);
	curp = filebuf = strbuf_value(ib);
	filesize = offset;
	endp = filebuf + filesize;
	/* strbuf_close(ib); */

	return 0;
}
/*
 * linetable_read: read(2) compatible routine for linetable.
 *
 *	io)	buf	read buffer
 *	i)	size	buffer size
 *	r)		==-1: end of file
 *			!=-1: number of bytes actually read
 */
int
linetable_read(buf, size)
	char *buf;
	int size;
{
	int leaved = endp - curp;

	if (leaved <= 0)
		return -1;	/* EOF */
	if (size > leaved)
		size = leaved;
	memcpy(buf, curp, size);
	curp += size;

	return size;
}
/*
 * linetable_put: put a line into table.
 *
 *	i)	offset	offset of the line
 *	i)	lineno	line number of the line (>= 1)
 */
void
linetable_put(offset, lineno)
	int offset;
	int lineno;
{
	int *entry;

	if (lineno <= 0)
		die("linetable_put: line number must >= 1 (lineno = %d)", lineno);
	entry = varray_assign(vb, lineno - 1, 1);
	*entry = offset;
}
/*
 * linetable_get: get a line from table.
 *
 *	i)	lineno	line number of the line (>= 1)
 *	o)	offset	offset of the line
 *			if offset == NULL, nothing returned.
 *	r)		line pointer
 */
char *
linetable_get(lineno, offset)
	int lineno;
	int *offset;
{
	int addr;

	if (lineno <= 0)
		die("linetable_get: line number must >= 1 (lineno = %d)", lineno);
	addr = *((int *)varray_assign(vb, lineno - 1, 0));
	if (offset)
		*offset = addr;
	return filebuf + addr;
}
/*
 * linetable_close: close line table.
 */
void
linetable_close(void)
{
	varray_close(vb);
	strbuf_close(ib);
}
/*
 * linetable_print: print a line.
 *
 *	i)	op	output file pointer
 *	i)	lineno	line number (>= 1)
 */
void
linetable_print(op, lineno)
	FILE *op;
	int lineno;
{
	char *s, *p;

	if (lineno <= 0)
		die("linetable_print: line number must >= 1 (lineno = %d)", lineno);
	s = linetable_get(lineno, NULL);
	if (s == NULL)
		return;
	for (p = s; *p != '\n'; p++)
		;
	if (p > s)
		fwrite(s, 1, p - s, op);
	fputc('\n', op);
}

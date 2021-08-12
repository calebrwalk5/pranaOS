/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <sys/cdefs.h>

__BEGIN_DECLS

extern char PC;
extern char* UP;
extern char* BC;

int tgetent(char* bp, const char* name);
int tgetflag(const char* id);
int tgetnum(const char* id);
char* tgetstr(const char* id, char** area);
char* tgoto(const char* cap, int col, int row);
int tputs(const char* str, int affcnt, int (*putc)(int));

__END_DECLS

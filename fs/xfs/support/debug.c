/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
 
#include <support/support.h>

#include <asm/page.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/module.h> /* for EXPORT_SYMBOL */

/* XXX we really should be able to move this into DEBUG below */

int		doass = 1;

void
assfail(char *a, char *f, int l)
{
    printk("XFS assertion failed: %s, file: %s, line: %d\n", a, f, l);
    BUG();
}

#ifdef DEBUG

unsigned long
random(void)
{
	static unsigned long	RandomValue = 1;
	/* cycles pseudo-randomly through all values between 1 and 2^31 - 2 */
	register long	rv = RandomValue;
	register long	lo;
	register long	hi;

	hi = rv / 127773;
	lo = rv % 127773;
	rv = 16807 * lo - 2836 * hi;
	if( rv <= 0 ) rv += 2147483647;
	return( RandomValue = rv );
}

int
get_thread_id(void)
{
	return current->pid;
}

EXPORT_SYMBOL(random);
EXPORT_SYMBOL(get_thread_id);

#endif

void
cmn_err(register int level, char *fmt, ...)
{
	char    *fp = fmt;
	char    message[256];
	va_list ap;

	va_start(ap, fmt);
	if (*fmt == '!') fp++;
	vsprintf(message, fp, ap);
	printk("%s\n", message);
	va_end(ap);
}


void
icmn_err(register int level, char *fmt, va_list ap)
{ 
	char	message[256];

	printk("cmn_err level %d ",level);
	vsprintf(message, fmt, ap);
	printk("%s\n", message);
}

EXPORT_SYMBOL(icmn_err);
EXPORT_SYMBOL(cmn_err);
EXPORT_SYMBOL(doass);
EXPORT_SYMBOL(assfail);

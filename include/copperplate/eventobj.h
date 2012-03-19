/*
 * Copyright (C) 2012 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#ifndef _COPPERPLATE_EVENTOBJ_H
#define _COPPERPLATE_EVENTOBJ_H

#include <copperplate/reference.h>

#ifdef CONFIG_XENO_COBALT

#include <pthread.h>

struct eventobj_corespec {
	cobalt_event_t event;
};

struct eventobj_wait_struct {
};

#define EVOBJ_FIFO  COBALT_EVENT_FIFO
#define EVOBJ_PRIO  COBALT_EVENT_PRIO

#define EVOBJ_ALL   COBALT_EVENT_ALL
#define EVOBJ_ANY   COBALT_EVENT_ANY

#else  /* CONFIG_XENO_MERCURY */

#include <copperplate/syncobj.h>

struct eventobj_corespec {
	struct syncobj sobj;
	unsigned long value;
	int flags;
};

struct eventobj_wait_struct {
	unsigned long value;
	int mode;
};

#define EVOBJ_FIFO  0x0
#define EVOBJ_PRIO  0x1

#define EVOBJ_ALL   0x0
#define EVOBJ_ANY   0x1

#endif /* CONFIG_XENO_MERCURY */

struct eventobj {
	struct eventobj_corespec core;
	fnref_type(void (*)(struct eventobj *evobj)) finalizer;
};

#ifdef __cplusplus
extern "C" {
#endif

int eventobj_init(struct eventobj *evobj,
		  unsigned long value, int flags,
		  fnref_type(void (*)(struct eventobj *evobj)) finalizer);

int eventobj_destroy(struct eventobj *evobj);

int eventobj_post(struct eventobj *evobj,
		  unsigned long bits);

int eventobj_wait(struct eventobj *evobj,
		  unsigned long bits,
		  unsigned long *bits_r,
		  int mode,
		  const struct timespec *timeout);

int eventobj_clear(struct eventobj *evobj,
		   unsigned long bits,
		   unsigned long *bits_r);

int eventobj_inquire(struct eventobj *evobj,
		     unsigned long *bits_r);

#ifdef __cplusplus
}
#endif

#endif /* _COPPERPLATE_EVENTOBJ_H */

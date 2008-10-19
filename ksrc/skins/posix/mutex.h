/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _POSIX_MUTEX_H
#define _POSIX_MUTEX_H

#include <asm/xenomai/atomic.h>
#include <pthread.h>

struct pse51_mutex;

union __xeno_mutex {
	pthread_mutex_t native_mutex;
	struct __shadow_mutex {
		unsigned magic;
		unsigned lockcnt;
		struct pse51_mutex *mutex;
#ifdef CONFIG_XENO_FASTSEM
		xnarch_atomic_t lock;
		union {
			unsigned owner_offset;
			xnarch_atomic_t *owner;
		};
		struct pse51_mutexattr attr;
#endif /* CONFIG_XENO_FASTSEM */
	} shadow_mutex;
};

#ifdef __KERNEL__

#include <posix/internal.h>
#include <posix/thread.h>
#include <posix/cb_lock.h>

typedef struct pse51_mutex {
	xnsynch_t synchbase;
	xnholder_t link;            /* Link in pse51_mutexq */

#define link2mutex(laddr)                                               \
	((pse51_mutex_t *)(((char *)laddr) - offsetof(pse51_mutex_t, link)))

	xnarch_atomic_t *owner;
	pthread_mutexattr_t attr;
	unsigned sleepers;
	pse51_kqueues_t *owningq;
} pse51_mutex_t;

extern pthread_mutexattr_t pse51_default_mutex_attr;

void pse51_mutexq_cleanup(pse51_kqueues_t *q);

void pse51_mutex_pkg_init(void);

void pse51_mutex_pkg_cleanup(void);

/* Internal mutex functions, exposed for use by syscall.c. */
int pse51_mutex_timedlock_break(struct __shadow_mutex *shadow,
				int timed, xnticks_t to);

int pse51_mutex_check_init(struct __shadow_mutex *shadow,
			   const pthread_mutexattr_t *attr);

int pse51_mutex_init_internal(struct __shadow_mutex *shadow,
			      pse51_mutex_t *mutex,
			      xnarch_atomic_t *ownerp,
			      const pthread_mutexattr_t *attr);

void pse51_mutex_destroy_internal(pse51_mutex_t *mutex,
				  pse51_kqueues_t *q);

static inline xnthread_t *
pse51_mutex_trylock_internal(xnthread_t *cur,
			     struct __shadow_mutex *shadow, unsigned count)
{
	pse51_mutex_t *mutex = shadow->mutex;
	xnhandle_t ownerh;
	xnthread_t *owner;

	if (xnpod_unblockable_p())
		return ERR_PTR(-EPERM);

	if (!pse51_obj_active(shadow, PSE51_MUTEX_MAGIC, struct __shadow_mutex))
		return ERR_PTR(-EINVAL);

#if XENO_DEBUG(POSIX)
	if (mutex->owningq != pse51_kqueues(mutex->attr.pshared))
		return ERR_PTR(-EPERM);
#endif /* XENO_DEBUG(POSIX) */

	ownerh = xnarch_atomic_cmpxchg(mutex->owner, XN_NO_HANDLE,
				       xnthread_handle(cur));
	if (unlikely(ownerh != XN_NO_HANDLE)) {
		owner = xnthread_lookup(clear_claimed(ownerh));
		if (!owner)
			return ERR_PTR(-EINVAL);
		return owner;
	}

	shadow->lockcnt = count;
	return NULL;
}

/* must be called with nklock locked, interrupts off. */
static inline int pse51_mutex_timedlock_internal(xnthread_t *cur,
						 struct __shadow_mutex *shadow,
						 unsigned count,
						 int timed,
						 xnticks_t abs_to)

{
	pse51_mutex_t *mutex;
	xnthread_t *owner;
	xnhandle_t ownerh, old;
	spl_t s;
	int err;

  retry_lock:
	owner = pse51_mutex_trylock_internal(cur, shadow, count);
	if (likely(!owner) || IS_ERR(owner))
		return PTR_ERR(owner);

	mutex = shadow->mutex;
	if (owner == cur)
		return -EBUSY;

	/* Set bit 0, so that mutex_unlock will know that the mutex is claimed.
	   Hold the nklock, for mutual exclusion with slow mutex_unlock. */
	xnlock_get_irqsave(&nklock, s);
	if (test_claimed(ownerh)) {
		old = xnarch_atomic_get(mutex->owner);
		goto test_no_owner;
	}
	do {
		old = xnarch_atomic_cmpxchg(mutex->owner, ownerh,
					    set_claimed(ownerh, 1));
		if (likely(old == ownerh))
			break;
	  test_no_owner:
		if (old == XN_NO_HANDLE) {
			/* Owner called fast mutex_unlock
			   (on another cpu) */
			xnlock_put_irqrestore(&nklock, s);
			goto retry_lock;
		}
		ownerh = old;
	} while (!test_claimed(ownerh));

	owner = xnthread_lookup(clear_claimed(ownerh));

	if (unlikely(!owner)) {
		err = -EINVAL;
		goto error;
	}

	xnsynch_set_owner(&mutex->synchbase, owner);
	++mutex->sleepers;
	if (timed)
		xnsynch_sleep_on(&mutex->synchbase, abs_to, XN_REALTIME);
	else
		xnsynch_sleep_on(&mutex->synchbase, XN_INFINITE, XN_RELATIVE);
	--mutex->sleepers;

	if (xnthread_test_info(cur, XNBREAK)) {
		err = -EINTR;
		goto error;
	}
	if (xnthread_test_info(cur, XNRMID)) {
		err = -EINVAL;
		goto error;
	}
	if (xnthread_test_info(cur, XNTIMEO)) {
		err = -ETIMEDOUT;
		goto error;
	}

	ownerh = set_claimed(xnthread_handle(cur), mutex->sleepers);
	xnarch_atomic_set(mutex->owner, ownerh);
	shadow->lockcnt = count;
	xnlock_put_irqrestore(&nklock, s);

	return 0;

  error:
	if (!mutex->sleepers)
		xnarch_atomic_set
			(mutex->owner,
			 clear_claimed(xnarch_atomic_get(mutex->owner)));
	xnlock_put_irqrestore(&nklock, s);
	return err;
}

static inline void pse51_mutex_unlock_internal(xnthread_t *cur,
					       pse51_mutex_t *mutex)
{
	xnhandle_t ownerh;
	xnthread_t *owner;
	spl_t s;

	if (likely(xnarch_atomic_cmpxchg(mutex->owner, cur, XN_NO_HANDLE) ==
		   xnthread_handle(cur)))
		return;

	xnlock_get_irqsave(&nklock, s);
	owner = xnsynch_wakeup_one_sleeper(&mutex->synchbase);
	ownerh = set_claimed(xnthread_handle(owner), mutex->sleepers);
	xnarch_atomic_set(mutex->owner, ownerh);
	if (owner)
		xnpod_schedule();
	xnlock_put_irqrestore(&nklock, s);
}

#endif /* __KERNEL__ */

#endif /* !_POSIX_MUTEX_H */

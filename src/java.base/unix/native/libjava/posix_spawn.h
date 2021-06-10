// From https://android.googlesource.com/platform/external/dhcpcd-6.8.2/+/refs/heads/pie-dr1-release/compat/posix_spawn.h
// From https://android.googlesource.com/platform/external/dhcpcd-6.8.2/+/refs/heads/pie-dr1-release/common.h
/*
 * dhcpcd - DHCP client daemon
 * Copyright (c) 2006-2012 Roy Marples <roy@marples.name>
 * All rights reserved
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef POSIX_SPAWN_H
#define POSIX_SPAWN_H

#include <signal.h>
#include <sys/param.h>
#include <sys/time.h>
#include <stdio.h>
#include <syslog.h>

#ifndef HOSTNAME_MAX_LEN
#define HOSTNAME_MAX_LEN	250	/* 255 - 3 (FQDN) - 2 (DNS enc) */
#endif
#ifndef MIN
#define MIN(a,b)		((/*CONSTCOND*/(a)<(b))?(a):(b))
#define MAX(a,b)		((/*CONSTCOND*/(a)>(b))?(a):(b))
#endif
#define UNCONST(a)		((void *)(unsigned long)(const void *)(a))
#define STRINGIFY(a)		#a
#define TOSTRING(a)		STRINGIFY(a)
#define UNUSED(a)		(void)(a)
#define USEC_PER_SEC		1000000L
#define USEC_PER_NSEC		1000L
#define NSEC_PER_SEC		1000000000L
#define MSEC_PER_SEC		1000L
#define MSEC_PER_NSEC		1000000L
/* Some systems don't define timespec macros */
#ifndef timespecclear
#define timespecclear(tsp)      (tsp)->tv_sec = (time_t)((tsp)->tv_nsec = 0L)
#define timespecisset(tsp)      ((tsp)->tv_sec || (tsp)->tv_nsec)
#define timespeccmp(tsp, usp, cmp)                                      \
        (((tsp)->tv_sec == (usp)->tv_sec) ?                             \
            ((tsp)->tv_nsec cmp (usp)->tv_nsec) :                       \
            ((tsp)->tv_sec cmp (usp)->tv_sec))
#define timespecadd(tsp, usp, vsp)                                      \
        do {                                                            \
                (vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec;          \
                (vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec;       \
                if ((vsp)->tv_nsec >= 1000000000L) {                    \
                        (vsp)->tv_sec++;                                \
                        (vsp)->tv_nsec -= 1000000000L;                  \
                }                                                       \
        } while (/* CONSTCOND */ 0)
#define timespecsub(tsp, usp, vsp)                                      \
        do {                                                            \
                (vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;          \
                (vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;       \
                if ((vsp)->tv_nsec < 0) {                               \
                        (vsp)->tv_sec--;                                \
                        (vsp)->tv_nsec += 1000000000L;                  \
                }                                                       \
        } while (/* CONSTCOND */ 0)
#endif
#define timespec_to_double(tv)						     \
	((double)(tv)->tv_sec + (double)((tv)->tv_nsec) / 1000000000.0)
#define timespecnorm(tv) do {						     \
	while ((tv)->tv_nsec >=  NSEC_PER_SEC) {			     \
		(tv)->tv_sec++;						     \
		(tv)->tv_nsec -= NSEC_PER_SEC;				     \
	}								     \
} while (0 /* CONSTCOND */);
#define ts_to_ms(ms, tv) do {						     \
	ms = (tv)->tv_sec * MSEC_PER_SEC;				     \
	ms += (tv)->tv_nsec / MSEC_PER_NSEC;				     \
} while (0 /* CONSTCOND */);
#define ms_to_ts(tv, ms) do {						     \
	(tv)->tv_sec = ms / MSEC_PER_SEC;				     \
	(tv)->tv_nsec = (suseconds_t)(ms - ((tv)->tv_sec * MSEC_PER_SEC))    \
	    * MSEC_PER_NSEC;						     \
} while (0 /* CONSTCOND */);
#ifndef TIMEVAL_TO_TIMESPEC
#define	TIMEVAL_TO_TIMESPEC(tv, ts) do {				\
	(ts)->tv_sec = (tv)->tv_sec;					\
	(ts)->tv_nsec = (tv)->tv_usec * USEC_PER_NSEC;			\
} while (0 /* CONSTCOND */)
#endif

typedef struct {
	short posix_attr_flags;
#define POSIX_SPAWN_SETSIGDEF		0x10
#define POSIX_SPAWN_SETSIGMASK		0x20
	sigset_t posix_attr_sigmask;
	sigset_t posix_attr_sigdefault;
} posix_spawnattr_t;

typedef struct {
//	int unused;
} posix_spawn_file_actions_t;

int posix_spawn(pid_t *, const char *,
    const posix_spawn_file_actions_t *, const posix_spawnattr_t *,
    char *const [], char *const []);
int posix_spawnattr_init(posix_spawnattr_t *);
int posix_spawnattr_setflags(posix_spawnattr_t *, short);
int posix_spawnattr_setsigmask(posix_spawnattr_t *, const sigset_t *);
int posix_spawnattr_setsigdefault(posix_spawnattr_t *, const sigset_t *);

#endif

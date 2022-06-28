// This file contains some definitions left over from autoconf's reign of terror.
// We will be slowly phasing this out.

#ifndef CTDLDIR
#error CTDLDIR is not defined , did we not run configure ?
#endif

#define ENABLE_NLS
#define F_PID_T "%d"
#define F_UID_T "%d"
#define F_XPID_T "%x"
#define GETPGRP_VOID 1
#define HAVE_ARPA_NAMESER_COMPAT_H 1
#define HAVE_GETPWNAM_R 1
#define HAVE_GETPWUID_R 1
#define HAVE_GETSPNAM 1
#define HAVE_ICONV
#define HAVE_MEMORY_H 1
#define HAVE_PTHREAD_H 1
#define HAVE_RESOLV_H
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRUCT_TM_TM_GMTOFF 1
#define HAVE_STRUCT_UCRED 1
#define HAVE_SYSCALL_H 1
#define HAVE_SYS_PRCTL_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_SYSCALL_H 1
#define LOCALEDIR "/root/citadel/citadel"
#define RETSIGTYPE void
#define SIZEOF_CHAR 1
#define SIZEOF_INT 4
#define SIZEOF_LOFF_T 8
#define SIZEOF_LONG 8
#define SIZEOF_SHORT 2
#define SIZEOF_SIZE_T 8
#define SSL_DIR "/root/citadel/citadel/keys"
#define STDC_HEADERS 1
#define TIME_WITH_SYS_TIME 1

// Enable GNU extensions on systems that have them.
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif

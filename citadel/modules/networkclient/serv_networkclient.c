/*
 * This module polls other Citadel servers for inter-site networking.
 *
 * Copyright (c) 2000-2017 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * ** NOTE **   A word on the S_NETCONFIGS semaphore:
 * This is a fairly high-level type of critical section.  It ensures that no
 * two threads work on the netconfigs files at the same time.  Since we do
 * so many things inside these, here are the rules:
 *  1. begin_critical_section(S_NETCONFIGS) *before* begin_ any others.
 *  2. Do *not* perform any I/O with the client during these sections.
 *
 */


#include "sysdep.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#ifdef HAVE_SYSCALL_H
# include <syscall.h>
#else 
# if HAVE_SYS_SYSCALL_H
#  include <sys/syscall.h>
# endif
#endif

#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include <libcitadel.h>
#include "citadel.h"
#include "server.h"
#include "citserver.h"
#include "support.h"
#include "config.h"
#include "user_ops.h"
#include "database.h"
#include "msgbase.h"
#include "internet_addressing.h"
#include "clientsocket.h"
#include "citadel_dirs.h"
#include "threads.h"
#include "context.h"
#include "ctdl_module.h"

struct CitContext networker_client_CC;


/*
 * Poll one Citadel node (the Citadel node name, host/ip, port number, and shared secret are supplied by the caller)
 */
void network_poll_node(StrBuf *node, StrBuf *host, StrBuf *port, StrBuf *secret)
{
	char buf[SIZ];
	CC->SBuf.Buf = NewStrBuf();
	CC->sMigrateBuf = NewStrBuf();
	CC->SBuf.ReadWritePointer = NULL;
	int bytes_read = 0;
	int bytes_total = 0;
	int this_block = 0;
	StrBuf *SpoolFileName = NULL;

	syslog(LOG_DEBUG, "netpoll: polling %s at %s:%s", ChrPtr(node), ChrPtr(host), ChrPtr(port));

	int sock = sock_connect((char *)ChrPtr(host), (char *)ChrPtr(port));
	if (sock < 0) {
		syslog(LOG_ERR, "%s: %s", ChrPtr(host), strerror(errno));
		return;
	}

	/* Read the server greeting */
        if (sock_getln(&sock, buf, sizeof buf) < 0) {
                goto bail;
        }

	/* Check that the remote is who we think it is and warn the site admin if not */
	if (strncmp(&buf[4], ChrPtr(node), StrLength(node))) {
		CtdlAideMessage(buf, "Connected to wrong node!");
		syslog(LOG_ERR, "netpoll: was expecting node <%s> but got %s", ChrPtr(node), buf);
		goto bail;
	}

	/* We're talking to the correct node.  Now identify ourselves. */
	snprintf(buf, sizeof buf, "NETP %s|%s", CtdlGetConfigStr("c_nodename"), ChrPtr(secret));
	sock_puts(&sock, buf);
        if (sock_getln(&sock, buf, sizeof buf) < 0) {
                goto bail;
        }
	if (buf[0] != '2') {
		CtdlAideMessage(buf, "Could not authenticate to network peer");
		syslog(LOG_ERR, "netpoll: could not authenticate to <%s> : %s", ChrPtr(node), buf);
		goto bail;
	}

	/* Tell it we want to download anything headed our way. */
	sock_puts(&sock, "NDOP");
        if (sock_getln(&sock, buf, sizeof buf) < 0) {
                goto bail;
        }
	if (buf[0] != '2') {
		CtdlAideMessage(buf, "NDOP error");
		syslog(LOG_ERR, "netpoll: NDOP error talking to <%s> : %s", ChrPtr(node), buf);
		goto bail;
	}

	bytes_total = atoi(&buf[4]);
	bytes_read = 0;

	SpoolFileName = NewStrBuf();
	StrBufPrintf(SpoolFileName,			// Incoming packets get dropped into the "spoolin/" directory
		"%s/%s.%lx%x",
		ctdl_netin_dir,
		ChrPtr(node),
		time(NULL),
		rand()
	);
	StrBufStripSlashes(SpoolFileName, 1);

	FILE *netinfp = fopen(ChrPtr(SpoolFileName), "w");
	FreeStrBuf(&SpoolFileName);
	if (!netinfp) {
		goto bail;
	}

	while (bytes_read < bytes_total) {
		snprintf(buf, sizeof buf, "READ %d|%d", bytes_read, bytes_total-bytes_read);
		sock_puts(&sock, buf);
        	if (sock_getln(&sock, buf, sizeof buf) < 0) {
			fclose(netinfp);
                	goto bail;
        	}
		if (buf[0] == '6') {
			this_block = atoi(&buf[4]);

			// Use buffered reads to download the data from remote server
			StrBuf *ThisBlockBuf = NewStrBuf();
			int blen = socket_read_blob(&sock, ThisBlockBuf, this_block, 20);
			if (blen > 0) {
				fwrite(ChrPtr(ThisBlockBuf), blen, 1, netinfp);
				bytes_read += blen;
			}
			FreeStrBuf(&ThisBlockBuf);
			if (blen < this_block) {
				syslog(LOG_DEBUG, "netpoll: got short block, ftn");
				fclose(netinfp);
				goto bail;
			}
		}
	}

	syslog(LOG_DEBUG, "netpoll: downloaded %d of %d bytes from %s", bytes_read, bytes_total, ChrPtr(node));

	if (fclose(netinfp) == 0) {
		sock_puts(&sock, "CLOS");				// CLOSing the download causes it to be deleted on the other node
        	if (sock_getln(&sock, buf, sizeof buf) < 0) {
                	goto bail;
        	}
	}

	// Now get ready to send our network data to the other node.
	SpoolFileName = NewStrBuf();
	StrBufPrintf(SpoolFileName,			// Outgoing packets come from the "spoolout/" directory
		"%s/%s",
		ctdl_netout_dir,
		ChrPtr(node)
	);
	FILE *netoutfp = fopen(ChrPtr(SpoolFileName), "w");
	FreeStrBuf(&SpoolFileName);
	if (!netoutfp) {
		goto bail;
	}
	fclose(netoutfp);
	//unlink(netoutfp);

bail:	close(sock);
	FreeStrBuf(&CC->SBuf.Buf);
	FreeStrBuf(&CC->sMigrateBuf);
}


/*
 * Poll other Citadel nodes and transfer inbound/outbound network data.
 * Set "full" to nonzero to force a poll of every node, or to zero to poll
 * only nodes to which we have data to send.
 */
void network_poll_other_citadel_nodes(int full_poll, HashList *ignetcfg)
{
	const char *key;
	long len;
	HashPos *Pos;
	void *vCfg;
	StrBuf *SpoolFileName;
	
	int poll = 0;
	
	if (GetCount(ignetcfg) ==0) {
		syslog(LOG_DEBUG, "netpoll: no neighbor nodes are configured - not polling");
		return;
	}
	become_session(&networker_client_CC);

	SpoolFileName = NewStrBufPlain(ctdl_netout_dir, -1);

	Pos = GetNewHashPos(ignetcfg, 0);

	while (GetNextHashPos(ignetcfg, Pos, &len, &key, &vCfg))
	{
		/* Use the string tokenizer to grab one line at a time */
		if (server_shutting_down) {
			return;
		}
		CtdlNodeConf *pNode = (CtdlNodeConf*) vCfg;
		poll = 0;
		
		StrBuf *node = NewStrBufDup(pNode->NodeName);
		StrBuf *host = NewStrBufDup(pNode->Host);
		StrBuf *port = NewStrBufDup(pNode->Port);
		StrBuf *secret = NewStrBufDup(pNode->Secret);
		
		if ( (StrLength(node) != 0) && 
		     (StrLength(secret) != 0) &&
		     (StrLength(host) != 0)
		) {
			poll = full_poll;
			if (poll == 0)
			{
				StrBufAppendBufPlain(SpoolFileName, HKEY("/"), 0);
				StrBufAppendBuf(SpoolFileName, node, 0);
				StrBufStripSlashes(SpoolFileName, 1);
				
				if (access(ChrPtr(SpoolFileName), R_OK) == 0) {
					poll = 1;
				}
			}
		}
		if (poll && (StrLength(host) > 0) && strcmp("0.0.0.0", ChrPtr(host))) {
			if (!CtdlNetworkTalkingTo(SKEY(node), NTT_CHECK)) {
				network_poll_node(node, host, port, secret);
			}
		}
		FreeStrBuf(&node);
		FreeStrBuf(&host);
		FreeStrBuf(&secret);
		FreeStrBuf(&port);
	}
	FreeStrBuf(&SpoolFileName);
	DeleteHashPos(&Pos);
}


void network_do_clientqueue(void)
{
	HashList *working_ignetcfg;
	int full_processing = 1;
	static time_t last_run = 0L;

	/*
	 * Run the full set of processing tasks no more frequently
	 * than once every n seconds
	 */
	if ( (time(NULL) - last_run) < CtdlGetConfigLong("c_net_freq") )
	{
		full_processing = 0;
		syslog(LOG_DEBUG, "netpoll: full processing in %ld seconds.",
			CtdlGetConfigLong("c_net_freq") - (time(NULL)- last_run)
		);
	}

	working_ignetcfg = CtdlLoadIgNetCfg();
	/*
	 * Poll other Citadel nodes.  Maybe.  If "full_processing" is set
	 * then we poll everyone.  Otherwise we only poll nodes we have stuff
	 * to send to.
	 */
	network_poll_other_citadel_nodes(full_processing, working_ignetcfg);
	DeleteHash(&working_ignetcfg);
}

/*
 * Module entry point
 */
CTDL_MODULE_INIT(network_client)
{
	if (!threading)
	{
		CtdlFillSystemContext(&networker_client_CC, "CitNetworker");
		CtdlRegisterSessionHook(network_do_clientqueue, EVT_TIMER, PRIO_SEND + 10);

	}
	return "networkclient";
}

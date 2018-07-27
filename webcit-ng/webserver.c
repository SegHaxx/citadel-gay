/*
 * webserver.c
 *
 * This module handles the task of setting up a listening socket, accepting
 * connections, and dispatching active connections onto a pool of worker
 * threads.
 *
 * Copyright (c) 1996-2018 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "webcit.h"			// All other headers are included from this header.

int num_threads_executing = 1;		// Number of worker threads currently bound to a connected client
int num_threads_existing = 1;		// Total number of worker threads that exist right now
int is_https = 0;			// Set to nonzero if we are running as an HTTPS server today.
static void *original_brk = NULL;	// Remember the original program break so we can test for leaks


/*
 * Spawn an additional worker thread into the pool.
 */
void spawn_another_worker_thread(int *pointer_to_master_socket)
{
	pthread_t th;		// Thread descriptor
	pthread_attr_t attr;	// Thread attributes

	/* set attributes for the new thread */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&attr, 1048576);	// Large stacks to prevent MIME parser crash on FreeBSD

	/* now create the thread */
	if (pthread_create(&th, &attr, (void *(*)(void *)) worker_entry, (void *) pointer_to_master_socket) != 0) {
		syslog(LOG_WARNING, "Can't create thread: %s", strerror(errno));
	} else {
		++num_threads_existing;
		++num_threads_executing;
	}

	/* free up the attributes */
	pthread_attr_destroy(&attr);
}


/*
 * Entry point for worker threads
 */
void worker_entry(int *pointer_to_master_socket)
{
	int master_socket = *pointer_to_master_socket;
	int i = 0;
	int fail_this_transaction = 0;
	struct client_handle ch;

	while (1) {
		/* Each worker thread blocks on accept() while waiting for something to do. */
		memset(&ch, 0, sizeof ch);
		ch.sock = -1;
		errno = EAGAIN;
		do {
			--num_threads_executing;
			syslog(LOG_DEBUG, "Additional memory allocated since startup: %d bytes", (int) (sbrk(0) - original_brk));
			syslog(LOG_DEBUG, "Thread 0x%x calling accept() on master socket %d", (unsigned int) pthread_self(),
			       master_socket);
			ch.sock = accept(master_socket, NULL, 0);
			if (ch.sock < 0) {
				syslog(LOG_DEBUG, "accept() : %s", strerror(errno));
			}
			++num_threads_executing;
			syslog(LOG_DEBUG, "socket %d is awake , threads executing: %d , threads total: %d", ch.sock,
			       num_threads_executing, num_threads_existing);
		} while ((master_socket > 0) && (ch.sock < 0));

		/* If all threads are executing, spawn more, up to the maximum */
		if ((num_threads_executing >= num_threads_existing) && (num_threads_existing <= MAX_WORKER_THREADS)) {
			spawn_another_worker_thread(pointer_to_master_socket);
		}

		/* We have a client.  Do some work. */

		/* Set the SO_REUSEADDR socket option */
		i = 1;
		setsockopt(ch.sock, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));

		/* If we are an HTTPS server, go crypto now. */
		if (is_https) {
			starttls(&ch);
			if (ch.ssl_handle == NULL) {
				fail_this_transaction = 1;
			}
		} else {
			int fdflags;
			fdflags = fcntl(ch.sock, F_GETFL);
			if (fdflags < 0) {
				syslog(LOG_WARNING, "unable to get server socket flags! %s", strerror(errno));
			}
		}

		/* Perform an HTTP transaction... */
		if (fail_this_transaction == 0) {
			perform_one_http_transaction(&ch);
		}

		/* Shut down SSL/TLS if required... */
		if (is_https) {
			endtls(&ch);
		}
		/* ...and close the socket. */
		//syslog(LOG_DEBUG, "Closing socket %d...", ch.sock);
		//lingering_close(ch.sock);
		close(ch.sock);
		syslog(LOG_DEBUG, "Closed socket %d.", ch.sock);
	}
}


/*
 * Start up a TCP HTTP[S] server on the requested port
 */
int webserver(char *webserver_interface, int webserver_port, int webserver_protocol)
{
	int master_socket = (-1);
	original_brk = sbrk(0);

	switch (webserver_protocol) {
	case WEBSERVER_HTTP:
		syslog(LOG_DEBUG, "Starting HTTP server on %s:%d", webserver_interface, webserver_port);
		master_socket = webcit_tcp_server(webserver_interface, webserver_port, 10);
		break;
	case WEBSERVER_HTTPS:
		syslog(LOG_DEBUG, "Starting HTTPS server on %s:%d", webserver_interface, webserver_port);
		master_socket = webcit_tcp_server(webserver_interface, webserver_port, 10);
		init_ssl();
		is_https = 1;
		break;
	default:
		syslog(LOG_ERR, "unknown protocol");
		;;
	}

	if (master_socket < 1) {
		syslog(LOG_ERR, "Unable to bind the web server listening socket");
		return (1);
	}

	syslog(LOG_INFO, "Listening on socket %d", master_socket);
	signal(SIGPIPE, SIG_IGN);

	worker_entry(&master_socket);	// this thread becomes a worker
	return (0);
}

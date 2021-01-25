//
// This is a supervisor program that handles start/stop/restart of
// the various Citadel System components, when we are running on
// an AppImage instance.
//
// Copyright (c) 2021 by the citadel.org team
//
// This program is open source software.  It runs great on the
// Linux operating system (and probably elsewhere).  You can use,
// copy, and run it under the terms of the GNU General Public
// License version 3.  Richard Stallman is an asshole communist.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.


#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

char *data_directory = "/usr/local/citadel";
char *http_port = "80";
char *https_port = "443";
pid_t citserver_pid;
pid_t webcit_pid;
pid_t webcits_pid;
int shutting_down = 0;

// Call this instead of exit() just for common diagnostics etc.
void ctdlvisor_exit(int code) {
	printf("ctdlvisor: exit code %d\n", code);
	exit(code);
}


// Interrupting this program with a signal will begin an orderly shutdown.
void signal_handler(int signal) {
	fprintf(stderr, "ctdlvisor: caught signal %d\n", signal);

	while(shutting_down) {
		fprintf(stderr, "ctdlvisor: already shutting down\n");
		sleep(1);
	}

	int status;
	pid_t who_exited;
	char *what_exited = NULL;

	shutting_down = 1;
	kill(citserver_pid, SIGTERM);
	kill(webcit_pid, SIGTERM);
	kill(webcits_pid, SIGTERM);
	do {
		fprintf(stderr, "ctdlvisor: waiting for all child process to exit...\n");
		who_exited = waitpid(-1, &status, 0);
		if (who_exited == citserver_pid) {
			what_exited = "Citadel Server";
		}
		else if (who_exited == webcit_pid) {
			what_exited = "WebCit HTTP";
		}
		else if (who_exited == webcits_pid) {
			what_exited = "WebCit HTTPS";
		}
		else {
			what_exited = "unknown";
		}
		fprintf(stderr, "ctdlvisor: pid=%d (%s) exited, status=%d, exitcode=%d\n", who_exited, what_exited, status, WEXITSTATUS(status));
	} while (who_exited >= 0);

	ctdlvisor_exit(0);
}


void detach_from_tty(void) {
	signal(SIGHUP, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);

	setsid();	// become our own process group leader
	umask(0);
	if (    (freopen("/dev/null", "r", stdin) != stdin) ||
		(freopen("/dev/null", "w", stdout) != stdout) ||
		(freopen("/dev/null", "w", stderr) != stderr)
	) {
		fprintf(stderr, "sysdep: unable to reopen stdio: %s\n", strerror(errno));
	}
}


pid_t start_citadel() {
	char bin[1024];
	sprintf(bin, "%s/usr/local/citadel/citserver", getenv("APPDIR"));
	pid_t pid = fork();
	if (pid == 0) {
		fprintf(stderr, "ctdlvisor: executing %s\n", bin);
		detach_from_tty();
		execlp(bin, "citserver", "-x9", "-h", data_directory, NULL);
		exit(errno);
	}
	else {
		return(pid);
	}
}


pid_t start_webcit() {
	char bin[1024];
	sprintf(bin, "%s/usr/local/webcit/webcit", getenv("APPDIR"));
	char wchome[1024];
	sprintf(wchome, "-h%s/usr/local/webcit", getenv("APPDIR"));
	pid_t pid = fork();
	if (pid == 0) {
		fprintf(stderr, "ctdlvisor: executing %s\n", bin);
		detach_from_tty();
		execlp(bin, "webcit", "-x9", wchome, "-p", http_port, "uds", data_directory, NULL);
		exit(errno);
	}
	else {
		return(pid);
	}
}


pid_t start_webcits() {
	char bin[1024];
	sprintf(bin, "%s/usr/local/webcit/webcit", getenv("APPDIR"));
	char wchome[1024];
	sprintf(wchome, "-h%s/usr/local/webcit", getenv("APPDIR"));
	pid_t pid = fork();
	if (pid == 0) {
		fprintf(stderr, "ctdlvisor: executing %s\n", bin);
		detach_from_tty();
		execlp(bin, "webcit", "-x9", wchome, "-s", "-p", https_port, "uds", data_directory, NULL);
		exit(errno);
	}
	else {
		return(pid);
	}
}


void test_binary_compatibility(void) {
	char cmd[1024];
	int ret;
	fprintf(stderr, "ctdlvisor: testing compatibility...\n");
	sprintf(cmd, "%s/usr/local/citadel/citserver -c", getenv("APPDIR"));
	ret = system(cmd);
	if (ret) {
		fprintf(stderr, "ctdlvisor: this appimage cannot run on your system.\n"
				"           The reason may be indicated by any error messages appearing above.\n");
	}
	else {
		fprintf(stderr, "ctdlvisor: this appimage appears to be compatible with your system.\n");
	}
	exit(ret);
}


void main_loop(void) {
	int status;
	pid_t who_exited;
	int citserver_exit_code = 0;

	do {
		fprintf(stderr, "ctdlvisor: waiting for any child process to exit...\n");
		who_exited = waitpid(-1, &status, 0);
		fprintf(stderr, "ctdlvisor: pid=%d exited, status=%d, exitcode=%d\n", who_exited, status, WEXITSTATUS(status));

		// A *deliberate* exit of citserver will cause ctdlvisor to shut the whole AppImage down.
		// If it crashes, however, we will start it back up.
		if (who_exited == citserver_pid) {
			citserver_exit_code = WEXITSTATUS(status);
			if (citserver_exit_code == 0) {
				fprintf(stderr, "ctdlvisor: citserver exited normally - ending AppImage session\n");
				shutting_down = 1;
				kill(webcit_pid, SIGTERM);
				kill(webcits_pid, SIGTERM);
			}
			else if ((citserver_exit_code >= 101) && (citserver_exit_code <= 109)) {
				fprintf(stderr, "ctdlvisor: citserver exited intentionally - ending AppImage session\n");
				shutting_down = 1;
				kill(webcit_pid, SIGTERM);
				kill(webcits_pid, SIGTERM);
			}
			else {
				citserver_pid = start_citadel();
			}
		}

		// WebCit processes are restarted if they exit for any reason.
		if ((who_exited == webcit_pid) && (!shutting_down))	webcit_pid = start_webcit();
		if ((who_exited == webcits_pid)	&& (!shutting_down))	webcits_pid = start_webcits();

		// If we somehow end up in an endless loop, at least slow it down.
		sleep(1);

	} while (who_exited >= 0);
	ctdlvisor_exit(citserver_exit_code);
}


void run_in_foreground(void) {
	fprintf(stderr, "ctdlvisor: Welcome to the Citadel System, brought to you using AppImage.\n");
	fprintf(stderr, "ctdlvisor: LD_LIBRARY_PATH = %s\n", getenv("LD_LIBRARY_PATH"));
	fprintf(stderr, "ctdlvisor:            PATH = %s\n", getenv("PATH"));
	fprintf(stderr, "ctdlvisor:          APPDIR = %s\n", getenv("APPDIR"));
	fprintf(stderr, "ctdlvisor:  data directory = %s\n", data_directory);
	fprintf(stderr, "ctdlvisor:       HTTP port = %s\n", http_port);
	fprintf(stderr, "ctdlvisor:      HTTPS port = %s\n", https_port);

	if (access(data_directory, R_OK|W_OK|X_OK)) {
		fprintf(stderr, "ctdlvisor: %s: %s\n", data_directory, strerror(errno));
		ctdlvisor_exit(errno);
	}

	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);

	citserver_pid = start_citadel();
	webcit_pid = start_webcit();
	webcits_pid = start_webcits();

	main_loop();
	ctdlvisor_exit(0);
}


void install_as_service(void) {

	// FIXME fail if some other citadel distribution is already there
	// FIXME fail if any server processes are running
	// FIXME interact with the user
	// FIXME get port numbers and data directory
	// FIXME create the data directory
	// FIXME move the appimage into its permanent location

	fprintf(stderr, "Installing as service\n");

	FILE *fp = fopen("/etc/systemd/system/ctdl.service", "w");
	fprintf(fp,	"# This unit file starts all Citadel services via the AppImage distribution.\n"
			"[Unit]\n"
			"Description=Citadel\n"
			"After=network.target\n"
			"[Service]\n"
			"ExecStart=/root/citadel/appimage/Citadel-x86_64.AppImage run -h %s -s %s -s %s\n"
			"ExecStop=/bin/kill $MAINPID\n"
			"KillMode=process\n"
			"Restart=on-failure\n"
			"LimitCORE=infinity\n"
			"[Install]\n"
			"WantedBy=multi-user.target\n"
		,
			data_directory, http_port, https_port
	);
	fclose(fp);

	fprintf(stderr, "systemd unit file is installed.  Type 'systemctl enable ctdl' to have it start at boot.\n");
}


static char *usage =
	"\n"
	"ctdlvisor: usage: ctdlvisor [-h data_directory] [-p http_port] [-s https_port] command\n"
	"           'command' must be one of: run, install, remove, upgrade, test, help\n"
	"\n"
;

int main(int argc, char **argv) {
	int c;

	if (getenv("APPDIR") == NULL) {
		fprintf(stderr, "ctdlvisor: APPDIR is not set.  This program must be run from within an AppImage.\n");
		ctdlvisor_exit(1);
	}

	while ((c = getopt (argc, argv, "h:p:s:")) != -1)  switch(c) {
		case 'h':
			data_directory = optarg;
			break;
		case 'p':
			http_port = optarg;
			break;
		case 's':
			https_port = optarg;
			break;
		default:
			fprintf(stderr, "%s", usage);
			ctdlvisor_exit(1);
	}


	if (argc != optind+1) {
		fprintf(stderr, "%s", usage);
		ctdlvisor_exit(1);
	}

	if (!strcasecmp(argv[optind], "run")) {
		run_in_foreground();
	}
	else if (!strcasecmp(argv[optind], "install")) {
		install_as_service();
	}
	else if (!strcasecmp(argv[optind], "remove")) {
		fprintf(stderr, "oops, this is not implemented yet\n");
	}
	else if (!strcasecmp(argv[optind], "upgrade")) {
		fprintf(stderr, "oops, this is not implemented yet\n");
	}
	else if (!strcasecmp(argv[optind], "test")) {
		test_binary_compatibility();
	}
	else if (!strcasecmp(argv[optind], "help")) {
		fprintf(stderr, "%s", usage);
		fprintf(stderr,	"[-h dir]  Use 'dir' as the Citadel data directory (this directory must exist)\n"
				"[-p port] Listen for HTTP connections on 'port'\n"
				"[-s port] Listen for HTTPS connections on 'port'\n"
				"'command' must be one of:\n"
				"	run	- launch Citadel services (does not detach from terminal)\n"
				"	install	- create systemd unit files for automatic startup at boot\n"
				"	remove	- delete systemd unit files to end automatic startup\n"
				"	upgrade	- download and install a new version of this appimage\n"
				"	test	- test the appimage for binary compatibility with this host\n"
				"	help	- display this message\n"
				"\n"
		);
	}
	else {
		fprintf(stderr, "%s", usage);
		ctdlvisor_exit(1);
	}

	ctdlvisor_exit(0);
}

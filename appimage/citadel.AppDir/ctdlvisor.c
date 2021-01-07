#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>




pid_t start_citadel() {
	pid_t pid = fork();
	if (pid == 0) {
		execlp("citserver", "citserver", "-x9", "-h/usr/local/citadel", NULL);
		exit(1);
	}
	else {
		return(pid);
	}
}


pid_t start_webcit() {
	pid_t pid = fork();
	if (pid == 0) {
		execlp("/bin/bash", "bash", "-c", "sleep 7", NULL);
		exit(1);
	}
	else {
		return(pid);
	}
}


pid_t start_webcits() {
	pid_t pid = fork();
	if (pid == 0) {
		execlp("/bin/bash", "bash", "-c", "sleep 9", NULL);
		exit(1);
	}
	else {
		return(pid);
	}
}





main() {
	int status;
	pid_t who_exited;

	pid_t citserver_pid = start_citadel();
	//pid_t webcit_pid = start_webcit();
	//pid_t webcits_pid = start_webcits();

	do {
		printf("LD_LIBRARY_PATH = %s\n", getenv("LD_LIBRARY_PATH"));
		printf("PATH = %s\n", getenv("PATH"));


		printf("waiting...\n");
		who_exited = waitpid(-1, &status, 0);
		printf("pid=%d exited, status=%d\n", who_exited, status);

		if (who_exited == citserver_pid)	citserver_pid = start_citadel();
		//if (who_exited == webcit_pid)		webcit_pid = start_webcit();
		//if (who_exited == webcits_pid)	webcits_pid = start_webcits();

		sleep(1);				// for sanity

	} while(who_exited >= 0);
	exit(0);
}

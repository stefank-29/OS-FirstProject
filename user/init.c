// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "kernel/fcntl.h"

char *argv[] = { "sh", 0 };

int
main(void)
{
	int pid, wpid;

	if(getpid() != 1){
		fprintf(2, "init: already running\n");
		exit();
	}

	if(open("/dev/console", O_RDWR) < 0){ // otvara konzolu za citanje i pisanje
		mknod("/dev/console", 1, 1); // pravi se uredjaj (major=1, minor=1)
		open("/dev/console", O_RDWR); // stavi /dev/console na ofile[0]
	}
	// otvore se jos 2 /dev/console (ofile[1] i ofile[2])
	dup(0);  // stdout
	dup(0);  // stderr

	for(;;){
		printf("init: starting sh\n");
		pid = fork();
		if(pid < 0){
			printf("init: fork failed\n");
			exit();
		}
		if(pid == 0){
			exec("/bin/sh", argv);
			printf("init: exec sh failed\n");
			exit();
		}
		while((wpid=wait()) >= 0 && wpid != pid)
			printf("zombie!\n");
	}
}

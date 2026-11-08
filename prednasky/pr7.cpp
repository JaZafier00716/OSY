#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <sys/mman.h>

sem_t *g_sem;

int main (int t_argn, char **t_args) {
    if (t_argn > 1) {
        if (strcmp(t_args[1], "-u") == 0) {
            sem_unlink("/semaforek");
        }
    }

    g_sem = sem_open("/semaforek", O_RDWR | O_CREAT, 0600, 1);
    if (t_argn > 1) {
        if (strcmp(t_args[1], "-i") == 0) {
            sem_init(g_sem, 1, 1);
        }
    }

    int shm_fd = shm_open("/citac", O_RDWR | O_CREAT, 0600); // Creates new shared memory
    
    ftruncate(shm_fd, sizeof(int)); // nafouknuti pameti

    int *counter = (int*) mmap(nullptr /*addr*/, sizeof(int) /*delka*/, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0 /*offset*/);


    // sem_init(g_sem, 0, 1);
	fork();
	while(1) {
        sem_wait(g_sem);
		printf("(%d) ", getpid());
        for (int i = 0; i < 10; i++) {
            printf("%d ", rand() % 1000);
            fflush(stdout);
        }
        (*counter)++;
        printf("[%d]\n", *counter);
        sem_post(g_sem);
    }

    return 0;
}

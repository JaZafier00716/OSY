#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <unistd.h>

int main() {
    int mq = mq_open("/fronta", O_CREAT | O_RDWR, 0600, nullptr);

    printf("mq_open: %d\n", mq);

    if (fork() == 0) {
        // Child process
        while (1) {
            char buf[128];
            sprintf(buf, "(%d) %d\n", getpid(), rand() % 100);
            mq_send(mq, buf, strlen(buf), 0);
            sleep(1);
        }
    }

    // parent process
    // to fix message too long error, we need to get the maximum message size from the queue attributes
    struct mq_attr mqdata;
    mq_getattr(mq, &mqdata);

    // char buf[333];
    char buf[mqdata.mq_msgsize];
    while (1) {
        unsigned int prio;
        int ret = mq_receive(mq, buf, sizeof(buf), &prio);
        if (ret < 0) {
            printf("ret = %d %d %s\n", ret, errno, strerror(errno)); // ret = -1 90 Message too long
            break;
        }
        write(1, buf, ret);
    }
}
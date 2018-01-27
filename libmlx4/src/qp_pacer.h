#ifndef QP_PACER_H
#define QP_PACER_H

#include "pacer.h"
#include <immintrin.h> /* For _mm_pause */  // remember to take off this header file and __mm_pause() when running on ConFlux

static inline void cpu_relax() __attribute__((always_inline));
static inline void cpu_relax() {
#if (COMPILER == MVCC)
    _mm_pause();
#elif (COMPILER == GCC || COMPILER == LLVM)
    asm("pause");
#endif
}

static void contact_pacer_read() {
    /* prepare unix domain socket */
    unsigned int s, len;
    struct sockaddr_un remote;
    char str[MSG_LEN];

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    printf("Contacting pacer...\n");

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, SOCK_PATH);
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(s, (struct sockaddr *)&remote, len) == -1) {
        perror("connect");
        exit(1);
    }
   
    printf("Sending read message...\n");
    strcpy(str, "read");
    if (send(s, str, strlen(str), 0) == -1) {
        perror("send: read");
        exit(1);
    }

    close(s);
}
#endif

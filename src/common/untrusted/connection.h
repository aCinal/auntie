

#ifndef __SRC_COMMON_UNTRUSTED_CONNECTION_H
#define __SRC_COMMON_UNTRUSTED_CONNECTION_H

#include <netinet/in.h>

struct connection {
    int fd;
    struct sockaddr_in peer;
};

#endif /* __SRC_COMMON_UNTRUSTED_CONNECTION_H */

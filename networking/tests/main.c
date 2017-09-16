#include <stdio.h>
#include <stdlib.h>
#include "tcp.h"



int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s role\n", argv[0]);
        return -1;
    }

    NET_tcpTest(atoi(argv[1]));

    return 0;
}

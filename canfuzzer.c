#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <linux/can.h>
#include "lib.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <time.h>
#include <unistd.h>

/* At time of writing, these constants are not defined in the headers */
#ifndef PF_CAN
#define PF_CAN 29
#endif

#ifndef AF_CAN
#define AF_CAN PF_CAN
#endif

int printusage() {
    printf("Usage: canfuzzer [OPTIONS]\n");
    printf("Fuzzing CAN messages over socketcan\n\n");
    printf("OPTIONS:\n");
    printf("    -i <hex value>  Arbitration ID with which messages are sent\n");
    printf("    -g <seconds>    Gap time between sending messages (Default 0.01s)\n");
    printf("    -b <int>        Number of data bytes (Default 8)\n");
    printf("    -m <r, s or i>  Mode for data generation: Random/Sweep/Increment (Default Random)\n");
    printf("    -l              Save sent messages to logfile\n");
    printf("    -n <ifname>     Set the interface (Default 'vcan0')\n\n");
    return -1;
}

int main(int argc, char *argv[]) {

    /* ###############################################################
     * Check cmdline arguments
     * ############################################################### */
    int id = -1, gap = 10000, number_bytes = 8;
    bool writetolog = false;
    char mode[83] = "r", interface[83] = "vcan0", filename[83]; /* suggested by -Wformat-overflow= */
    FILE *logfile = NULL;

    while (true) {
        char option = getopt(argc, argv, "i:g:b:m:ln:h");
        if (option == -1) break; /* end of list */
        switch (option) {
            case 'i':
                if (id != -1) return printusage(); /* avoid doubled arguments */
                id = abs((int)strtol(optarg, NULL, 16));
                break;
            case 'g':
                if (gap != 10000) return printusage();
                gap = abs((int)(strtod(optarg, NULL)*1000000)); /* gap in  microseconds */
                break;
            case 'b':
                if (number_bytes != 8) return printusage();
                number_bytes = abs((int)strtol(optarg, NULL, 10));
                break;
            case 'm':
                if (strcmp(mode, "r") != 0) return printusage();
                strcpy(mode, optarg);
                if ( strcmp(mode, "r") != 0 && strcmp(mode, "s") != 0 && strcmp(mode, "i") != 0 ) {
                    return printusage();
                }
                break;
            case 'l':
                writetolog = true;
                break;
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

    if (id == -1) {
        printf("No ID is set.\n");
        return printusage();
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
    * Create the logfile
    * ############################################################### */
    if (writetolog) {
        time_t currtime;
        time(&currtime);
        struct tm *now = localtime(&currtime);

        sprintf(filename, "canfuzzer-%04d-%02d-%02d-%02d%02d%02d.log",
                now->tm_year + 1900, now->tm_mon + 1, now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec);

        logfile = fopen(filename, "w");
        if (!logfile) {
            perror("logfile");
            return 1;
        }
    }

    /* ###############################################################
     * Generate an send messages
     * ############################################################### */
    int active_byte = 0, carry;
    struct timespec starttime={0,0}, runtime={0,0};
    double elapsedtime;

    clock_gettime(CLOCK_MONOTONIC, &starttime);
    srand(time(NULL));

    frame.can_id = id;
    frame.len = number_bytes;
    for (int i = 0; i < frame.len; i++) {
        // set data bytes to 00 initially
        frame.data[i] = 0;
    }

    while (true) {
        /* Runtime calculation */
        clock_gettime(CLOCK_MONOTONIC, &runtime);
        elapsedtime = ((double)runtime.tv_sec + 1.0e-9*(double)runtime.tv_nsec)
                      - ((double)starttime.tv_sec + 1.0e-9*(double)starttime.tv_nsec);
        
        /* Generate payload */
        /* random */
        if (strcmp(mode, "r") == 0) {
            for (int i = 0; i < frame.len; i++) {
                frame.data[i] = random();
            }
        }
        /* sweep */
        if (strcmp(mode, "s") == 0) {
            if (active_byte >= number_bytes) {
                active_byte = 0;
            }
            if (frame.data[active_byte] < 255) {
                frame.data[active_byte]++;
            }
            else {
                frame.data[active_byte] = 0;
                frame.data[++active_byte]++;
            }
        }
        /* increment */
        if (strcmp(mode, "i") == 0) {
            carry = 0;
            if (frame.data[0] < 255) {
                frame.data[0]++;
            }
            for (int i = 0; i < number_bytes; i++) {
                if (carry > 0) {
                    frame.data[i] += carry;
                    carry = 0;
                }
                if (frame.data[i] >= 255) {
                    carry = 1;
                }
            }
        }
        
        /* Write message to the can bus */
        write(skt, &frame, CAN_MTU);

        printf("Sending [%d Byte] ", frame.len);
        /* use fprint_canframe function from can-utils lib.c */
        fprint_canframe(stdout, &frame, "\n", 0, sizeof(frame));

        if (writetolog) {
            /* writing to logfile - parse_canframe() format */
            fprintf(logfile, "%.6f ", elapsedtime);
            fprint_canframe(logfile, &frame, "\n", 0, frame.len);
        }

        /* if option -g is set gap time between message sending - default 10000 */
        usleep(gap);
    }

    if (writetolog) {
        fclose(logfile);
    }

    return 0;
}
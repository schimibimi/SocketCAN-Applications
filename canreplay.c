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
    printf("Usage: canreplay [OPTIONS]\n");
    printf("Replays CAN messages over socketcan\n\n");
    printf("OPTIONS:\n");
    printf("    -p <seconds>    Capturing period (Default 60 seconds)\n");
    printf("    -d <seconds>    Delay before replaying messages\n");
    printf("    -i <hex value>  Filter messages by hexadecimal Arbitration ID\n");
    printf("    -t              Replay with exact time differences between messages\n");
    printf("    -g <seconds>    Define gap time between sending messages (Default 0.01s)\n");
    printf("    -f <FILE>       Use logfile input for replaying messages\n");
    printf("    -l              Keep temporarily created logfile\n");
    printf("    -n <ifname>     Set the interface (Default 'vcan0')\n\n");
    return -1;
}

int main(int argc, char *argv[]) {

    /* ###############################################################
     * Check cmdline arguments
     * ############################################################### */
    int period = 60, idfilter = -1, delay = 0, gap = 10000;
    bool timediff = false, keeplogfile = false, fileinput = false;

    FILE *logfile = NULL;
    char interface[32] = "vcan0", filename[83] = ""; /* suggested by -Wformat-overflow= */

    while (true) {
        char option = getopt(argc, argv, "p:d:i:tg:f:ln:h");
        if (option == -1) break; /* end of list */
        switch (option) {
            case 'p':
                if (period != 60) return printusage(); /* avoid doubled arguments */
                period = abs((int)strtol(optarg, NULL, 10));
                break;
            case 'd':
                if (delay != 0) return printusage();
                delay = abs((int)strtol(optarg, NULL, 10));
                break;
            case 'i':
                if (idfilter != -1) return printusage();
                idfilter = abs((int)strtol(optarg, NULL, 16));
                break;
            case 't':
                timediff = true;
                break;
            case 'g':
                if (gap != 10000) return printusage();
                gap = abs((int)(strtod(optarg, NULL)*1000000)); /* gap in  microseconds */
                break;
            case 'f':
                if (filename[0] != '\0') return printusage();
                fileinput = true;
                strcpy(filename, optarg);
                break;
            case 'l':
                keeplogfile = true;
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

    if(!fileinput) { /* create logfile if no file input is given */
        /* ###############################################################
         * Create the logfile
         * ############################################################### */
        time_t currtime;
        time(&currtime);
        struct tm *now = localtime(&currtime);

        sprintf(filename, "canreplay-%04d-%02d-%02d-%02d%02d%02d.log",
                now->tm_year + 1900, now->tm_mon + 1, now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec);

        logfile = fopen(filename, "w+");
        if (!logfile) {
            perror("logfile");
            return 1;
        }

        /* ###############################################################
         * Run capturing loop
         * ############################################################### */
        struct timespec starttime={0,0}, runtime={0,0};
        double elapsedtime;

        clock_gettime(CLOCK_MONOTONIC, &starttime);

        while (true){
            /* Runtime calculation */
            clock_gettime(CLOCK_MONOTONIC, &runtime);
            elapsedtime = ((double)runtime.tv_sec + 1.0e-9*(double)runtime.tv_nsec)
                          - ((double)starttime.tv_sec + 1.0e-9*(double)starttime.tv_nsec);
            if (elapsedtime >= period) {
                break;
            }
            /* Read a message back from the CAN bus */
            read(skt, &frame, sizeof(frame));
            /* writing to console */
            printf("Receiving [%d byte] ", frame.len);
            fprint_canframe(stdout, &frame, "\n", 0, frame.len);
            /* writing to logfile - parse_canframe() format */
            fprintf(logfile, "%.6f ", elapsedtime);
            fprint_canframe(logfile, &frame, "\n", 0, frame.len);
        }
    }
    else {
        /* open the passed logfile */
        logfile = fopen(filename, "r");
        if (!logfile) {
            perror("logfile");
            return 1;
        }
    }

    /* ###############################################################
     * Wait delay time before begin replaying
     * ############################################################### */
    if(delay != 0) {
        printf("\nWaiting %d seconds before replaying...\n", delay);
        sleep(delay);
    }

    /* ###############################################################
     * Replaying messages
     * ############################################################### */
    char line[64], *strptr, strsend[32], *delimiter = " ";
    double messagetime, lastmessagetime = 0.0;
    int strtoken, msgsize;
    bool dowrite;

    rewind(logfile); /* sets the file position to the beginning */
    /* read line by line from logfile */
    while(fgets(line, sizeof(line), logfile) != NULL) {
        /* cut string into parts */
        strptr = strtok(line, delimiter);
        strtoken = 0;
        while (strptr != NULL) {
            if (timediff && strtoken == 0) { /* cut gap time */
                messagetime = strtod(strptr, NULL);
                gap = (int)((messagetime - lastmessagetime)*1000000); /* get gap in microseconds */
                lastmessagetime = messagetime;
            }
            if (strtoken == 1) { /* cut can message */
                strcpy(strsend, strptr);
            }
            strptr = strtok(NULL, delimiter);
            strtoken++;
        }
        /* if option -t or -g is set gap time between message sending - default 10000 */
        usleep(gap);

        strsend[strcspn(strsend, "\n")] = 0; /* remove '\n' from string end */
        /* use parsing function from can-utils lib.c */
        msgsize = parse_canframe(strsend, &frame);
	    /* Filter messages by id */
	    dowrite = false;
        if(idfilter != -1) { /* filter is set */
            if(frame.can_id == idfilter) { /* filter matches */
                dowrite = true;
            }
        }
        else dowrite = true; /* filter is not set */
        if (dowrite) {
            /* Write message to the can bus */
            write(skt, &frame, msgsize);
            printf("Sending [%d Byte] ", frame.len);
            /* use fprint_canframe function from can-utils lib.c */
            fprint_canframe(stdout, &frame, "\n", 0, sizeof(frame));
	    }
    }

    /* ###############################################################
     * Keep or delete logfile
     * ############################################################### */
    fclose(logfile);
    if (!keeplogfile && !fileinput) {
        if (remove(filename) != 0) {
            perror("logfile");
        }
    }

    return 0;
}
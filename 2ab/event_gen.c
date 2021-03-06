#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <linux/ptp_clock.h>

#define PTP_PIN_SETFUNC    _IOW(PTP_CLK_MAGIC, 7, struct ptp_pin_desc)

#define FD_TO_CLOCKID(fd) ((~(clockid_t) (fd) << 3) | 3)
#ifndef CLOCK_INVALID
#define CLOCK_INVALID -1
#endif

struct ptp_pin_desc {
    /*
     * Hardware specific human readable pin name. This field is
     * set by the kernel during the PTP_PIN_GETFUNC ioctl and is
     * ignored for the PTP_PIN_SETFUNC ioctl.
     */
    char name[64];
    /*
     * Pin index in the range of zero to ptp_clock_caps.n_pins - 1.
     */
    unsigned int index;
    /*
     * Which of the PTP_PF_xxx functions to use on this pin.
     */
    unsigned int func;
    /*
     * The specific channel to use for this function.
     * This corresponds to the 'index' field of the
     * PTP_EXTTS_REQUEST and PTP_PEROUT_REQUEST ioctls.
     */
    unsigned int chan;
    /*
     * Reserved for future use.
     */
    unsigned int rsv[5];
};

char *device_m;             /* PTP device */
int index_m;                /* Channel index, '1' corresponds to 'TIMER6' */
int fd_m;                   /* device file descriptor */
clockid_t clkid_m; 	    /* Clock ids */
struct ptp_pin_desc desc;   /* Pin configuration */

// static int running = 1;

// static void exit_handler(int s)
// {
// 	printf("Exit requested \n");
//   	running = 0;
// }

/**
 * Initialize the QoT system for capturing input event timestamps.
 *
 * Params:
 * dev - PTP device file
 * channel - Channel index, '1' corresponds to 'TIMER6'
 */
int init_qot(char* dev, int channel, int ts_gen_s[], long long ts_gen_ns[], int size_ts_gen)
{
    struct ptp_perout_request perout_request;	/* Programmable interrupt */
    struct timespec ts;				/* Time storage */

    device_m = dev;                             /* PTP device */
    index_m = channel;                          /* Channel index */

    /* Open the character device */
    fd_m = open(device_m, O_RDWR);
    if (fd_m < 0) {
        fprintf(stderr, "opening device %s: %s\n",
                device_m, strerror(errno));
        return -1;
    }
    printf("Device opened %d\n", fd_m);

    memset(&desc, 0, sizeof(desc));
    desc.index = index_m;
    desc.func = 2;              // '2' corresponds to periodic output
    desc.chan = index_m;
    if (ioctl(fd_m, PTP_PIN_SETFUNC, &desc)) {
        printf("Set pin func failed for %d\n", fd_m);
        return -1;
    }
    printf("Set pin func successful for %d\n", fd_m);

    clkid_m = FD_TO_CLOCKID(fd_m);
    if (CLOCK_INVALID == clkid_m) {
	printf("failed to read clock id for %d\n", fd_m);
	return -1;
    }
    printf("clock id is %lld\n", clkid_m);

    /* Get the clock current time */
    if (clock_gettime(clkid_m, &ts)) {
	printf("clock_gettime failed for %d\n",fd_m);
	return -1;
    }
    uint64_t nano_ts = (ts.tv_sec * 1000000000) + ts.tv_nsec;
    printf("QoT current time: %lld.%09u\n", (long long)ts.tv_sec, (long long)ts.tv_nsec);

    /* Configure pulse per second to start at deterministic point in future */
    memset(&perout_request, 0, sizeof(perout_request));
    int n;
    double offset = 2.0;
    
    for(n=0; n<size_ts_gen; n++){

        //pulseStart = ((long long)(nano_ts)) + (offset + ts_gen[n]) * 1000000000;

        perout_request.index = index_m;
        perout_request.start.sec = ((long long)(ts.tv_sec)) + offset + ts_gen_s[n];
        perout_request.start.nsec = (long long)ts.tv_nsec + ts_gen_ns[n];
        perout_request.period.sec = 0;
        perout_request.period.nsec = 50000000; // 50 msecond period, change it to desired value

        if (ioctl(fd_m, PTP_PEROUT_REQUEST, &perout_request)) {
            printf("cannot request periodic output for %d\n", fd_m);
            return -1;
        }
        printf("Output for requested time successful for %d, outputting at %lld.%09u: \n", n, ((long long)(ts.tv_sec)) + offset + ts_gen_s[n], (long long)ts.tv_nsec + ts_gen_ns[n]);
        
        // pulseEnd = pulseStart + 25500000;
        perout_request.start.nsec = (long long)ts.tv_nsec + ts_gen_ns[n] + 25500000;
        perout_request.period.nsec = 0; // 1 second period, change it to desired value

        if (ioctl(fd_m, PTP_PEROUT_REQUEST, &perout_request)) {
            printf("cannot request periodic output for %d\n", fd_m);
            return -1;
        }
        printf("Set output low successful for %d, outputting at %lld.%09u: \n", n, ((long long)(ts.tv_sec)) + offset + ts_gen_s[n], (long long)ts.tv_nsec + ts_gen_ns[n] + 25500000);
        

        //printf("Requesting periodic output success for %d\n", fd_m);
    }
    

    return 0;
}

int deinit_qot()
{
    int ret = 0;

    /* Disable the pin */
    memset(&desc, 0, sizeof(desc));
    desc.index = index_m;
    desc.func = 0;              // '0' corresponds to no function
    desc.chan = index_m;
    if (ioctl(fd_m, PTP_PIN_SETFUNC, &desc)) {
        printf("Disable pin func failed for %d\n", fd_m);
        ret = -1;
    }

    /* Close the character device */
    close(fd_m);

    return ret;
}

// int main(void)
// {
//     // Setup QoT
//     if (init_qot("/dev/ptp1", 2)) { // index=2 corresponds to TIMER7
//         printf("Initialize QoT failed\n");
// 	deinit_qot();
//         exit(EXIT_FAILURE);
//     }

//     int err = 0;
//     uint64_t nano_ts;

//     signal(SIGINT, exit_handler);
//     while (running) 
//     {
// 	usleep(10000);
//     }


//     // Teardown QoT
//     if (deinit_qot()) {
//         printf("Deinitialize QoT failed\n");
//     }

//     return(EXIT_SUCCESS);
// }

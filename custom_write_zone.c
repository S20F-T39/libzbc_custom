#define _GNU_SOURCE     /* O_LARGEFILE & O_DIRECT */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include <libzbc/zbc.h>

/**
 * I/O abort.
 */
static int zbc_write_zone_abort = 0;

/**
 * System time in usecs.
 */
static inline unsigned long long zbc_write_zone_usec(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return (unsigned long long) tv.tv_sec * 1000000LL +
           (unsigned long long) tv.tv_usec;
}

/**
 * Signal handler.
 */
static void zbc_write_zone_sigcatcher(int sig) {
    zbc_write_zone_abort = 1;
}

int main(int argc, char **argv) {

    int zone_idx, i, fd = -1;
    int flags = O_RDWR;
    char *path, *file = NULL;
    ssize_t ret = 1;

    struct zbc_device *dev = NULL;
    struct zbc_device_info info = {0};
    struct zbc_zone *zones = NULL;
    struct zbc_zone *io_zone = NULL;

    unsigned int nr_zones;          // Number of Zones

    if (argc < 3) {
        usage:
        printf("Usage: %s [options] <file> <dev>\n"
               "Options:\n"
               "    -v          : Verbose mode\n",
               argv[0]);
        return 1;
    }

    /* Parse Options */
    for (i = 1; i < (argc - 1); i++) {
        if (strcmp(argv[i], "-v") == 0) {
            zbc_set_log_level("debug");
        } else if (argv[i][0] == '-') {

            fprintf(stderr, "Unknown option \"%s\"\n", argv[i]);
            goto usage;

        } else {
            break;
        }
    }

    if (i != (argc - 2))
        goto usage;

    /* Get Parameters */
    file = argv[i];
    printf("File: %s\n", file);

    path = argv[i + 1];

    /* Setup signal handler */
    signal(SIGQUIT, zbc_write_zone_sigcatcher);
    signal(SIGINT, zbc_write_zone_sigcatcher);
    signal(SIGTERM, zbc_write_zone_sigcatcher);

    /* Open Device */
    ret = zbc_open(path, flags, &dev);
    if (ret != 0) {
        if (ret == -ENODEV)
            fprintf(stderr,"Open %s failed (not a zoned block device)\n",
                    path);
        else
            fprintf(stderr, "Open %s failed (%s)\n",
                    path, strerror(-ret));
        return 1;
    }

    zbc_get_device_info(dev, &info);

    printf("Device %s:\n", path);
    zbc_print_device_info(&info, stdout);

    /* Get Zone list */
    ret = zbc_list_zones(dev, 0, ZBC_RO_EMPTY, &zones, &nr_zones);
    if (ret != 0) {
        fprintf(stderr, "zbc_list_zones failed\n");
        ret = 1;
        goto out;
    }

    /* Get Target Zone */
    /**
     * zone_idx 가 zone number 를 의미하며,
     * nr_zone 보다 zone_idx 가 크면 invalid 한 것.
     *
     */
     if (zone_idx > (int) nr_zones) {

     }

    /* Part of File I/O */

    out:
    if (fd > 0)
        close(fd);
    zbc_close(dev);
    free(zones);

    return ret;
}

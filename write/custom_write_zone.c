// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * This file is part of libzbc.
 *
 * Copyright (C) 2009-2014, HGST, Inc. All rights reserved.
 * Copyright (C) 2016, Western Digital. All rights reserved.
 * Copyright (C) 2020 Western Digital COrporation or its affiliates.
 *
 * Author: Damien Le Moal (damien.lemoal@wdc.com)
 *         Christophe Louargant (christophe.louargant@wdc.com)
 */

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

    int init_zone_idx, zone_idx, i, fd = -1;
    int flags = O_RDWR;
    char *path, *end, *file = NULL;
    void *io_buffer = NULL;

    ssize_t ret = 1;
    size_t io_size, io_align, buf_size = 512;

    struct stat st;

    struct zbc_device *dev = NULL;
    struct zbc_device_info info = {0};

    struct zbc_zone *empty_zones = NULL;
    struct zbc_zone *imp_open_zones = NULL;
    struct zbc_zone *io_zone = NULL;

    unsigned int nr_zones;              // Number of Zones
    unsigned long pattern = 0;
    unsigned long long f_size;

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
            fprintf(stderr, "Open %s failed (not a zoned block device)\n",
                    path);
        else
            fprintf(stderr, "Open %s failed (%s)\n",
                    path, strerror(-ret));
        return 1;
    }

    zbc_get_device_info(dev, &info);

    printf("Device %s:\n", path);
    zbc_print_device_info(&info, stdout);

    /* Get Empty Zone list (Without Conventional) */
    // TODO sector 를 찾을 수 있는가? == empty zone 첫 번째 index 를 구할 수 있는가?
    ret = zbc_list_zones(dev, 0, ZBC_RO_EMPTY, &empty_zones, &nr_zones);
    if (ret != 0) {
        fprintf(stderr, "zbc_list_zones failed\n");
        ret = 1;
        goto out;
    }

    /* Get Target Zone */
    /* Conventional zone 을 제외하고 받아왔으므로, Conventional check 불필요 */
    /* Empty zone 중에 제일 첫번째 zone */
    io_zone = &empty_zones[0];
    zone_idx = (int) (io_zone->zbz_start / zbc_zone_length(io_zone));
    if (!zbc_zone_sequential(io_zone)) {
        errno = EINVAL;
        perror("Invalid or Cannot find zone\n");
        return -1;
    }

    printf("Target zone: Zone %d / %d, type 0x%x (%s), "
           "cond 0x%x (%s), rwp %d, non_seq %d, "
           "sector %llu, %llu sectors, wp %llu\n",
           zone_idx,
           nr_zones,
           zbc_zone_type(io_zone),
           zbc_zone_type_str(zbc_zone_type(io_zone)),
           zbc_zone_condition(io_zone),
           zbc_zone_condition_str(zbc_zone_condition(io_zone)),
           zbc_zone_rwp_recommended(io_zone),
           zbc_zone_non_seq(io_zone),
           zbc_zone_start(io_zone),
           zbc_zone_length(io_zone),
           zbc_zone_wp(io_zone));

    /* Part of File I/O */
    /**
     * Check I/O alignment and get an I/O buffer
     */
    if (zbc_zone_sequential_req(io_zone))
        io_align = info.zbd_pblock_size;        // Physical Block Size
    else
        io_align = info.zbd_lblock_size;        // Logical Block Size

    if (buf_size % io_align) {
        fprintf(stderr, "Invalid I/O size %zu (must me aligned on %zu)\n",
                buf_size, io_align);
        ret = 1;
        goto out;
    }

    io_size = buf_size;
    ret = posix_memalign((void **) &io_buffer, sysconf(_SC_PAGESIZE), io_size);
    if (ret != 0) {
        fprintf(stderr, "No memory for I/O buffer (%zu B)\n", io_size);
        ret = 1;
        goto out;
    }

    memset(io_buffer, pattern, io_size);

    /* Open the file to read */
    if (file) {
        fd = open(file, O_LARGEFILE | O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "Open file \"%s\" failed %d (%s)\n",
                    file, errno, strerror(errno));
            ret = 1;
            goto out;
        }

        ret = fstat(fd, &st);
        if (ret != 0) {
            fprintf(stderr, "Stat file \"%s\" failed %d (%s)\n",
                    file, errno, strerror(errno));
            ret = 1;
            goto out;
        }

        if (S_ISREG(st.st_mode)) {
            f_size = st.st_size;
            printf("Valid File Size: %llu\n", f_size);
        } else if (S_ISBLK(st.st_mode)) {
            ret = ioctl(fd, BLKGETSIZE64, &f_size);
            if (ret != 0) {
                fprintf(stderr, "ioctl BLKGETSIZE64 block device \"%s\" failed %d (%s)\n",
                        file, errno, strerror(errno));
                ret = 1;
                goto out;
            }
        } else {
            fprintf(stderr, "Unsupported file \"%s\" type\n", file);
            ret = 1;
            goto out;
        }
    }

    out:
    if (fd > 0)
        close(fd);
    zbc_close(dev);
    free(empty_zones);
    free(io_buffer);

    return ret;
}

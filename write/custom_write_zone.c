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

    int zone_idx, i, fd = -1;
    int flags = O_RDWR;
    char *path, *file = NULL;
    void *io_buffer = NULL;

    long long zone_offset = 0;
    long long sector_offset;
    long long sector_max;

    ssize_t ret;
    ssize_t sector_count;
    size_t io_size, io_align, buf_size;

    struct stat st;

    struct zbc_device *dev = NULL;
    struct zbc_device_info info = {0};

    struct zbc_zone *empty_zones = NULL;
    struct zbc_zone *imp_open_zones = NULL;
    struct zbc_zone *io_zone = NULL;

    unsigned int nr_empty_zones;
    unsigned int nr_imp_open_zones;

    unsigned long pattern = 0;
    unsigned long long io_count = 0, io_num = 0;
    unsigned long long elapsed, f_size;
    unsigned long long b_rate, b_count = 0;

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

    /* Get Zone lists (Without Conventional) */
    ret = zbc_list_zones(dev, 0, ZBC_RO_EMPTY, &empty_zones, &nr_empty_zones);
    if (ret != 0) {
        fprintf(stderr, "zbc_list_zones failed\n");
        ret = 1;
        goto out;
    }
    ret = zbc_list_zones(dev, 0, ZBC_RO_IMP_OPEN, &imp_open_zones, &nr_imp_open_zones);
    if (ret != 0) {
        fprintf(stderr, "zbc_list_zones failed\n");
        ret = 1;
        goto out;
    }

    /* Get Target Zone */
    /* Conventional zone 을 제외하고 받아왔으므로, Conventional check 불필요 */
    if (nr_empty_zones == 0)
        io_zone = &imp_open_zones[0];
    else
        io_zone = &empty_zones[0];

    zone_idx = (int) (io_zone->zbz_start / zbc_zone_length(io_zone));
    if (!zbc_zone_sequential(io_zone)) {
        errno = EINVAL;
        perror("Invalid or Cannot find zone\n");
        return -1;
    }

    if (nr_empty_zones == 0) {
        printf("Target zone: Implicit Open Zone %d / %d, type 0x%x (%s), "
               "cond 0x%x (%s), rwp %d, non_seq %d, "
               "sector %llu, %llu sectors, wp %llu\n",
               zone_idx,
               nr_imp_open_zones,
               zbc_zone_type(io_zone),
               zbc_zone_type_str(zbc_zone_type(io_zone)),
               zbc_zone_condition(io_zone),
               zbc_zone_condition_str(zbc_zone_condition(io_zone)),
               zbc_zone_rwp_recommended(io_zone),
               zbc_zone_non_seq(io_zone),
               zbc_zone_start(io_zone),
               zbc_zone_length(io_zone),
               zbc_zone_wp(io_zone));
    } else {
        printf("Target zone: Empty Zone %d / %d, type 0x%x (%s), "
               "cond 0x%x (%s), rwp %d, non_seq %d, "
               "sector %llu, %llu sectors, wp %llu\n",
               zone_idx,
               nr_empty_zones,
               zbc_zone_type(io_zone),
               zbc_zone_type_str(zbc_zone_type(io_zone)),
               zbc_zone_condition(io_zone),
               zbc_zone_condition_str(zbc_zone_condition(io_zone)),
               zbc_zone_rwp_recommended(io_zone),
               zbc_zone_non_seq(io_zone),
               zbc_zone_start(io_zone),
               zbc_zone_length(io_zone),
               zbc_zone_wp(io_zone));
    }


    /* Part of File I/O */
    /**
     * Check I/O alignment and get an I/O buffer
     */
    buf_size = sysconf(_SC_PAGESIZE);

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

        printf("Writing file \"%s\" (%llu B) to target zone %d, %zu B I/Os\n",
               file, f_size, zone_idx, io_size);

    } else {

        printf("Filling target zone %d, %zu B I/Os\n",
               zone_idx, io_size);

    }

    sector_max = zbc_zone_length(io_zone);
    if (zbc_zone_sequential_req(io_zone)) {
        if (zbc_zone_full(io_zone))
            sector_max = 0;
        else if (zbc_zone_wp(io_zone) > zbc_zone_start(io_zone))
            sector_max = zbc_zone_wp(io_zone) - zbc_zone_start(io_zone);
    }

    elapsed = zbc_write_zone_usec();

    while (!zbc_write_zone_abort) {
        if (file) {
            size_t ios;

            /* Read File */
            ret = read(fd, io_buffer, io_size);
            if (ret < 0) {
                fprintf(stderr, "Read file \"%s\" failed %d (%s)\n",
                        file, errno, strerror(errno));
                ret = 1;
                break;
            }

            ios = ret;
            if (ios < io_size) {
                if (ios) {
                    /* Clear end of buffer */
                    memset(io_buffer + ios, 0, io_size - ios);
                }
            }

            if (!ios) {
                /* EOF */
                break;
            }
        }

        /* Do not exceed the end of the zone */
        if (zbc_zone_sequential(io_zone) && zbc_zone_full(io_zone))
            sector_count = 0;
        else
            sector_count = io_size >> 9;
        if (zone_offset + sector_count > sector_max)
            sector_count = sector_max - zone_offset;
        if (!sector_count)
            break;
        sector_offset = zbc_zone_start(io_zone) + zone_offset;

        /* Write to zone */
        ret = zbc_pwrite(dev, io_buffer, sector_count, sector_offset);
        if (ret <= 0) {
            fprintf(stderr, "%s failed %zd (%s)\n",
                    "zbc_pwrite", -ret, strerror(-ret));
            ret = 1;
            goto out;
        }

        zone_offset += ret;
        b_count += ret << 9;
        io_count++;

        if (io_num > 0 && io_count >= io_num)
            break;

    }

    elapsed = zbc_write_zone_usec() - elapsed;
    if (elapsed) {
        printf("Wrote %llu B (%llu I/Os) in %llu.%03llu sec\n",
               b_count, io_count, elapsed / 1000000, (elapsed / 1000000) / 1000);
        printf("    IOPS %llu\n", io_count * 1000000 / elapsed);
        b_rate = b_count * 1000000 / elapsed;
        printf("    BW %llu.%03llu MB/s\n",
               b_rate / 1000000, (b_rate % 1000000) / 1000);
    } else {
        printf("Wrote %llu B (%llu I/Os)\n", b_count, io_count);
    }

    out:
    if (fd > 0)
        close(fd);
    zbc_close(dev);
    free(empty_zones);
    free(io_buffer);

    return ret;
}

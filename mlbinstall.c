/*
 * mlbinstall
 * Configures MLB to boot a kernel with a command line and installs it on
 * a target (e.g. a file, a block device, ...).
 *
 * Copyright (C) 2014 Wiktor W Brodlo
 *
 * mlbinstall is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mlbinstall is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mlbinstall. If not, see <http://www.gnu.org/licenses/>.
 */

#define _BSD_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500

#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <err.h>
#include <endian.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <stdio.h>

#include "mlb_bin.h"

/* Checks if the kernel boot protocol is supported */
void check_version(const char *kernel)
{
	int fd = open(kernel, O_RDONLY);
	if (fd == -1)
		err(1, "Failed opening %s", kernel);

	long ps = sysconf(_SC_PAGESIZE);
	size_t mlength = (512 / ps + 1) * ps;
	uint8_t *m = mmap(NULL, mlength, PROT_READ, MAP_PRIVATE, fd, 0);
	if (m == MAP_FAILED)
		err(1, "Failed mapping %s", kernel);

	uint32_t header = *(uint32_t *)(m + 0x202);
	uint16_t version = be16toh(*(uint16_t *)(m + 0x206));
	uint8_t loadflags = m[0x211];

	if (header != 0x53726448)
		errx(1, "%s is missing a Linux kernel header", kernel);
	if (version < 0x204)
		errx(1, "Kernel too old, boot protocol version >= 0x204/\
kernel version >= 2.6.14 required, but %s is 0x%x", kernel, version);
	if (!(loadflags & 0x01))
		errx(1, "Kernel needs to be loaded high");

	munmap(m, mlength);
	close(fd);
}

/* Returns the length of cmdline, including the terminating null. */
uint16_t cmdlen(const char *cmdline, size_t mlblen, size_t mbrlen)
{
	/* Note the last byte of mlb.bin is 0, reserved for cmdline. */
	size_t maxlen = mbrlen - mlblen + 1;
	size_t len = strnlen(cmdline, maxlen);
	if (len == maxlen)
		errx(1, "Command line too long, max length: %lu", maxlen);
	return len + 1;
}

/* Returns the kernel file LBA. */
uint32_t lba(const char *fn)
{
	int fd = open(fn, O_RDONLY);
	if (fd == -1)
		err(1, "Failed opening %s", fn);

	struct fiemap *fm = calloc(1,
	                 sizeof(struct fiemap) + sizeof(struct fiemap_extent));
	if (!fm)
		err(1, "Failed to allocate memory");
	fm->fm_length = ~0ull;
	fm->fm_flags = FIEMAP_FLAG_SYNC;
	fm->fm_extent_count = 1;
	if (ioctl(fd, FS_IOC_FIEMAP, fm) == -1)
		err(1, "ioctl failed");
	if (close(fd))
		err(1, "Failed closing %s", fn);

	/*
	 * Some messages from <linux/fiemap.h>.
	 * TODO: Write better messages.
	 */
	bool error = false;
	if (!(fm->fm_extents[0].fe_flags & FIEMAP_EXTENT_LAST)) {
		warnx("%s is fragmented", fn);
		error = true;
	}
	if (fm->fm_extents[0].fe_flags & FIEMAP_EXTENT_UNKNOWN) {
		warnx("%s: Data location unknown", fn);
		error = true;
	}
	if (fm->fm_extents[0].fe_flags & FIEMAP_EXTENT_DELALLOC) {
		warnx("%s: Location still pending", fn);
		error = true;
	}
	if (fm->fm_extents[0].fe_flags & FIEMAP_EXTENT_ENCODED) {
		warnx("%s: Data can not be read while fs is unmounted", fn);
		error = true;
	}
	if (fm->fm_extents[0].fe_flags & FIEMAP_EXTENT_DATA_ENCRYPTED) {
		warnx("%s: Data is encrypted by fs", fn);
		error = true;
	}
	if (fm->fm_extents[0].fe_flags & FIEMAP_EXTENT_NOT_ALIGNED) {
		warnx("%s: Extent offsets may not be block aligned", fn);
		error = true;
	}
	if (fm->fm_extents[0].fe_flags & FIEMAP_EXTENT_UNWRITTEN) {
		warnx("%s: Space allocated, but no data (i.e. zero)", fn);
		error = true;
	}
	if (fm->fm_extents[0].fe_physical / 512 > ~0u) {
		warnx("%s is further than 2 TB into the disk", fn);
		error = true;
	}
	if (error)
		errx(1, "%s is unbootable due to condition(s) above", fn);

	/* TODO: Make this more portable. */
	uint32_t lba = fm->fm_extents[0].fe_physical / 512;
	free(fm);
	return lba;
}

/* Copies MLB code to the MBR buffer. */
void mlbcopy(uint8_t *mbr, unsigned char *mlb, size_t mlblen)
{
	memcpy(mbr, mlb, mlblen);
}

/* Copies the kernel LBA to the MBR buffer. */
void lbacopy(uint8_t *mbr, size_t mlblen, uint32_t lba)
{
	memcpy(mbr + mlblen - 5, &lba, 4);
}

/* Copies the kernel command line to the MBR buffer. */
void cmdcopy(uint8_t *mbr, size_t mlblen, const char *cmd, uint16_t clen)
{
	memcpy(mbr + mlblen - 1, cmd, clen);
	for (size_t i = 0; i < mlblen - 1; ++i)
		if (mbr[i] == 0xca && mbr[i + 1] == 0xfe)
			memcpy(mbr + i, &clen, 2);
}

/* Writes the MBR buffer to the target MBR. */
void mbrwrite(const char *target, uint8_t *mbr)
{
	FILE *f = fopen(target, "r+");
	if (!f)
		err(1, "Failed opening %s", target);
	if (!fwrite(mbr, 446, 1, f))
		err(1, "%s: Failed writing the MBR", target);
	if (fseek(f, 510, 0))
		err(1, "%s: Failed seeking to write the magic value", target);
	uint16_t magic = 0xaa55;
	if (!fwrite(&magic, 2, 1, f))
		err(1, "%s: Failed writing the magic value", target);
	if (fclose(f))
		err(1, "Failed closing %s", target);
	sync();
}

int main(int argc, char **argv)
{
	/* Check for -vbr first to avoid calling strncmp twice */
	bool vbr = false;
	if (argc == 5 && !strncmp(argv[4], "-vbr", 5))
		vbr = true;

	if ((argc != 4 && argc != 5) || (argc == 5 && !vbr))
		errx(1, "Usage: %s <target> <kernel> <command line> [-vbr]\n\
Configures MLB to boot the kernel with the command line and installs it on\n\
target (could be a file, a block device, ...). Specify -vbr as the last\n\
argument to not reserve space for a partition table and gain an extra\n\
64 bytes for the command line.\n", argv[0]);

	const char *target = argv[1];
	const char *kernel = argv[2];
	const char *cmdline = argv[3];

	check_version(kernel);

	size_t mbr_len = vbr ? 510 : 446;
	uint16_t cmdline_len = cmdlen(cmdline, mlb_bin_len, mbr_len);
	uint32_t kernel_lba = lba(kernel);

	uint8_t mbr[510];
	memset(mbr, 0, mbr_len);
	mlbcopy(mbr, mlb_bin, mlb_bin_len);
	lbacopy(mbr, mlb_bin_len, kernel_lba);
	cmdcopy(mbr, mlb_bin_len, cmdline, cmdline_len);

	mbrwrite(target, mbr);
}


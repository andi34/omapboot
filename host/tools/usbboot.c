/*
 * Copyright (C) 2010 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the 
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED 
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <arpa/inet.h>
#include "usb.h"
#include "usbboot.h"
#ifndef USBBOOT_STAND_ALONE
#include "../../include/common/user_params.h"
#endif

static struct chip_info chip;

static char *usb_boot_read_chip_info(usb_handle *usb)
{
	static char proc_type[8];
	uint32_t msg_getid = 0xF0030003;
	uint8_t id[81];
	uint8_t *crc1;
	uint8_t gp_device_crc1[4] = {0, 0, 0, 0};
	int i;

	memset(id, 0xee, 81);
	fprintf(stderr,"reading ASIC ID\n");
	usb_write(usb, &msg_getid, sizeof(msg_getid));
	usb_read(usb, id, sizeof(id));

	memcpy(&chip.chip, &id[OFF_CHIP+0], 2);
	chip.chip = ntohs(chip.chip);
	chip.rom_rev = id[OFF_ROM_REV];
	memcpy(chip.IDEN, &id[OFF_ID], 20);
	memcpy(chip.MPKH, &id[OFF_MPKH], 32);
	chip.crc0 = ntohl(*(uint32_t *)&id[73]);
	chip.crc1 = ntohl(*(uint32_t *)&id[77]);

	fprintf(stderr,"CHIP: %02x%02x\n", id[OFF_CHIP+0], id[OFF_CHIP+1]);
	fprintf(stderr, "rom minor version: %02X\n", id[OFF_ROM_REV]);
	fprintf(stderr,"IDEN: ");
	for (i = 0; i < 20; i++)
		fprintf(stderr,"%02x", id[OFF_ID+i]);
	fprintf(stderr,"\nMPKH: ");
	for (i = 0; i < 32; i++)
		fprintf(stderr,"%02x", id[OFF_MPKH+i]);
	fprintf(stderr,"\nCRC0: %02x%02x%02x%02x\n",
		id[73], id[74], id[75], id[76]);
	fprintf(stderr,"CRC1: %02x%02x%02x%02x\n",
		id[77], id[78], id[79], id[80]);

	crc1 = &id[77];
	if (memcmp(crc1, &gp_device_crc1, 4 * sizeof(uint8_t))) {
		fprintf(stderr, "device is ED/HD (EMU/HS)\n");
		strcpy(proc_type, "EMU");
	} else {
		fprintf(stderr, "device is GP\n");
		strcpy(proc_type, "GP");
	}

	strcpy(chip.proc_type, proc_type);
	return proc_type;
}

static int usb_boot(usb_handle *usb, int fastboot_mode,
	     void *data, unsigned sz,
	     void *data2, unsigned sz2)
{
	uint32_t msg_boot = 0xF0030002;
	uint32_t msg_size = sz;
	uint32_t param = 0;

	fprintf(stderr, "sending 2ndstage to target...\n");
	usb_write(usb, &msg_boot, sizeof(msg_boot));
	usb_write(usb, &msg_size, sizeof(msg_size));
	usb_write(usb, data, sz);

	if ((data2) || (fastboot_mode > 0)) {
		fprintf(stderr,"waiting for 2ndstage response...\n");
		usb_read(usb, &msg_size, sizeof(msg_size));
		if (msg_size != 0xaabbccdd) {
			fprintf(stderr, "unexpected 2ndstage response\n");
			return -1;
		}

		if (fastboot_mode > 2) {

			param = fastboot_mode;
			usb_write(usb, &param, sizeof(param));
		}

		/* In fastboot mode, we stay in SRAM so don't
		download data2 to the target. Return back from here */
		if (fastboot_mode == 1) {
			fprintf(stderr, "received 2ndstage response...\n");
			return 0;
		}

		msg_size = sz2;
		usb_write(usb, &msg_size, sizeof(msg_size));
		usb_read(usb, &param, sizeof(param));
		if (param != 0xaabbccdd) {
			fprintf(stderr, "unexpected 2ndstage response\n");
			return -1;
		}

		fprintf(stderr, "sending image to target...size "
				"(%d-B/%d-KB/%d-MB)\n", msg_size,
				msg_size/1024, msg_size/(1024 * 1024));

		usb_write(usb, data2, sz2);
	}
	
	return 0;
}

static int match_omap_bootloader(usb_ifc_info *ifc)
{
	if (ifc->dev_vendor != 0x0451)
		return -1;
	if ((ifc->dev_product != 0xd010) && (ifc->dev_product != 0xd00f) &&
		(ifc->dev_product != 0xd011) &&  (ifc->dev_product != 0xd012) &&
		(ifc->dev_product != 0xd013) && (ifc->dev_product != 0xd014))
		return -1;
	return 0;
}

static void *load_file(const char *file, unsigned *sz)
{
	void *data;
	struct stat s;
	int fd;
	
	fd = open(file, O_RDONLY);
	if (fd < 0)
		return 0;
	
	if (fstat(fd, &s))
		goto fail;
	
	data = malloc(s.st_size);
	if (!data)
		goto fail;
	
	if (read(fd, data, s.st_size) != s.st_size) {
		free(data);
		goto fail;
	}
	
	close(fd);
	*sz = s.st_size;
	return data;
	
fail:
	fprintf(stderr, "failed loading file\n");
	close(fd);
	return 0;
}

static int usage(void)
{
	fprintf(stderr, "\nusbboot syntax and options:\n\n");
	fprintf(stderr, "usbboot [ <2ndstage> ] <image>\n\n");
	fprintf(stderr, "------------------------------------------------------\n");
#ifndef USBBOOT_STAND_ALONE
	fprintf(stderr, "example: ./out/<board>/usbboot boot.img\n");
	fprintf(stderr, "                    OR\n");
	fprintf(stderr, "         ./out/<board>/usbboot out/<board>/aboot.ift "
			"boot.img\n\t\t(for HD boards, aboot.ift needs signing)\n");
	fprintf(stderr, "=> Download and execute aboot second stage in SRAM\n"
			"   and then download and execute boot.img in SDRAM\n\n");
	fprintf(stderr, "------------------------------------------------------\n");
	fprintf(stderr, "example: ./out/<board>/usbboot -f\n");
	fprintf(stderr, "                    OR\n");
	fprintf(stderr, "         ./out/<board>/usbboot -f "
						"out<board>/iboot.ift\n");
	fprintf(stderr, "=> Download and execute iboot second stage in SRAM\n"
	                "   along with the configuration header (CH) and then\n"
			"   enter into FASTBOOT mode\n\n");
	fprintf(stderr, "------------------------------------------------------\n");
	fprintf(stderr, "example: ./out/<board>/usbboot -[M/m]\n");
	fprintf(stderr, "=> Execute SDRAM memory tests from SRAM\n"
			"during the first stage boot.\n\n");
	fprintf(stderr, "------------------------------------------------------\n");
#endif
	fprintf(stderr, "example: ./out/<board>/usbboot -s <file> \n");
	fprintf(stderr, "                    OR\n");
	fprintf(stderr, "         ./out/<board>/usbboot -S <file> \n");
	fprintf(stderr, "=> Download and execute <file> in internal memory\n");
	fprintf(stderr, "   without waiting for any response.\n");

	return 0;
}

static int *load_data_file(void *file, unsigned *size)
{
	if (!file)
		file = load_file("iboot.ift", size);

	else
		file = load_file(file, size);

	if (!file) {
		fprintf(stderr, "unable to load signed HS/HD iboot.ift\n");
		return NULL;
	} else
		return file;
}

#ifdef EMBED_IBOOT_HS
static void *get_iboot_hs(unsigned int *sz)
{
	void *data = NULL;
	uint32_t cpu = chip.chip << 8 | chip.rom_rev;

	switch (cpu) {
#if defined(CONFIG_IS_OMAP4)
	case 0x443003:
	case 0x443004:
		data = iboot_hs_4430_ES2_data;
		*sz  = iboot_hs_4430_ES2_size;
		break;
	case 0x444001:
		data = iboot_hs_4460_ES1_data;
		*sz  = iboot_hs_4460_ES1_size;
		break;
	case 0x447001:
		data = iboot_hs_4470_ES1_data;
		*sz  = iboot_hs_4470_ES1_size;
		break;
#elif defined(CONFIG_IS_OMAP5)
	case 0x543001:
		data = iboot_hs_5430_ES1_data;
		*sz  = iboot_hs_5430_ES1_size;
		break;
	case 0x543002:
		data = iboot_hs_5430_ES2_data;
		*sz  = iboot_hs_5430_ES2_size;
		break;
#endif
	default:
		fprintf(stderr, "Unknown cpu type\n");
	}

	return data;
}
#endif

int main(int argc, char **argv)
{
	void *data = NULL, *data2 = NULL;
	unsigned sz, sz2;
	usb_handle *usb = NULL;
	int once = 1;
	int fastboot_mode = 0;

	if ((argc < 2) || (argc > 3)) {
		usage();
		return 0;
	}

	if ((argv[1][0] == '-') &&
		((argv[1][1] == 's') || ((argv[1][1] == 'S')))) {
			for (;;) {
				usb = usb_open(match_omap_bootloader);
				if (usb) {
					usb_boot_read_chip_info(usb);
					break;
				}
			}
			data = load_data_file(argv[2], &sz);
			if (data == 0) {
				fprintf(stderr, "cannot load '%s'\n", argv[2]);
				usage();
				return -1;
			}
			once = 0;
#ifdef USBBOOT_STAND_ALONE
	} else {
		fprintf (stderr, "ERROR: Option not supported in stand-alone mode\n");
		return -1;
	}
#else
	} else if ((argv[1][0] == '-') &&
		(((argv[1][1] == 'f') || ((argv[1][1] == 'F'))) ||
		((argv[1][1] == 'm') || ((argv[1][1] == 'M'))))) {
		for (;;) {
			usb = usb_open(match_omap_bootloader);
			if (usb) {
				char proctype[8];
				strcpy(proctype, usb_boot_read_chip_info(usb));
				if (!memcmp(proctype, "EMU", 3)) {
					if (argc == 3)
						data = load_data_file(argv[2],
							&sz);
					else
						data = load_data_file(NULL,
							&sz);
#ifdef EMBED_IBOOT_HS
					if (!data)
						data = get_iboot_hs(&sz);
					if (data)
						fprintf(stderr, "using built-in"
						" HS iboot of size %d-KB\n",
								sz/1024);
#else
					if (!data) {
						fprintf(stderr, "No embedded HS"
						" iboot has been found and user"
						" didn't provide a valid "
						"iboot.ift either!");
						fprintf(stderr, " User needs to"
						" rebuild with MSHIELD.\n");
						return -1;
					}
#endif
				} else {
					fprintf(stderr, "using built-in GP "
						"iboot of size %d-KB\n",
							iboot_gp_size/1024);
					data = iboot_gp_data;
					sz = iboot_gp_size;
				}
				if (!data)
					return -1;

#ifdef TWO_STAGE_OMAPBOOT
				fprintf(stderr, "using built-in sboot of size "
						"%d-KB\n", sboot_size/1024);
				data2 = sboot_data;
				sz2 = sboot_size;

				if (data2 == 0) {
					usage();

					/* free up memory */
					if (data)
						free(data);

					return -1;
				}
				fastboot_mode = ((argv[1][1]) - 30) |
								USER_RQ_MASK;
				printf("fastboot_mode = %08x\n", fastboot_mode);
#endif
				break;
			}
			if (once) {
				once = 0;
				fprintf(stderr, "waiting for device...\n");
			}
			usleep(250);
		}

		once = 1;
#ifndef TWO_STAGE_OMAPBOOT
		fastboot_mode = 1;
		data2 = 0;
		sz2 = 0;
#endif

	} else {
		if (argc < 3) {
			fprintf(stderr, "using built-in 2ndstage.bin of size"
						" %d-KB\n", aboot_size/1024);
			data = aboot_data;
			sz = aboot_size;
		} else {
			data = load_file(argv[1], &sz);
			if (data == 0) {
				fprintf(stderr, "cannot load '%s'\n", argv[1]);
				usage();
				return -1;
			}
			argc--;
			argv++;
		}

		data2 = load_file(argv[1], &sz2);
		if (data2 == 0) {
			fprintf(stderr, "cannot load '%s'\n", argv[1]);
			usage();

			/* free up memory */
			if (data)
				free(data);

			return -1;
		}
	}
#endif

	for (;;) {
		if (usb == NULL)
			usb = usb_open(match_omap_bootloader);

		if (usb)
			return usb_boot(usb, fastboot_mode,
					data, sz, data2, sz2);
		if (once) {
			once = 0;
			fprintf(stderr, "waiting for device...\n");
		}
		usleep(250);
	}
	
	return -1;    
}

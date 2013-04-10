/*
 * dfu-util
 *
 * (C) 2007-2008 by OpenMoko, Inc.
 * Written by Harald Welte <laforge@openmoko.org>
 *
 * Based on existing code of dfu-programmer-0.4
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <libusb.h>
#include <errno.h>

#include "portable.h"
#include "dfu.h"
#include "usb_dfu.h"
#include "dfu_file.h"
#include "dfu_load.h"
#include "dfu_util.h"
#include "dfuse.h"
#include "quirks.h"

#ifdef HAVE_USBPATH_H
#include <usbpath.h>
#endif

int verbose = 0;

static void help(void)
{
	fprintf(stderr, "Usage: dfu-util [options] ...\n"
		"  -h --help\t\t\tPrint this help message\n"
		"  -V --version\t\t\tPrint the version number\n"
		"  -v --verbose\t\t\tPrint verbose debug statements\n"
		"  -l --list\t\t\tList currently attached DFU capable devices\n");
	fprintf(stderr, "  -e --detach\t\t\tDetach currently attached DFU capable devices\n"
		"  -E --detach-delay seconds\tTime to wait before reopening a device after detach\n"
		"  -d --device <vendor>:<product>[,<vendor_dfu>:<product_dfu>]\n"
		"\t\t\t\tSpecify Vendor/Product ID(s) of DFU device\n"
		"  -p --path <bus-port. ... .port>\tSpecify path to DFU device\n"
		"  -c --cfg <config_nr>\t\tSpecify the Configuration of DFU device\n"
		"  -i --intf <intf_nr>\t\tSpecify the DFU Interface number\n"
		"  -S --serial <serial_string>[,<serial_string_dfu>]\n"
		"\t\t\t\tSpecify Serial String of DFU device\n"
		"  -a --alt <alt>\t\tSpecify the Altsetting of the DFU Interface\n"
		"\t\t\t\tby name or by number\n");
	fprintf(stderr, "  -t --transfer-size <size>\tSpecify the number of bytes per USB Transfer\n"
		"  -U --upload <file>\t\tRead firmware from device into <file>\n"
		"  -Z --upload-size <bytes>\t\tSpecify the expected upload size in bytes\n"
		"  -D --download <file>\t\tWrite firmware from <file> into device\n"
		"  -R --reset\t\t\tIssue USB Reset signalling once we're finished\n"
		"  -s --dfuse-address <address>\tST DfuSe mode, specify target address for\n"
		"\t\t\t\traw file download or upload. Not applicable for\n"
		"\t\t\t\tDfuSe file (.dfu) downloads\n"
		);
	exit(EX_USAGE);
}

static void print_version(void)
{
	printf(PACKAGE_STRING "\n\n");
	printf("Copyright 2005-2008 Weston Schmidt, Harald Welte and OpenMoko Inc.\n"
	       "Copyright 2010-2012 Tormod Volden and Stefan Schmidt\n"
	       "This program is Free Software and has ABSOLUTELY NO WARRANTY\n"
	       "Please report bugs to " PACKAGE_BUGREPORT "\n\n");
}

static struct option opts[] = {
	{ "help", 0, 0, 'h' },
	{ "version", 0, 0, 'V' },
	{ "verbose", 0, 0, 'v' },
	{ "list", 0, 0, 'l' },
	{ "detach", 0, 0, 'e' },
	{ "detach-delay", 1, 0, 'E' },
	{ "device", 1, 0, 'd' },
	{ "path", 1, 0, 'p' },
	{ "configuration", 1, 0, 'c' },
	{ "cfg", 1, 0, 'c' },
	{ "interface", 1, 0, 'i' },
	{ "intf", 1, 0, 'i' },
	{ "altsetting", 1, 0, 'a' },
	{ "alt", 1, 0, 'a' },
	{ "serial", 1, 0, 'S' },
	{ "transfer-size", 1, 0, 't' },
	{ "upload", 1, 0, 'U' },
	{ "upload-size", 1, 0, 'Z' },
	{ "download", 1, 0, 'D' },
	{ "reset", 0, 0, 'R' },
	{ "dfuse-address", 1, 0, 's' }
};

int main(int argc, char **argv)
{
	struct dfu_if _rt_dif, _dif, *dif = &_dif;
	int expected_size = 0;
	int num_devs;
	int num_ifs;
	unsigned int transfer_size = 0;
	enum mode mode = MODE_NONE;
	struct dfu_status status;
	struct usb_dfu_func_descriptor func_dfu = {0}, func_dfu_rt = {0};
	libusb_context *ctx;
	struct libusb_device_descriptor desc;
	struct dfu_file file;
	char *alt_name = NULL; /* query alt name if non-NULL */
	char *device_id_filter = NULL;
	unsigned char active_alt_name[MAX_DESC_STR_LEN+1];
	char *end;
	int final_reset = 0;
	int ret;
	int dfuse_device = 0;
	const char *dfuse_options = NULL;
	int detach_delay =
#ifdef HAVE_WINDOWS_H
		5; /* Windows takes longer than other platforms to re-enumerate USB devices */
#else
		2;
#endif

	memset(dif, 0, sizeof(*dif));
	file.name = NULL;

	/* make sure all prints are flushed */
	setvbuf(stdout, NULL, _IONBF, 0);

	while (1) {
		int c, option_index = 0;
		c = getopt_long(argc, argv, "hVvleE:d:p:c:i:a:S:t:U:D:Rs:Z:", opts,
				&option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			help();
			break;
		case 'V':
			mode = MODE_VERSION;
			break;
		case 'v':
			verbose++;
			break;
		case 'l':
			mode = MODE_LIST;
			break;
		case 'e':
			mode = MODE_DETACH;
			break;
		case 'E':
			detach_delay = atoi(optarg);
			break;
		case 'd':
			device_id_filter = optarg;
			break;
		case 'p':
			/* Parse device path */
			dif->path = optarg;
			dif->flags |= DFU_IFF_PATH;
			ret = resolve_device_path(dif);
			if (ret < 0) {
				errx(EX_IOERR, "unable to parse `%s'",
				    optarg);
			}
			if (!ret) {
				errx(EX_IOERR, "cannot find `%s'", optarg);
			}
			break;
		case 'c':
			/* Configuration */
			dif->configuration = atoi(optarg);
			dif->flags |= DFU_IFF_CONFIG;
			break;
		case 'i':
			/* Interface */
			dif->interface = atoi(optarg);
			dif->flags |= DFU_IFF_IFACE;
			break;
		case 'a':
			/* Interface Alternate Setting */
			dif->altsetting = (uint8_t)strtoul(optarg, &end, 0);
			if (*end)
				alt_name = optarg;
			dif->flags |= DFU_IFF_ALT;
			break;
		case 'S': {
			char *remainder = optarg;

			dif->serial = (*remainder == ',') ? 0 : remainder;
			dif->serial_dfu = dif->serial;
			for (; *remainder != '\0'; ++remainder) {
				if (*remainder == ',') {
					*remainder = '\0';
					++remainder;
					dif->serial_dfu = (*remainder == '\0') ? 0 : remainder;
					break;
				}
			}
			break;
		}
		case 't':
			transfer_size = atoi(optarg);
			break;
		case 'U':
			mode = MODE_UPLOAD;
			file.name = optarg;
			break;
		case 'Z':
			expected_size = atoi(optarg);
			break;
		case 'D':
			mode = MODE_DOWNLOAD;
			file.name = optarg;
			break;
		case 'R':
			final_reset = 1;
			break;
		case 's':
			dfuse_options = optarg;
			break;
		default:
			help();
			break;
		}
	}

	print_version();
	if (mode == MODE_VERSION) {
		exit(0);
	}

	if (mode == MODE_NONE) {
		fprintf(stderr, "You need to specify one of -D or -U\n");
		help();
	}

	if (device_id_filter) {
		/* Parse device ID */
		parse_vendprod(dif, device_id_filter);
		printf("Device ID filter: run-time ");
		if (dif->flags & DFU_IFF_VENDOR) {
			printf("%04x", dif->vendor);
		} else {
			printf("xxxx");
		}
		printf(":");
		if (dif->flags & DFU_IFF_PRODUCT) {
			printf("%04x", dif->product);
		} else {
			printf("xxxx");
		}
		printf(", DFU mode ");
		if (dif->flags & DFU_IFF_VENDOR_DFU) {
			printf("%04x", dif->vendor_dfu);
		} else {
			printf("xxxx");
		}
		printf(":");
		if (dif->flags & DFU_IFF_PRODUCT_DFU) {
			printf("%04x", dif->product_dfu);
		} else {
			printf("xxxx");
		}
		printf("\n");
	}

	ret = libusb_init(&ctx);
	if (ret)
		errx(EX_IOERR, "unable to initialize libusb: %i", ret);

	if (verbose > 1) {
		libusb_set_debug(ctx, 255);
	}

	if (mode == MODE_LIST) {
		list_dfu_interfaces(ctx);
		exit(0);
	}

	num_devs = count_dfu_devices(ctx, dif);
	if (num_devs == 0) {
		errx(EX_IOERR, "No DFU capable USB device found");
		exit(1);
	} else if (num_devs > 1) {
		/* We cannot safely support more than one DFU capable device
		 * with same vendor/product ID, since during DFU we need to do
		 * a USB bus reset, after which the target device will get a
		 * new address */
		errx(EX_IOERR, "More than one DFU capable USB device found! "
		       "Try `--list' and specify the serial number "
		       "or disconnect all but one device\n");
		exit(3);
	}
	if (!get_first_dfu_device(ctx, dif))
		exit(3);

	/* We have exactly one device. Its libusb_device is now in dif->dev */

	printf("Opening DFU capable USB device...\n");
	ret = libusb_open(dif->dev, &dif->dev_handle);
	if (ret || !dif->dev_handle)
		errx(EX_IOERR, "Cannot open device");

	/* try to find first DFU interface of device */
	memcpy(&_rt_dif, dif, sizeof(_rt_dif));
	if (!get_first_dfu_if(&_rt_dif))
		exit(1);

	print_dfu_if(&_rt_dif, NULL);

	/* find set of quirks for this device */
	_rt_dif.quirks = get_quirks(_rt_dif.vendor, _rt_dif.product, _rt_dif.bcdDevice);

	/* Obtain run-time DFU functional descriptor without asking device
	 * E.g. Freerunner does not like to be requested at this point */
	ret = get_cached_extra_descriptor(_rt_dif.dev, _rt_dif.configuration,
					  _rt_dif.interface, USB_DT_DFU, 0,
					  (unsigned char *) &func_dfu_rt,
					  sizeof(func_dfu_rt));
	if (ret == 7) {
		/* DFU 1.0 does not have this field */
		printf("Deducing device DFU version from functional descriptor "
		       "length\n");
		func_dfu_rt.bcdDFUVersion = libusb_cpu_to_le16(0x0100);
	} else if (ret < 9) {
		errx(EX_IOERR, "WARNING: Can not find cached DFU functional "
			"descriptor");
		printf("Warning: Assuming DFU version 1.0\n");
		func_dfu_rt.bcdDFUVersion = libusb_cpu_to_le16(0x0100);
	}
	printf("Run-time device DFU version %04x\n",
	       libusb_le16_to_cpu(func_dfu_rt.bcdDFUVersion));

	/* Transition from run-Time mode to DFU mode */
	if (!(_rt_dif.flags & DFU_IFF_DFU)) {
		int err;
		/* In the 'first round' during runtime mode, there can only be one
		* DFU Interface descriptor according to the DFU Spec. */

		/* FIXME: check if the selected device really has only one */

		printf("Claiming USB DFU Runtime Interface...\n");
		if (libusb_claim_interface(_rt_dif.dev_handle, _rt_dif.interface) < 0) {
			errx(EX_IOERR, "Cannot claim interface %d",
				_rt_dif.interface);
			exit(1);
		}

		if (libusb_set_interface_alt_setting(_rt_dif.dev_handle, _rt_dif.interface, 0) < 0) {
			errx(EX_IOERR, "Cannot set alt interface zero");
			exit(1);
		}

		printf("Determining device status: ");

		err = dfu_get_status(_rt_dif.dev_handle, _rt_dif.interface, &status);
		if (err == LIBUSB_ERROR_PIPE) {
			printf("Device does not implement get_status, assuming appIDLE\n");
			status.bStatus = DFU_STATUS_OK;
			status.bwPollTimeout = 0;
			status.bState  = DFU_STATE_appIDLE;
			status.iString = 0;
		} else if (err < 0) {
			errx(EX_IOERR, "error get_status");
			exit(1);
		} else {
			printf("state = %s, status = %d\n",
			       dfu_state_to_string(status.bState), status.bStatus);
		}

		if (!(_rt_dif.quirks & QUIRK_POLLTIMEOUT))
			milli_sleep(status.bwPollTimeout);

		switch (status.bState) {
		case DFU_STATE_appIDLE:
		case DFU_STATE_appDETACH:
			printf("Device really in Runtime Mode, send DFU "
			       "detach request...\n");
			if (dfu_detach(_rt_dif.dev_handle, 
				       _rt_dif.interface, 1000) < 0) {
				errx(EX_IOERR, "error detaching");
				exit(1);
			}
			libusb_release_interface(_rt_dif.dev_handle,
						 _rt_dif.interface);
			if (func_dfu_rt.bmAttributes & USB_DFU_WILL_DETACH) {
				printf("Device will detach and reattach...\n");
			} else {
				printf("Resetting USB...\n");
				ret = libusb_reset_device(_rt_dif.dev_handle);
				if (ret < 0 && ret != LIBUSB_ERROR_NOT_FOUND)
					errx(EX_IOERR, "error resetting "
						"after detach");
			}
			milli_sleep(detach_delay * 1000);
			break;
		case DFU_STATE_dfuERROR:
			printf("dfuERROR, clearing status\n");
			if (dfu_clear_status(_rt_dif.dev_handle,
					     _rt_dif.interface) < 0) {
				errx(EX_IOERR, "error clear_status");
				exit(1);
			}
			/* fall through */
		default:
			errx(EX_IOERR, "WARNING: Runtime device already "
				"in DFU state ?!?");
			goto dfustate;
			break;
		}
		libusb_release_interface(_rt_dif.dev_handle,
					 _rt_dif.interface);
		libusb_close(_rt_dif.dev_handle);
		_rt_dif.dev_handle = NULL;

		if (mode == MODE_DETACH) {
			libusb_exit(ctx);
			exit(0);
		}

		/* Adjust the filter to only match devices in DFU mode */
		dif->flags |= DFU_IFF_DFU;

		if (dif->flags & DFU_IFF_PATH) {
			ret = resolve_device_path(dif);
			if (ret < 0) {
				errx(EX_IOERR, "internal error: cannot re-parse `%s'",
				    dif->path);
				abort();
			}
			if (!ret) {
				errx(EX_IOERR, "Cannot resolve path after RESET?");
				exit(1);
			}
		}

		num_devs = count_dfu_devices(ctx, dif);
		if (num_devs == 0) {
			errx(EX_IOERR, "Lost device after RESET?");
			exit(1);
		} else if (num_devs > 1) {
			errx(EX_IOERR, "More than one DFU capable USB device found!"
				"Try `--list' and specify the serial number "
				"or disconnect all but one device\n");
			exit(1);
		}
		if (!get_first_dfu_device(ctx, dif))
			exit(3);

		printf("Opening DFU USB Device...\n");
		ret = libusb_open(dif->dev, &dif->dev_handle);
		if (ret || !dif->dev_handle) {
			errx(EX_IOERR, "Cannot open device");
		}
	} else {
		/* we're already in DFU mode, so we can skip the detach/reset
		 * procedure */
	}

dfustate:
	if (alt_name) {
		int n;

		n = find_dfu_if(dif->dev, &alt_by_name, alt_name);
		if (!n) {
			errx(EX_IOERR, "No such Alternate Setting: \"%s\"",
			    alt_name);
		}
		if (n < 0) {
			errx(EX_IOERR, "Error %d in name lookup", n);
		}
		dif->altsetting = n-1;
	}

	num_ifs = count_matching_dfu_if(dif);
	if (num_ifs < 0) {
		errx(EX_IOERR, "No matching DFU Interface after RESET?!?");
	} else if (num_ifs > 1 ) {
		printf("Detected interfaces after DFU transition\n");
		list_dfu_interfaces(ctx);
			errx(EX_IOERR, "We have %u DFU Interfaces/Altsettings,"
			" you have to specify one via --intf / --alt"
			" options", num_ifs);
		exit(1);
	}

	if (!get_matching_dfu_if(dif)) {
		errx(EX_IOERR, "Can't find the matching DFU interface/"
			"altsetting");
	}
	print_dfu_if(dif, NULL);
	if (get_alt_name(dif, active_alt_name) > 0)
		dif->alt_name = active_alt_name;
	else
		dif->alt_name = NULL;

#if 0
	printf("Setting Configuration %u...\n", dif->configuration);
	if (libusb_set_configuration(dif->dev_handle, dif->configuration) < 0) {
		errx(EX_IOERR, "Cannot set configuration");
	}
#endif
	printf("Claiming USB DFU Interface...\n");
	if (libusb_claim_interface(dif->dev_handle, dif->interface) < 0) {
		errx(EX_IOERR, "Cannot claim interface");
	}

	printf("Setting Alternate Setting #%d ...\n", dif->altsetting);
	if (libusb_set_interface_alt_setting(dif->dev_handle, dif->interface, dif->altsetting) < 0) {
		errx(EX_IOERR, "Cannot set alternate interface");
	}

status_again:
	printf("Determining device status: ");
	if (dfu_get_status(dif->dev_handle, dif->interface, &status ) < 0) {
		errx(EX_IOERR, "error get_status");
	}
	printf("state = %s, status = %d\n",
	       dfu_state_to_string(status.bState), status.bStatus);
	if (!(dif->quirks & QUIRK_POLLTIMEOUT))
		milli_sleep(status.bwPollTimeout);

	switch (status.bState) {
	case DFU_STATE_appIDLE:
	case DFU_STATE_appDETACH:
		errx(EX_IOERR, "Device still in Runtime Mode!");
		break;
	case DFU_STATE_dfuERROR:
		printf("dfuERROR, clearing status\n");
		if (dfu_clear_status(dif->dev_handle, dif->interface) < 0) {
			errx(EX_IOERR, "error clear_status");
		}
		goto status_again;
		break;
	case DFU_STATE_dfuDNLOAD_IDLE:
	case DFU_STATE_dfuUPLOAD_IDLE:
		printf("aborting previous incomplete transfer\n");
		if (dfu_abort(dif->dev_handle, dif->interface) < 0) {
			errx(EX_IOERR, "can't send DFU_ABORT");
		}
		goto status_again;
		break;
	case DFU_STATE_dfuIDLE:
		printf("dfuIDLE, continuing\n");
		break;
	default:
		break;
	}

	if (DFU_STATUS_OK != status.bStatus ) {
		printf("WARNING: DFU Status: '%s'\n",
			dfu_status_to_string(status.bStatus));
		/* Clear our status & try again. */
		dfu_clear_status(dif->dev_handle, dif->interface);
		dfu_get_status(dif->dev_handle, dif->interface, &status);

		if (DFU_STATUS_OK != status.bStatus) {
			errx(EX_IOERR, "%d", status.bStatus);
		}
		if (!(dif->quirks & QUIRK_POLLTIMEOUT))
			milli_sleep(status.bwPollTimeout);
	}

	/* Get the DFU mode DFU functional descriptor
	 * If it is not found cached, we will request it from the device */
	ret = get_cached_extra_descriptor(dif->dev, dif->configuration,
					  dif->interface, USB_DT_DFU, 0,
					  (unsigned char *) &func_dfu,
					  sizeof(func_dfu));
	if (ret < 7) {
		errx(EX_IOERR, "Error obtaining cached DFU functional "
			"descriptor");
		ret = usb_get_any_descriptor(dif->dev_handle,
					     USB_DT_DFU, 0,
					     (unsigned char *) &func_dfu,
					     sizeof(func_dfu));
	}
	if (ret == 7) {
		printf("Deducing device DFU version from functional descriptor "
		       "length\n");
		func_dfu.bcdDFUVersion = libusb_cpu_to_le16(0x0100);
	} else if (ret < 9) {
		printf("Error obtaining DFU functional descriptor\n");
		printf("Please report this as a bug!\n");
		printf("Warning: Assuming DFU version 1.0\n");
		func_dfu.bcdDFUVersion = libusb_cpu_to_le16(0x0100);
		printf("Warning: Transfer size can not be detected\n");
		func_dfu.wTransferSize = 0;
	}

	if (dif->quirks & QUIRK_FORCE_DFU11)
		func_dfu.bcdDFUVersion = libusb_cpu_to_le16(0x0110);

	printf("DFU mode device DFU version %04x\n",
	       libusb_le16_to_cpu(func_dfu.bcdDFUVersion));

	if (func_dfu.bcdDFUVersion == libusb_cpu_to_le16(0x11a))
		dfuse_device = 1;

	/* If not overridden by the user */
	if (!transfer_size) {
		transfer_size = libusb_le16_to_cpu(func_dfu.wTransferSize);
		if (transfer_size) {
			printf("Device returned transfer size %i\n",
			       transfer_size);
		} else {
			errx(EX_IOERR, "Transfer size must be "
				"specified");
		}
	}

#ifdef HAVE_GETPAGESIZE
/* autotools lie when cross-compiling for Windows using mingw32/64 */
#ifndef __MINGW32__
	/* limitation of Linux usbdevio */
	if ((int)transfer_size > getpagesize()) {
		transfer_size = getpagesize();
		printf("Limited transfer size to %i\n", transfer_size);
	}
#endif /* __MINGW32__ */
#endif /* HAVE_GETPAGESIZE */

	/* DFU specification */
	if (libusb_get_device_descriptor(dif->dev, &desc)) {
		errx(EX_IOERR, "Failed to get device descriptor");
	}
	if (transfer_size < desc.bMaxPacketSize0) {
		transfer_size = desc.bMaxPacketSize0;
		printf("Adjusted transfer size to %i\n", transfer_size);
	}

	switch (mode) {
	case MODE_UPLOAD:
		/* open for "exclusive" writing in a portable way */
		file.filep = fopen(file.name, "ab");
		if (file.filep == NULL) {
			perror(file.name);
			exit(1);
		}
               fseek(file.filep, 0, SEEK_END);
		if (ftell(file.filep)) {
			errx(EX_IOERR, "%s: File exists", file.name);
			fclose(file.filep);
			exit(1);
		}
		if (dfuse_device || dfuse_options) {
		    if (dfuse_do_upload(dif, transfer_size, &file,
					dfuse_options) < 0)
			exit(1);
		} else {
		    if (dfuload_do_upload(dif, transfer_size, &file) < 0)
			exit(1);
		}
		fclose(file.filep);
		break;
	case MODE_DOWNLOAD:
		file.filep = fopen(file.name, "rb");
		if (file.filep == NULL)
			err(EX_IOERR, "Could not open %s for reading", file.name);

		ret = parse_dfu_suffix(&file);
		if (ret < 0)
			exit(1);
		if (ret == 0) {
			errx(EX_IOERR, "Warning: File has no DFU suffix");
		} else if (file.bcdDFU != 0x0100 && file.bcdDFU != 0x11a) {
			errx(EX_IOERR, "Unsupported DFU file revision "
				"%04x", file.bcdDFU);
		}
		if (file.idVendor != 0xffff &&
		    dif->vendor != file.idVendor) {
			errx(EX_IOERR, "Warning: File vendor ID %04x does "
				"not match device %04x", file.idVendor, dif->vendor);
		}
		if (file.idProduct != 0xffff &&
		    dif->product != file.idProduct) {
			errx(EX_IOERR, "Warning: File product ID %04x does "
				"not match device %04x", file.idProduct, dif->product);
		}
		if (dfuse_device || dfuse_options || file.bcdDFU == 0x11a) {
		        if (dfuse_do_dnload(dif, transfer_size, &file,
							dfuse_options) < 0)
				exit(1);
		} else {
			if (dfuload_do_dnload(dif, transfer_size, &file) < 0)
				exit(1);
	 	}
		fclose(file.filep);
		break;
	default:
		errx(EX_IOERR, "Unsupported mode: %u", mode);
		break;
	}

	if (final_reset) {
		if (dfu_detach(dif->dev_handle, dif->interface, 1000) < 0) {
			errx(EX_IOERR, "can't detach");
		}
		printf("Resetting USB to switch back to runtime mode\n");
		ret = libusb_reset_device(dif->dev_handle);
		if (ret < 0 && ret != LIBUSB_ERROR_NOT_FOUND) {
			errx(EX_IOERR, "error resetting after download");
		}
	}

	libusb_close(dif->dev_handle);
	dif->dev_handle = NULL;
	libusb_exit(ctx);
	exit(0);
}


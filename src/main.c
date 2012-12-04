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

int debug;
int verbose = 0;

static void help(void)
{
	printf( "Usage: dfu-util [options] ...\n"
		"  -h --help\t\t\tPrint this help message\n"
		"  -V --version\t\t\tPrint the version number\n"
		"  -v --verbose\t\t\tPrint verbose debug statements\n"
		"  -l --list\t\t\tList the currently attached DFU capable USB devices\n");
	printf(	"  -e --detach\t\t\tDetach the currently attached DFU capable USB devices\n"
		"  -d --device vendor:product\tSpecify Vendor/Product ID of DFU device\n"
		"  -p --path bus-port. ... .port\tSpecify path to DFU device\n"
		"  -c --cfg config_nr\t\tSpecify the Configuration of DFU device\n"
		"  -i --intf intf_nr\t\tSpecify the DFU Interface number\n"
		"  -a --alt alt\t\t\tSpecify the Altsetting of the DFU Interface\n"
		"\t\t\t\tby name or by number\n");
	printf(	"  -t --transfer-size\t\tSpecify the number of bytes per USB Transfer\n"
		"  -U --upload file\t\tRead firmware from device into <file>\n"
		"  -D --download file\t\tWrite firmware from <file> into device\n"
		"  -R --reset\t\t\tIssue USB Reset signalling once we're finished\n"
		"  -s --dfuse-address address\tST DfuSe mode, specify target address for\n"
		"\t\t\t\traw file download or upload. Not applicable for\n"
		"\t\t\t\tDfuSe file (.dfu) downloads\n"
		);
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
	{ "device", 1, 0, 'd' },
	{ "path", 1, 0, 'p' },
	{ "configuration", 1, 0, 'c' },
	{ "cfg", 1, 0, 'c' },
	{ "interface", 1, 0, 'i' },
	{ "intf", 1, 0, 'i' },
	{ "altsetting", 1, 0, 'a' },
	{ "alt", 1, 0, 'a' },
	{ "transfer-size", 1, 0, 't' },
	{ "upload", 1, 0, 'U' },
	{ "download", 1, 0, 'D' },
	{ "reset", 0, 0, 'R' },
	{ "dfuse-address", 1, 0, 's' }
};

int main(int argc, char **argv)
{
	struct dfu_if _rt_dif, _dif, *dif = &_dif;
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

	memset(dif, 0, sizeof(*dif));
	file.name = NULL;

	while (1) {
		int c, option_index = 0;
		c = getopt_long(argc, argv, "hVvled:p:c:i:a:t:U:D:Rs:", opts,
				&option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			help();
			exit(0);
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
		case 'd':
			device_id_filter = optarg;
			break;
		case 'p':
			/* Parse device path */
			dif->path = optarg;
			dif->flags |= DFU_IFF_PATH;
			ret = resolve_device_path(dif);
			if (ret < 0) {
				fprintf(stderr, "unable to parse `%s'\n",
				    optarg);
				exit(2);
			}
			if (!ret) {
				fprintf(stderr, "cannot find `%s'\n", optarg);
				exit(1);
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
			dif->altsetting = strtoul(optarg, &end, 0);
			if (*end)
				alt_name = optarg;
			dif->flags |= DFU_IFF_ALT;
			break;
		case 't':
			transfer_size = atoi(optarg);
			break;
		case 'U':
			mode = MODE_UPLOAD;
			file.name = optarg;
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
			exit(2);
		}
	}

	print_version();
	if (mode == MODE_VERSION) {
		exit(0);
	}

	if (mode == MODE_NONE) {
		fprintf(stderr, "Error: You need to specify one of -D or -U\n\n");
		help();
		exit(2);
	}

	if (device_id_filter) {
		/* Parse device ID */
		parse_vendprod(&dif->vendor, &dif->product, device_id_filter);
		printf("Filter on vendor = 0x%04x product = 0x%04x\n",
		       dif->vendor, dif->product);
		if (dif->vendor)
			dif->flags |= DFU_IFF_VENDOR;
		if (dif->product)
			dif->flags |= DFU_IFF_PRODUCT;
	}

	ret = libusb_init(&ctx);
	if (ret) {
		fprintf(stderr, "unable to initialize libusb: %i\n", ret);
		return EXIT_FAILURE;
	}

	if (verbose > 1) {
		libusb_set_debug(ctx, 255);
	}

	if (mode == MODE_LIST) {
		list_dfu_interfaces(ctx);
		exit(0);
	}

	dfu_init(5000);

	num_devs = count_dfu_devices(ctx, dif);
	if (num_devs == 0) {
		fprintf(stderr, "No DFU capable USB device found\n");
		exit(1);
	} else if (num_devs > 1) {
		/* We cannot safely support more than one DFU capable device
		 * with same vendor/product ID, since during DFU we need to do
		 * a USB bus reset, after which the target device will get a
		 * new address */
		fprintf(stderr, "More than one DFU capable USB device found, "
		       "you might try `--list' and then disconnect all but one "
		       "device\n");
		exit(3);
	}
	if (!get_first_dfu_device(ctx, dif))
		exit(3);

	/* We have exactly one device. Its libusb_device is now in dif->dev */

	printf("Opening DFU capable USB device... ");
	ret = libusb_open(dif->dev, &dif->dev_handle);
	if (ret || !dif->dev_handle) {
		fprintf(stderr, "Cannot open device\n");
		exit(1);
	}

	/* try to find first DFU interface of device */
	memcpy(&_rt_dif, dif, sizeof(_rt_dif));
	if (!get_first_dfu_if(&_rt_dif))
		exit(1);

	printf("ID %04x:%04x\n", _rt_dif.vendor, _rt_dif.product);

	/* find set of quirks for this device */
	set_quirks(_rt_dif.vendor, _rt_dif.product, _rt_dif.bcdDevice);

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
		fprintf(stderr, "WARNING: Can not find cached DFU functional "
			"descriptor\n");
		printf("Warning: Assuming DFU version 1.0\n");
		func_dfu_rt.bcdDFUVersion = libusb_cpu_to_le16(0x0100);
	}
	printf("Run-time device DFU version %04x\n",
	       libusb_le16_to_cpu(func_dfu_rt.bcdDFUVersion));

	/* Transition from run-Time mode to DFU mode */
	if (!(_rt_dif.flags & DFU_IFF_DFU)) {
		/* In the 'first round' during runtime mode, there can only be one
		* DFU Interface descriptor according to the DFU Spec. */

		/* FIXME: check if the selected device really has only one */

		printf("Claiming USB DFU Runtime Interface...\n");
		if (libusb_claim_interface(_rt_dif.dev_handle, _rt_dif.interface) < 0) {
			fprintf(stderr, "Cannot claim interface %d\n",
				_rt_dif.interface);
			exit(1);
		}

		if (libusb_set_interface_alt_setting(_rt_dif.dev_handle, _rt_dif.interface, 0) < 0) {
			fprintf(stderr, "Cannot set alt interface zero\n");
			exit(1);
		}

		printf("Determining device status: ");
		if (dfu_get_status(_rt_dif.dev_handle, _rt_dif.interface, &status ) < 0) {
			fprintf(stderr, "error get_status\n");
			exit(1);
		}
		printf("state = %s, status = %d\n", 
		       dfu_state_to_string(status.bState), status.bStatus);
		if (!(quirks & QUIRK_POLLTIMEOUT))
			milli_sleep(status.bwPollTimeout);

		switch (status.bState) {
		case DFU_STATE_appIDLE:
		case DFU_STATE_appDETACH:
			printf("Device really in Runtime Mode, send DFU "
			       "detach request...\n");
			if (dfu_detach(_rt_dif.dev_handle, 
				       _rt_dif.interface, 1000) < 0) {
				fprintf(stderr, "error detaching\n");
				exit(1);
				break;
			}
			libusb_release_interface(_rt_dif.dev_handle,
						 _rt_dif.interface);
			if (func_dfu_rt.bmAttributes & USB_DFU_WILL_DETACH) {
				printf("Device will detach and reattach...\n");
			} else {
				printf("Resetting USB...\n");
				ret = libusb_reset_device(_rt_dif.dev_handle);
				if (ret < 0 && ret != LIBUSB_ERROR_NOT_FOUND)
					fprintf(stderr, "error resetting "
						"after detach\n");
			}
			milli_sleep(2000);
			break;
		case DFU_STATE_dfuERROR:
			printf("dfuERROR, clearing status\n");
			if (dfu_clear_status(_rt_dif.dev_handle,
					     _rt_dif.interface) < 0) {
				fprintf(stderr, "error clear_status\n");
				exit(1);
				break;
			}
			break;
		default:
			fprintf(stderr, "WARNING: Runtime device already "
				"in DFU state ?!?\n");
			goto dfustate;
			break;
		}
		libusb_release_interface(_rt_dif.dev_handle,
					 _rt_dif.interface);
		libusb_close(_rt_dif.dev_handle);

		if (mode == MODE_DETACH) {
			libusb_exit(ctx);
			exit(0);
		}

		if (dif->flags & DFU_IFF_PATH) {
			ret = resolve_device_path(dif);
			if (ret < 0) {
				fprintf(stderr,
				    "internal error: cannot re-parse `%s'\n",
				    dif->path);
				abort();
			}
			if (!ret) {
				fprintf(stderr,
				    "Cannot resolve path after RESET?\n");
				exit(1);
			}
		}

		num_devs = count_dfu_devices(ctx, dif);
		if (num_devs == 0) {
			fprintf(stderr, "Lost device after RESET?\n");
			exit(1);
		} else if (num_devs > 1) {
			fprintf(stderr, "More than one DFU capable USB "
				"device found, you might try `--list' and "
				"then disconnect all but one device\n");
			exit(1);
		}
		if (!get_first_dfu_device(ctx, dif))
			exit(3);

		printf("Opening DFU USB Device...\n");
		ret = libusb_open(dif->dev, &dif->dev_handle);
		if (ret || !dif->dev_handle) {
			fprintf(stderr, "Cannot open device\n");
			exit(1);
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
			fprintf(stderr, "No such Alternate Setting: \"%s\"\n",
			    alt_name);
			exit(1);
		}
		if (n < 0) {
			fprintf(stderr, "Error %d in name lookup\n", n);
			exit(1);
		}
		dif->altsetting = n-1;
	}

	num_ifs = count_matching_dfu_if(dif);
	if (num_ifs < 0) {
		fprintf(stderr, "No matching DFU Interface after RESET?!?\n");
		exit(1);
	} else if (num_ifs > 1 ) {
		printf("Detected interfaces after DFU transition\n");
		list_dfu_interfaces(ctx);
		fprintf(stderr, "We have %u DFU Interfaces/Altsettings,"
			" you have to specify one via --intf / --alt"
			" options\n", num_ifs);
		exit(1);
	}

	if (!get_matching_dfu_if(dif)) {
		fprintf(stderr, "Can't find the matching DFU interface/"
			"altsetting\n");
		exit(1);
	}
	print_dfu_if(dif, NULL);
	if (get_alt_name(dif, active_alt_name) > 0)
		dif->alt_name = active_alt_name;
	else
		dif->alt_name = NULL;

#if 0
	printf("Setting Configuration %u...\n", dif->configuration);
	if (libusb_set_configuration(dif->dev_handle, dif->configuration) < 0) {
		fprintf(stderr, "Cannot set configuration\n");
		exit(1);
	}
#endif
	printf("Claiming USB DFU Interface...\n");
	if (libusb_claim_interface(dif->dev_handle, dif->interface) < 0) {
		fprintf(stderr, "Cannot claim interface\n");
		exit(1);
	}

	printf("Setting Alternate Setting #%d ...\n", dif->altsetting);
	if (libusb_set_interface_alt_setting(dif->dev_handle, dif->interface, dif->altsetting) < 0) {
		fprintf(stderr, "Cannot set alternate interface\n");
		exit(1);
	}

status_again:
	printf("Determining device status: ");
	if (dfu_get_status(dif->dev_handle, dif->interface, &status ) < 0) {
		fprintf(stderr, "error get_status\n");
		exit(1);
	}
	printf("state = %s, status = %d\n",
	       dfu_state_to_string(status.bState), status.bStatus);
	if (!(quirks & QUIRK_POLLTIMEOUT))
		milli_sleep(status.bwPollTimeout);

	switch (status.bState) {
	case DFU_STATE_appIDLE:
	case DFU_STATE_appDETACH:
		fprintf(stderr, "Device still in Runtime Mode!\n");
		exit(1);
		break;
	case DFU_STATE_dfuERROR:
		printf("dfuERROR, clearing status\n");
		if (dfu_clear_status(dif->dev_handle, dif->interface) < 0) {
			fprintf(stderr, "error clear_status\n");
			exit(1);
		}
		goto status_again;
		break;
	case DFU_STATE_dfuDNLOAD_IDLE:
	case DFU_STATE_dfuUPLOAD_IDLE:
		printf("aborting previous incomplete transfer\n");
		if (dfu_abort(dif->dev_handle, dif->interface) < 0) {
			fprintf(stderr, "can't send DFU_ABORT\n");
			exit(1);
		}
		goto status_again;
		break;
	case DFU_STATE_dfuIDLE:
		printf("dfuIDLE, continuing\n");
		break;
	}

	if (DFU_STATUS_OK != status.bStatus ) {
		printf("WARNING: DFU Status: '%s'\n",
			dfu_status_to_string(status.bStatus));
		/* Clear our status & try again. */
		dfu_clear_status(dif->dev_handle, dif->interface);
		dfu_get_status(dif->dev_handle, dif->interface, &status);

		if (DFU_STATUS_OK != status.bStatus) {
			fprintf(stderr, "Error: %d\n", status.bStatus);
			exit(1);
		}
		if (!(quirks & QUIRK_POLLTIMEOUT))
			milli_sleep(status.bwPollTimeout);
	}

	/* Get the DFU mode DFU functional descriptor
	 * If it is not found cached, we will request it from the device */
	ret = get_cached_extra_descriptor(dif->dev, dif->configuration,
					  dif->interface, USB_DT_DFU, 0,
					  (unsigned char *) &func_dfu,
					  sizeof(func_dfu));
	if (ret < 7) {
		fprintf(stderr, "Error obtaining cached DFU functional "
			"descriptor\n");
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

	if (quirks & QUIRK_FORCE_DFU11)
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
			fprintf(stderr, "Error: Transfer size must be "
				"specified\n");
			exit(1);
		}
	}

#ifdef HAVE_GETPAGESIZE
/* autotools lie when cross-compiling for Windows using mingw32/64 */
#ifndef __MINGW32__
	/* limitation of Linux usbdevio */
	if (transfer_size > getpagesize()) {
		transfer_size = getpagesize();
		printf("Limited transfer size to %i\n", transfer_size);
	}
#endif /* __MINGW32__ */
#endif /* HAVE_GETPAGESIZE */

	/* DFU specification */
	if (libusb_get_device_descriptor(dif->dev, &desc)) {
		fprintf(stderr, "Error: Failed to get device descriptor\n");
		exit(1);
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
		if (ftell(file.filep)) {
			fprintf(stderr, "%s: File exists\n", file.name);
			fclose(file.filep);
			exit(1);
		}
		if (dfuse_device || dfuse_options) {
		    if (dfuse_do_upload(dif, transfer_size, file,
					dfuse_options) < 0)
			exit(1);
		} else {
		    if (dfuload_do_upload(dif, transfer_size, file) < 0)
			exit(1);
		}
		fclose(file.filep);
		break;
	case MODE_DOWNLOAD:
		file.filep = fopen(file.name, "rb");
		if (file.filep == NULL) {
			perror(file.name);
			exit(1);
		}
		ret = parse_dfu_suffix(&file);
		if (ret < 0)
			exit(1);
		if (ret == 0) {
			fprintf(stderr, "Warning: File has no DFU suffix\n");
		} else if (file.bcdDFU != 0x0100 && file.bcdDFU != 0x11a) {
			fprintf(stderr, "Unsupported DFU file revision "
				"%04x\n", file.bcdDFU);
			exit(1);
		}
		if (file.idVendor != 0xffff &&
		    dif->vendor != file.idVendor) {
			fprintf(stderr, "Warning: File vendor ID %04x does "
				"not match device %04x\n", file.idVendor, dif->vendor);
		}
		if (file.idProduct != 0xffff &&
		    dif->product != file.idProduct) {
			fprintf(stderr, "Warning: File product ID %04x does "
				"not match device %04x\n", file.idProduct, dif->product);
		}
		if (dfuse_device || dfuse_options || file.bcdDFU == 0x11a) {
		        if (dfuse_do_dnload(dif, transfer_size, file,
							dfuse_options) < 0)
				exit(1);
		} else {
			if (dfuload_do_dnload(dif, transfer_size, file) < 0)
				exit(1);
	 	}
		fclose(file.filep);
		break;
	default:
		fprintf(stderr, "Unsupported mode: %u\n", mode);
		exit(1);
	}

	if (final_reset) {
		if (dfu_detach(dif->dev_handle, dif->interface, 1000) < 0) {
			fprintf(stderr, "can't detach\n");
		}
		printf("Resetting USB to switch back to runtime mode\n");
		ret = libusb_reset_device(dif->dev_handle);
		if (ret < 0 && ret != LIBUSB_ERROR_NOT_FOUND) {
			fprintf(stderr, "error resetting after download\n");
		}
	}

	libusb_close(dif->dev_handle);
	libusb_exit(ctx);
	exit(0);
}


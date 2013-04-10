/*
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <libusb.h>

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

extern int verbose;

/* Find DFU interfaces in a given device.
 * Iterate through all DFU interfaces and their alternate settings
 * and call the passed handler function on each setting until handler
 * returns non-zero. */
int find_dfu_if(libusb_device *dev,
		       int (*handler)(struct dfu_if *, void *),
		       void *v)
{
	struct libusb_device_descriptor desc;
	struct libusb_config_descriptor *cfg;
	const struct libusb_interface_descriptor *intf;
	const struct libusb_interface *uif;
	struct dfu_if _dif, *dfu_if = &_dif;
	int cfg_idx, intf_idx, alt_idx;
	int rc;

	memset(dfu_if, 0, sizeof(*dfu_if));
	rc = libusb_get_device_descriptor(dev, &desc);
	if (rc)
		return rc;
	for (cfg_idx = 0; cfg_idx < desc.bNumConfigurations;
	     cfg_idx++) {
		rc = libusb_get_config_descriptor(dev, cfg_idx, &cfg);
		if (rc)
			return rc;
		/* in some cases, noticably FreeBSD if uid != 0,
		 * the configuration descriptors are empty */
		if (!cfg)
			return 0;
		for (intf_idx = 0; intf_idx < cfg->bNumInterfaces;
		     intf_idx++) {
			uif = &cfg->interface[intf_idx];
			if (!uif)
				return 0;
			for (alt_idx = 0;
			     alt_idx < uif->num_altsetting; alt_idx++) {
				intf = &uif->altsetting[alt_idx];
				if (!intf)
					return 0;
				if (intf->bInterfaceClass == 0xfe &&
				    intf->bInterfaceSubClass == 1) {
					dfu_if->dev = dev;
					dfu_if->vendor = dfu_if->vendor_dfu = desc.idVendor;
					dfu_if->product = dfu_if->product_dfu = desc.idProduct;
					dfu_if->bcdDevice = desc.bcdDevice;
					dfu_if->configuration = cfg->
							bConfigurationValue;
					dfu_if->interface =
						intf->bInterfaceNumber;
					dfu_if->altsetting =
						intf->bAlternateSetting;
					if (intf->bInterfaceProtocol == 2)
						dfu_if->flags |= DFU_IFF_DFU;
					else
						dfu_if->flags &= ~DFU_IFF_DFU;
					if (!handler)
						return 1;
					rc = handler(dfu_if, v);
					if (rc != 0)
						return rc;
				}
			}
		}

		libusb_free_config_descriptor(cfg);
	}

	return 0;
}

int _get_first_cb(struct dfu_if *dif, void *v)
{
	struct dfu_if *v_dif = (struct dfu_if*) v;

	/* Selectively copy DFU device information */
	if (dif->flags & DFU_IFF_DFU) {
		v_dif->flags |= DFU_IFF_DFU;
		v_dif->vendor_dfu = dif->vendor_dfu;
		v_dif->product_dfu = dif->product_dfu;
		if (!(v_dif->flags & DFU_IFF_VENDOR)) {
			v_dif->vendor = dif->vendor;
		}
		if (!(v_dif->flags & DFU_IFF_PRODUCT)) {
			v_dif->product = dif->product;
		}
	} else {
		v_dif->flags &= ~DFU_IFF_DFU;
		v_dif->vendor = dif->vendor;
		v_dif->product = dif->product;
	}
	v_dif->bcdDevice = dif->bcdDevice;
	v_dif->configuration = dif->configuration;
	v_dif->interface = dif->interface;
	v_dif->altsetting = dif->altsetting;
	v_dif->alt_name = dif->alt_name;
	v_dif->bus = dif->bus;
	v_dif->devnum = dif->devnum;
	v_dif->path = dif->path;
	v_dif->count = dif->count;
	v_dif->dev = dif->dev;

	/* return a value that makes find_dfu_if return immediately */
	return 1;
}

/* Fills in dif with the first found DFU interface */
int get_first_dfu_if(struct dfu_if *dif)
{
	return find_dfu_if(dif->dev, &_get_first_cb, (void *) dif);
}

int _check_match_cb(struct dfu_if *dif, void *v)
{
	struct dfu_if *v_dif = (struct dfu_if*) v;

	if (v_dif->flags & DFU_IFF_IFACE &&
	    dif->interface != v_dif->interface)
		return 0;
	if (v_dif->flags & DFU_IFF_ALT &&
	    dif->altsetting != v_dif->altsetting)
		return 0;
	return _get_first_cb(dif, v);
}

/* Fills in dif from the matching DFU interface/altsetting */
int get_matching_dfu_if(struct dfu_if *dif)
{
	return find_dfu_if(dif->dev, &_check_match_cb, (void *) dif);
}

int _count_match_cb(struct dfu_if *dif, void *v)
{
	struct dfu_if *v_dif = (struct dfu_if*) v;

	if (v_dif->flags & DFU_IFF_IFACE &&
	    dif->interface != v_dif->interface)
		return 0;
	if (v_dif->flags & DFU_IFF_ALT &&
	    dif->altsetting != v_dif->altsetting)
		return 0;
	v_dif->count++;
	return 0;
}

/* Count matching DFU interface/altsetting */
int count_matching_dfu_if(struct dfu_if *dif)
{
	dif->count = 0;
	find_dfu_if(dif->dev, &_count_match_cb, (void *) dif);
	return dif->count;
}

/* Retrieves serial string.
 * Returns string length, or negative on error */
static int get_serial(struct dfu_if *dfu_if, unsigned char *serial)
{
	struct libusb_device_descriptor desc;
	libusb_device *dev = dfu_if->dev;
	libusb_device_handle *dev_handle = dfu_if->dev_handle;
	int ret = -1;

	if (libusb_get_device_descriptor(dev, &desc))
		return -1;
	if (desc.iSerialNumber == 0)
		return -1;
	if (!dev_handle) {
		libusb_open(dfu_if->dev, &dev_handle);
	}
	if (dev_handle) {
		ret = libusb_get_string_descriptor_ascii
			(dev_handle, desc.iSerialNumber, serial, MAX_DESC_STR_LEN);
		if (!dfu_if->dev_handle) {
			libusb_close(dev_handle);
		}
	}
	return ret;
}

/* Retrieves alternate interface name string.
 * Returns string length, or negative on error */
int get_alt_name(struct dfu_if *dfu_if, unsigned char *name)
{
	libusb_device *dev = dfu_if->dev;
	struct libusb_config_descriptor *cfg;
	int alt_name_str_idx;
	int ret;

	ret = libusb_get_config_descriptor_by_value(dev, dfu_if->configuration,
						    &cfg);
	if (ret)
		return ret;

	alt_name_str_idx = cfg->interface[dfu_if->interface].
			       altsetting[dfu_if->altsetting].iInterface;
	ret = -1;
	if (alt_name_str_idx) {
		libusb_device_handle *dev_handle = dfu_if->dev_handle;

		if (!dev_handle) {
			libusb_open(dfu_if->dev, &dev_handle);
		}
		if (dev_handle) {
			ret = libusb_get_string_descriptor_ascii(
					dev_handle, alt_name_str_idx,
					name, MAX_DESC_STR_LEN);
			if (!dfu_if->dev_handle) {
				libusb_close(dev_handle);
			}
		}
	}
	libusb_free_config_descriptor(cfg);
	return ret;
}

int print_dfu_if(struct dfu_if *dfu_if, void *v)
{
	unsigned char name[MAX_DESC_STR_LEN+1] = "UNDEFINED";
	unsigned char serial[MAX_DESC_STR_LEN+1] = "UNDEFINED";

	get_alt_name(dfu_if, name);
	get_serial(dfu_if, serial);

	printf("Found %s: [%04x:%04x] ver=%04X, devnum=%u, cfg=%u, intf=%u, alt=%u,\n"
	       "\tname=\"%s\", serial=\"%s\"\n",
	       dfu_if->flags & DFU_IFF_DFU ? "DFU" : "Runtime",
	       dfu_if->flags & DFU_IFF_DFU ? dfu_if->vendor_dfu : dfu_if->vendor,
	       dfu_if->flags & DFU_IFF_DFU ? dfu_if->product_dfu : dfu_if->product,
	       dfu_if->bcdDevice, dfu_if->devnum,
	       dfu_if->configuration, dfu_if->interface,
	       dfu_if->altsetting, name, serial);
	return 0;
}

/* Walk the device tree and print out DFU devices */
int list_dfu_interfaces(libusb_context *ctx)
{
	libusb_device **list;
	libusb_device *dev;
	ssize_t num_devs, i;

	num_devs = libusb_get_device_list(ctx, &list);

	for (i = 0; i < num_devs; ++i) {
		dev = list[i];
		find_dfu_if(dev, &print_dfu_if, NULL);
	}

	libusb_free_device_list(list, 1);
	return 0;
}

int alt_by_name(struct dfu_if *dfu_if, void *v)
{
	unsigned char name[MAX_DESC_STR_LEN+1];

	if (get_alt_name(dfu_if, name) < 0)
		return 0;
	if (strcmp((char *)name, v))
		return 0;
	/*
	 * Return altsetting+1 so that we can use return value 0 to indicate
	 * "not found".
	 */
	return dfu_if->altsetting+1;
}

int _count_cb(struct dfu_if *dif, void *v)
{
	int *count = (int*) v;
	int new_count = *count;
	int increment = (new_count < 0) ? -1 : 1;

	new_count += increment;
	if ((dif->flags & DFU_IFF_DFU) && (new_count >= 0)) {
		/* DFU mode interface found, make count negative */
		new_count = -new_count;
	}
	*count = new_count;

	return 0;
}

/* Count DFU interfaces within a single device.  Returned value will be
 * negative if the device is in DFU mode. */
int count_dfu_interfaces(libusb_device *dev)
{
	int num_found = 0;

	find_dfu_if(dev, &_count_cb, (void *) &num_found);

	return num_found;
}


/* Iterate over all matching DFU capable devices within system */
int iterate_dfu_devices(libusb_context *ctx, struct dfu_if *dif,
    int (*action)(struct libusb_device *dev, void *user), void *user)
{
	libusb_device **list;
	ssize_t num_devs, i;

	num_devs = libusb_get_device_list(ctx, &list);
	for (i = 0; i < num_devs; ++i) {
		int dfu_count;
		int retval;
		struct libusb_device_descriptor desc;
		struct libusb_device *dev = list[i];

		if (dif && (dif->flags & DFU_IFF_DEVNUM) &&
		    (libusb_get_bus_number(dev) != dif->bus ||
		     libusb_get_device_address(dev) != dif->devnum))
			continue;
		if (libusb_get_device_descriptor(dev, &desc))
			continue;
		dfu_count = count_dfu_interfaces(dev);
		if (dfu_count == 0)
			continue;
		if (dif) {
			const char *serial_filter;

			if ((dif->flags & DFU_IFF_DFU) && dfu_count >= 0)
				continue;
			if (dfu_count < 0) {
				serial_filter = dif->serial_dfu;
				if ( (dif->flags & DFU_IFF_VENDOR_DFU) &&
				     (desc.idVendor != dif->vendor_dfu) )
					continue;
				if ( (dif->flags & DFU_IFF_PRODUCT_DFU) &&
				     (desc.idProduct != dif->product_dfu) )
					continue;
			} else {
				serial_filter = dif->serial;
				if ( (dif->flags & DFU_IFF_VENDOR) &&
				     (desc.idVendor != dif->vendor) )
					continue;
				if ( (dif->flags & DFU_IFF_PRODUCT) &&
				     (desc.idProduct != dif->product) )
					continue;
			}
			if (serial_filter) {
				unsigned char serial[MAX_DESC_STR_LEN+1];
				int ret;

				ret = libusb_open(dev, &dif->dev_handle);
				if (ret || !dif->dev_handle) {
					errx(EX_IOERR, "Cannot open device");
					exit(1);
				}
				if (libusb_get_string_descriptor_ascii(
					dif->dev_handle, desc.iSerialNumber, serial,
					MAX_DESC_STR_LEN) < 0) {
						libusb_close(dif->dev_handle);
						dif->dev_handle = NULL;
						continue;
				}
				libusb_close(dif->dev_handle);
				dif->dev_handle = NULL;
				if (strcmp((char *)serial, serial_filter)) {
					continue;
				}
			}
		}

		retval = action(dev, user);
		if (retval) {
			libusb_free_device_list(list, 0);
			return retval;
		}
	}
	libusb_free_device_list(list, 0);
	return 0;
}


int found_dfu_device(struct libusb_device *dev, void *user)
{
	struct dfu_if *dif = (struct dfu_if*) user;

	dif->dev = dev;
	return 1;
}


/* Find the first DFU-capable device, save it in dfu_if->dev */
int get_first_dfu_device(libusb_context *ctx, struct dfu_if *dif)
{
	return iterate_dfu_devices(ctx, dif, found_dfu_device, dif);
}


int count_one_dfu_device(struct libusb_device *dev, void *user)
{
	int *num = (int*) user;

	(*num)++;
	return 0;
}


/* Count DFU capable devices within system */
int count_dfu_devices(libusb_context *ctx, struct dfu_if *dif)
{
	int num_found = 0;

	iterate_dfu_devices(ctx, dif, count_one_dfu_device, &num_found);
	return num_found;
}


void parse_vendprod(struct dfu_if *dif, const char *str)
{
	char *remainder;

	dif->flags &= ~( DFU_IFF_VENDOR | DFU_IFF_PRODUCT |
			 DFU_IFF_VENDOR_DFU | DFU_IFF_PRODUCT_DFU );
	dif->vendor = dif->vendor_dfu = strtoul(str, &remainder, 16);
	dif->product = dif->product_dfu = 0;
	if (remainder != str) {
		dif->flags |= DFU_IFF_VENDOR | DFU_IFF_VENDOR_DFU;
		str = remainder;
	}
	if (*str == ':') {
		++str;
		dif->product = dif->product_dfu = strtoul(str, &remainder, 16);
		if (remainder != str) {
			dif->flags |= DFU_IFF_PRODUCT | DFU_IFF_PRODUCT_DFU;
			str = remainder;
		}
	}
	if (*str == ',') {
		++str;
		dif->flags &= ~(DFU_IFF_VENDOR_DFU | DFU_IFF_PRODUCT_DFU);
		dif->vendor_dfu = strtoul(str, &remainder, 16);
		if (remainder != str) {
			dif->flags |= DFU_IFF_VENDOR_DFU;
			str = remainder;
		}
		if (*str == ':') {
			++str;
			dif->product_dfu = strtoul(str, &remainder, 16);
			if (remainder != str) {
				dif->flags |= DFU_IFF_PRODUCT_DFU;
				str = remainder;
			}
		}
	}
}


#ifdef HAVE_USBPATH_H

int resolve_device_path(struct dfu_if *dif)
{
	int res;

	res = usb_path2devnum(dif->path);
	if (res < 0)
		return -EINVAL;
	if (!res)
		return 0;

	dif->bus = atoi(dif->path);
	dif->devnum = res;
	dif->flags |= DFU_IFF_DEVNUM;
	return res;
}

#else /* HAVE_USBPATH_H */

int resolve_device_path(struct dfu_if *dif)
{
	errx(EX_IOERR, "USB device paths are not supported by this dfu-util.");
	exit(1);
}

#endif /* !HAVE_USBPATH_H */

/* Look for a descriptor in a concatenated descriptor list
 * Will return desc_index'th match of given descriptor type
 * Returns length of found descriptor, limited to res_size */
int find_descriptor(const unsigned char *desc_list, int list_len,
			   uint8_t desc_type, uint8_t desc_index,
			   uint8_t *res_buf, int res_size)
{
	int p = 0;
	int hit = 0;

	while (p + 1 < list_len) {
		int desclen;

		desclen = (int) desc_list[p];
		if (desclen == 0) {
			errx(EX_IOERR, "Invalid descriptor list");
			return -1;
		}
		if (desc_list[p + 1] == desc_type && hit++ == desc_index) {
			if (desclen > res_size)
				desclen = res_size;
			if (p + desclen > list_len)
				desclen = list_len - p;
			memcpy(res_buf, &desc_list[p], desclen);
			return desclen;
		}
		p += (int) desc_list[p];
	}
	return 0;
}

/* Look for a descriptor in the active configuration
 * Will also find extra descriptors which are normally
 * not returned by the standard libusb_get_descriptor() */
int usb_get_any_descriptor(struct libusb_device_handle *dev_handle,
				  uint8_t desc_type,
				  uint8_t desc_index,
				  unsigned char *resbuf, int res_len)
{
	struct libusb_device *dev;

	dev = libusb_get_device(dev_handle);
	if (!dev) {
		errx(EX_IOERR, "Broken device handle");
		return -1;
	}

	/* Finally try to retrieve it requesting the device directly
	 * This is not supported on all devices for non-standard types */
	return libusb_get_descriptor(dev_handle, desc_type, desc_index,
				     resbuf, res_len);
}

/* Get cached extra descriptor from libusb for an interface
 * Returns length of found descriptor */
int get_cached_extra_descriptor(struct libusb_device *dev,
				       uint8_t bConfValue,
				       uint8_t intf,
				       uint8_t desc_type, uint8_t desc_index,
				       unsigned char *resbuf, int res_len)
{
	struct libusb_config_descriptor *cfg;
	const unsigned char *extra;
	int extra_len;
	int ret;
	int alt;

	ret = libusb_get_config_descriptor_by_value(dev, bConfValue, &cfg);
	if (ret == LIBUSB_ERROR_NOT_FOUND) {
		errx(EX_IOERR, "Device is unconfigured");
		return -1;
	} else if (ret) {
		errx(EX_IOERR, "failed "
			"libusb_config_descriptor_by_value()");
		exit(1);
	}

	ret = find_descriptor(cfg->extra, cfg->extra_length, desc_type,
			      desc_index, resbuf, res_len);
	if (ret > 1) {
		libusb_free_config_descriptor(cfg);
		return (ret);
	}

	/* Extra descriptors can be shared between alternate settings but
	 * libusb may attach them to one setting. Therefore go through all.
	 * Note that desc_index is per alternate setting, hits will not be
	 * counted from one to another */
	for (alt = 0; alt < cfg->interface[intf].num_altsetting;
	     alt++) {
		extra = cfg->interface[intf].altsetting[alt].extra;
		extra_len = cfg->interface[intf].altsetting[alt].extra_length;
		if (extra_len > 1)
			ret = find_descriptor(extra, extra_len, desc_type,
					      desc_index, resbuf, res_len);
		if (ret > 1)
			break;
	}
	libusb_free_config_descriptor(cfg);
	if (ret < 2 && verbose)
		printf("Did not find cached descriptor\n");

	return ret;
}

#ifndef DFU_UTIL_H
#define DFU_UTIL_H

/* USB string descriptor should contain max 126 UTF-16 characters
 * but 253 would even accomodate any UTF-8 encoding */
#define MAX_DESC_STR_LEN 253

enum mode {
	MODE_NONE,
	MODE_VERSION,
	MODE_LIST,
	MODE_DETACH,
	MODE_UPLOAD,
	MODE_DOWNLOAD
};

/* Find DFU interfaces in a given device.
 * Iterate through all DFU interfaces and their alternate settings
 * and call the passed handler function on each setting until handler
 * returns non-zero. */
int find_dfu_if(libusb_device *dev,
		       int (*handler)(struct dfu_if *, void *),
		       void *v);

int _get_first_cb(struct dfu_if *dif, void *v);


/* Fills in dif with the first found DFU interface */
int get_first_dfu_if(struct dfu_if *dif);

int _check_match_cb(struct dfu_if *dif, void *v);

/* Fills in dif from the matching DFU interface/altsetting */
int get_matching_dfu_if(struct dfu_if *dif);

int _count_match_cb(struct dfu_if *dif, void *v);

/* Count matching DFU interface/altsetting */
int count_matching_dfu_if(struct dfu_if *dif);

/* Retrieves alternate interface name string.
 * Returns string length, or negative on error */
int get_alt_name(struct dfu_if *dfu_if, unsigned char *name);

int print_dfu_if(struct dfu_if *dfu_if, void *v);

/* Walk the device tree and print out DFU devices */
int list_dfu_interfaces(libusb_context *ctx);

int alt_by_name(struct dfu_if *dfu_if, void *v);

int _count_cb(struct dfu_if *dif, void *v);

/* Count DFU interfaces within a single device */
int count_dfu_interfaces(libusb_device *dev);

/* Iterate over all matching DFU capable devices within system */
int iterate_dfu_devices(libusb_context *ctx, struct dfu_if *dif,
    int (*action)(struct libusb_device *dev, void *user), void *user);

int found_dfu_device(struct libusb_device *dev, void *user);

/* Find the first DFU-capable device, save it in dfu_if->dev */
int get_first_dfu_device(libusb_context *ctx, struct dfu_if *dif);

int count_one_dfu_device(struct libusb_device *dev, void *user);

/* Count DFU capable devices within system */
int count_dfu_devices(libusb_context *ctx, struct dfu_if *dif);

void parse_vendprod(uint16_t *vendor, uint16_t *product,
			   const char *str);

int resolve_device_path(struct dfu_if *dif);

/* Look for a descriptor in a concatenated descriptor list
 * Will return desc_index'th match of given descriptor type
 * Returns length of found descriptor, limited to res_size */
int find_descriptor(const unsigned char *desc_list, int list_len,
			   uint8_t desc_type, uint8_t desc_index,
			   uint8_t *res_buf, int res_size);

/* Look for a descriptor in the active configuration
 * Will also find extra descriptors which are normally
 * not returned by the standard libusb_get_descriptor() */
int usb_get_any_descriptor(struct libusb_device_handle *dev_handle,
				  uint8_t desc_type,
				  uint8_t desc_index,
				  unsigned char *resbuf, int res_len);


/* Get cached extra descriptor from libusb for an interface
 * Returns length of found descriptor */
int get_cached_extra_descriptor(struct libusb_device *dev,
				       uint8_t bConfValue,
				       uint8_t intf,
				       uint8_t desc_type, uint8_t desc_index,
				       unsigned char *resbuf, int res_len);

#endif /* DFU_UTIL_H */

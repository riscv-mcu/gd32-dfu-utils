
#ifndef DFU_FILE_H
#define DFU_FILE_H

#include <stdint.h>

struct dfu_file {
    /* File name */
    const char *name;
    /* Pointer to file loaded into memory */
    uint8_t *firmware;
    /* Different sizes */
    struct {
	int total;
	int prefix;
	int suffix;
    } size;
    /* From prefix fields */
    uint32_t lmdfu_address;

    /* From DFU suffix fields */
    uint32_t dwCRC;
    uint16_t bcdDFU;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
};

extern int verbose;

void dfu_load_file(struct dfu_file *, int check_suffix, int check_prefix);
void dfu_store_file(struct dfu_file *, int have_suffix, int have_prefix);

void dfu_progress_bar(const char *desc, unsigned long long curr,
		unsigned long long max);
void *dfu_malloc(size_t size);
uint32_t dfu_file_write_crc(int f, uint32_t crc, const void *buf, int size);

#endif /* DFU_FILE_H */

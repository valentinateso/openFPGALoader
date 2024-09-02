// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2022 Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>
 */

#ifndef SRC_LIBUSB_LL_HPP_
#define SRC_LIBUSB_LL_HPP_

#include <libusb.h>

typedef struct {
    uint8_t bus_addr;
    uint8_t dev_addr;
    uint16_t vid;
    uint16_t pid;
    char probe_type[256];
    uint8_t imanufacturer[200];
    uint8_t iserial[200];
    uint8_t iproduct[200];
} usb_scan_item;

class libusb_ll {
	public:
		explicit libusb_ll(int vid = -1, int pid = -1);
		~libusb_ll();

        usb_scan_item** scan(int8_t verbose_level);
        int find(int8_t verbose_level, usb_scan_item* item);
        int available(int8_t verbose_level);

	private:
		struct libusb_context *_usb_ctx;
		bool _verbose;
};

#endif  // SRC_LIBUSB_LL_HPP_

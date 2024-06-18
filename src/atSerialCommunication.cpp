//
// Created by Valentina Bujic on 21.8.23..
//

#include "atSerialCommunication.hpp"
#include "verbose.hpp"

const int ATSerialCommunication::BAUDRATE = 115200;
const int ATSerialCommunication::MESSAGE_SIZE = ATSerialCommunication::BAUDRATE / 512;
const int ATSerialCommunication::LATENCY = 63;

ATSerialCommunication::ATSerialCommunication(const cable_t &cable, int verbose) {
    _ftdi = ftdi_new();
    if (_ftdi == NULL) {
        if (verbose != quiet)
            printf("Error: Unable to create ftdi\r\n");
        return;
    }
    int interface = ftdi_set_interface(_ftdi, (ftdi_interface) cable.config.interface);
    if (interface < 0) {
        if (verbose != quiet) {
            printf("Error: Unable to set interface: %d (%s)\n", interface,
                   (interface == -1) ?
                   "unknown interface"
                                     : (interface == -2) ?
                                       "USB device unavailable"
                                                         : (interface == -3) ?
                                                           "Device already open, interface can't be set in that state"
                                                                             : "unknown error");
        }
        return;
    }

    int ftStatus = ftdi_usb_open(_ftdi, cable.vid, cable.pid);
    if (ftStatus < 0) {
        if (verbose != quiet)
            printf("Error: Can not open usb device: %s\r\n", ftdi_get_error_string(_ftdi));
        return;
    }

    int latency_ret = ftdi_set_latency_timer(_ftdi, ATSerialCommunication::LATENCY);
    if (latency_ret < 0) {
        if (verbose != quiet) {
            printf("Error: Can not set latency timer: %d (%s)\n", latency_ret,
                   (latency_ret == -1) ?
                   "latency out of range (Value should be between 1 and 255)"
                                       : (latency_ret == -2) ?
                                         "unable to set latency timer"
                                                             : (latency_ret == -3) ?
                                                               "USB device unavailable"
                                                                                   : "unknown error");
        }
    }

    int baudrate_ret = ftdi_set_baudrate(_ftdi, ATSerialCommunication::BAUDRATE);
    if (baudrate_ret < 0) {
        if (verbose != quiet) {

            printf("Error: Can not set baud rate: %d (%s)\n", baudrate_ret,
                   (baudrate_ret == -1) ?
                   "invalid baudrate"
                                        : (baudrate_ret == -2) ?
                                          "setting baudrate failed"
                                                               : (baudrate_ret == -3) ?
                                                                 "USB device unavailable"
                                                                                      : "unknown error");
        }
        return;
    }
}

ATSerialCommunication::~ATSerialCommunication() {
    ftdi_usb_close(_ftdi);
    ftdi_deinit(_ftdi);
}

std::string ATSerialCommunication::write_command(unsigned char *command, int len, int verbose) {
    if (verbose > normal)
        printf("Sending: %s\n", command);

    int ret = ftdi_write_data(_ftdi, command, len);
    if (ret < 0) {
        if (verbose != quiet) {
            printf("Error: Failed to write data: %d (%s)\n", ret,
                   (ret == -666) ?
                   "USB device unavailable"
                                 : "error code from usb_bulk_write()");
        }
        return "NULL";
    } else if (ret != len) {
        if (verbose != quiet)
            printf("Error: Failed to write data: Only %d out of %d bytes are written\n", ret, len);
        return "NULL";
    }


    unsigned char u_response[ATSerialCommunication::MESSAGE_SIZE];
    ret = ftdi_read_data(_ftdi, u_response, ATSerialCommunication::MESSAGE_SIZE);

    int tries = 0;
    while (ret == 0 && tries < 50) {
        if (verbose != quiet)
            printf("Warning: Got empty response. Reading again! %d\n", ++tries);
        ret = ftdi_read_data(_ftdi, u_response, ATSerialCommunication::MESSAGE_SIZE);
    }

    if (ret <= 0) {
        if (verbose != quiet) {
            printf("Error: Failed to read data: %d (%s)\n", ret,
                   (ret == 0) ?
                   "no data was available"
                              : (ret == -666) ?
                                "USB device unavailable"
                                              : "error code from libusb_bulk_transfer()");
        }
        return "NULL";
    }

    if (verbose) {
        if (verbose > quiet)
            printf("Response: %s\n", u_response);
    }

    char response[ATSerialCommunication::MESSAGE_SIZE];
    snprintf(response, sizeof u_response, "%s", u_response);
    return response;
}
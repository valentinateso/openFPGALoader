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
            printf("Unable to create ftdi\r\n");
        return;
    }
    int interface = ftdi_set_interface(_ftdi, (ftdi_interface) cable.config.interface);
    if (interface < 0) {
        if (verbose != quiet)
            printf("Unable to set interface\r\n");
        return;
    }

    int ftStatus = ftdi_usb_open(_ftdi, cable.vid, cable.pid);
    if (ftStatus < 0) {
        if (verbose != quiet)
            printf("Error opening usb device: %s\r\n", ftdi_get_error_string(_ftdi));
        return;
    }

    int latency = ftdi_set_latency_timer(_ftdi, ATSerialCommunication::LATENCY);

    if (ftdi_set_baudrate(_ftdi, ATSerialCommunication::BAUDRATE) < 0) {
        if (verbose != quiet)
            printf("Error setting baud rate! \n");
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
    if (ret != len) {
        if (verbose != quiet)
            printf("Write error: %d (written %d)\n", ret, len);
        return "NULL";
    }


    unsigned char u_response[ATSerialCommunication::MESSAGE_SIZE];
    ret = ftdi_read_data(_ftdi, u_response, ATSerialCommunication::MESSAGE_SIZE);

    int tries = 0;
    while (ret == 0) {
        if (verbose != quiet)
            printf("Got empty response. Reading again! %d\n", ++tries);
        ret = ftdi_read_data(_ftdi, u_response, ATSerialCommunication::MESSAGE_SIZE);
    }

    if (ret <= 0) {
        if (verbose != quiet)
            printf("Read error: %s\n", std::to_string(ret).c_str());
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
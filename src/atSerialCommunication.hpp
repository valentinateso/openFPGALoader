//
// Created by Valentina Bujic on 21.8.23..
//

#ifndef OPENFPGALOADER_ATSERIALCOMMUNICATION_HPP
#define OPENFPGALOADER_ATSERIALCOMMUNICATION_HPP

#include "ftdipp_mpsse.hpp"

class ATSerialCommunication {
public:
    ATSerialCommunication(const cable_t &cable, bool verbose);
    ~ATSerialCommunication();
    std::string write_command(unsigned char* command, int len, bool verbose);

protected:
    struct ftdi_context *_ftdi;

};


#endif //OPENFPGALOADER_ATSERIALCOMMUNICATION_HPP

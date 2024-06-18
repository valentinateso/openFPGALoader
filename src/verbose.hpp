//
// Created by Valentina Bujic on 17.6.24..
//

#ifndef OPENFPGALOADER_VERBOSE_H
#define OPENFPGALOADER_VERBOSE_H

enum verbose_level {
    quiet = -1,
    normal = 0,
    verbose = 1,
    debug = 2
};

#define LOG_ERR(fmt, ...)                                               \
    fprintf(stderr, "[ERROR] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

#define LOG_WARNING(fmt, ...)                                               \
    fprintf(stderr, "[WARNING] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...)                                               \
    fprintf(stderr, "[INFO] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)


#endif //OPENFPGALOADER_VERBOSE_H

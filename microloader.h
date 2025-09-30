#ifdef TW_AMONET
#ifndef RECOVERY_AMONET_MICROLOADER_H_
#define RECOVERY_AMONET_MICROLOADER_H_

#include <unistd.h>

#ifdef TW_MICROLOADER
extern uint8_t microloader_bin[1024];
#else
uint8_t microloader_bin[1024];
#endif

#endif // RECOVERY_AMONET_MICROLOADER_H
#endif // TW_AMONET

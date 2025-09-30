#ifdef TW_AMONET
#ifndef RECOVERY_AMONET_H_
#define RECOVERY_AMONET_H_

#include <unistd.h>

#define EXPLOIT_TAG "[amonet] "
int patch_part(const char* part_path);
int unpatch_part(const char* part_path);
int load_microloader(void);

#endif  // RECOVERY_AMONET_H_
#endif // TW_AMONET

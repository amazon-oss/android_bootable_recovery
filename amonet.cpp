#ifdef TW_AMONET

#include <string.h>

#include "amonet.h"
#include "microloader.h"
#include "twcommon.h"
#include "partitions.hpp"

int unpatch_part(const char* part_path) {
  FILE *fp = NULL;
  uint8_t boot_data[0x800];
  int ret = -1;

  const char *part_name = &part_path[1];
  
  TWPartition* partition = PartitionManager.Find_Partition_By_Path(part_path);
  const char* amonet_part = partition?partition->Actual_Block_Device.c_str():"";

  gui_print_color("highlight", EXPLOIT_TAG "Remove %s patch...\n", part_name);

  fp = fopen(amonet_part, "r+b");
  if (!fp) {
    gui_print_color("highlight", EXPLOIT_TAG "Failed to open the %s device\n", part_name);
    goto cleanup;
  }

  if (fread(boot_data, sizeof(boot_data), 1, fp) != 1) {
    gui_print_color("highlight", EXPLOIT_TAG "Failed to read data\n");
    goto cleanup;
  }

  if (memcmp(boot_data + 0x400, "ANDROID!", 8) != 0) {
    // Exploit not installed yet, but that's okay
    gui_print_color("highlight", EXPLOIT_TAG "NOT_INSTALLED\n");
    ret = 0;
    goto cleanup;
  }

  // Assume exploit is installed. Uninstall it by copying the second 0x400 over the first 0x400
  memcpy(boot_data, boot_data + 0x400, 0x400);
  // and zero out the second 0x400
  memset(boot_data + 0x400, 0, 0x400);

  if (fseek(fp, 0, SEEK_SET) != 0) {
    gui_print_color("highlight", EXPLOIT_TAG "Failed to seek\n");
    goto cleanup;
  }

  if (fwrite(boot_data, sizeof(boot_data), 1, fp) != 1) {
    gui_print_color("highlight", EXPLOIT_TAG "Failed to write data\n");
    goto cleanup;
  }

  gui_print_color("highlight", EXPLOIT_TAG "OK\n");
  ret = 0;

cleanup:
  if (fp) {
    fclose(fp);
    fp = NULL;
  }

  return ret;
}

int patch_part(const char* part_path) {
  FILE *fp = NULL;
  uint8_t boot_data[0x800];
  int ret = -1;

  const char *part_name = &part_path[1];
  
  TWPartition* partition = PartitionManager.Find_Partition_By_Path(part_path);
  const char *amonet_part = partition?partition->Actual_Block_Device.c_str():"";
  
  gui_print_color("highlight", EXPLOIT_TAG "Install %s patch... \n", part_name);

  fp = fopen(amonet_part, "r+b");
  if (!fp) {
    gui_print_color("highlight", EXPLOIT_TAG "Failed to open the %s device\n", part_name);
    goto cleanup;
  }

  if (fread(boot_data, sizeof(boot_data), 1, fp) != 1) {
    gui_print_color("highlight", EXPLOIT_TAG "Failed to read data\n");
    goto cleanup;
  }

  if (memcmp(boot_data + 0x400, "ANDROID!", 8) == 0) {
    gui_print_color("highlight", EXPLOIT_TAG "ALREADY_INSTALLED\n"); // If the rom author injected the boot image herself
    ret = 0;
    goto cleanup;
  }

  // Copy first half to the second half, replace first half with the microloader
  memcpy(boot_data + 0x400, boot_data, 0x400);
  memcpy(boot_data, microloader_bin, 0x400);

  if (fseek(fp, 0, SEEK_SET) != 0) {
    gui_print_color("highlight", EXPLOIT_TAG "Failed to seek\n");
    goto cleanup;
  }

  if (fwrite(boot_data, sizeof(boot_data), 1, fp) != 1) {
    gui_print_color("highlight", EXPLOIT_TAG "Failed to write data\n");
    goto cleanup;
  }

  gui_print_color("highlight", EXPLOIT_TAG "OK\n");
  ret = 0;

cleanup:
  if (fp) {
    fclose(fp);
    fp = NULL;
  }

  return ret;
}

int load_microloader() {
#ifdef TW_MICROLOADER
  return 0;
#else
  FILE *fp = NULL;
  uint8_t boot_data[0x800];
  int ret = -1;
  static const char *part_path = "/recovery";

  const char *part_name = &part_path[1];
  
  TWPartition* partition = PartitionManager.Find_Partition_By_Path(part_path);
  const char *amonet_part = partition?partition->Actual_Block_Device.c_str():"";

  gui_print_color("highlight", EXPLOIT_TAG "Load microloader from %s... \n", part_name);

  fp = fopen(amonet_part, "r+b");
  if (!fp) {
    gui_print_color("highlight", EXPLOIT_TAG "Failed to open the %s device\n", part_name);
    goto cleanup;
  }

  if (fread(boot_data, sizeof(boot_data), 1, fp) != 1) {
    gui_print_color("highlight", EXPLOIT_TAG "Failed to read data\n");
    goto cleanup;
  }

  if (memcmp(boot_data + 0x400, "ANDROID!", 8) != 0) {
    gui_print_color("highlight", EXPLOIT_TAG "No microloader found in recovery\n");
    ret = 0;
    goto cleanup;
  }

  if(memcpy(microloader_bin, boot_data, 0x400))
    ret = 0;

	
cleanup:
  if (fp) {
    fclose(fp);
    fp = NULL;
  }

  return ret;
#endif
}

#endif

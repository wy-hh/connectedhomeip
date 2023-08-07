
#include <stdbool.h>
#include <bl_boot2.h>
#include <bl_mtd.h>
#include <hal_boot2.h>
#include <hosal_efuse.h>
#include <bl602_romdriver.h>

#include <mbedtls/sha256.h>
#include <xz_api.h>
#include "uncompress_lzma2.h"

#define BOOT_HEADER_SIZE_BL602 176
#define BOOT_HEADER_BL602_HASH_SIZE  64
#define EFUSE_PUBLIC_KEY_HASH_OFFSET 0x1C
#define BOOT_HEADER_SIZE_BL702 176
#define BOOT_HEADER_BL702_HASH_SIZE  64
#define BOOT_PARTITION_FW_NAME     ("FW")

extern uint8_t _ld_ram_addr0;
extern uint8_t _ld_ram_addr1;

uint8_t xz_cache[4096 * 20];
uint8_t xz_dest[1024];

bool ota_image_get_public_key(uint8_t key[], uint32_t *p_len) 
{
    HALPartition_Entry_Config ptEntry;
	bl_mtd_handle_t fwHandle = NULL;
    mbedtls_sha256_context sha256Conext;
    uint8_t publicKey[BOOT_HEADER_BL602_HASH_SIZE];
    uint8_t publicKeyHash[32];
    bl_mtd_info_t mtdInfo;

   	int iret = hal_boot2_get_active_entries(BOOT2_PARTITION_TYPE_FW, &ptEntry);

    printf ("===========================================================\r\n");

    if (PT_ERROR_SUCCESS != iret) {
    	printf ("iret = %d\r\n", iret);
    	return false;
    }

    printf ("type = %d\r\n", ptEntry.type);
    printf ("device = %d\r\n", ptEntry.device);
    printf ("activeIndex = %d\r\n", ptEntry.activeIndex);
    printf ("name = %s\r\n", ptEntry.name);
    printf ("Address@0 = %08lx, maxlen = %ld\r\n", ptEntry.Address[0], ptEntry.maxLen[0]);
    printf ("Address@1 = %08lx, maxlen = %ld\r\n", ptEntry.Address[1], ptEntry.maxLen[1]);
    printf ("len = %ld\r\n", ptEntry.len);
    printf ("age = %ld\r\n", ptEntry.age);

    printf ("===========================================================\r\n");

    bl_mtd_open(BOOT_PARTITION_FW_NAME, &(fwHandle), BL_MTD_OPEN_FLAG_NONE);
    if (NULL == fwHandle) {
    	return false;
    }

    bl_mtd_info(fwHandle, &mtdInfo);
    uint8_t * xipaddr_base = (uint8_t *)((uint32_t)(&_ld_ram_addr0) - RomDriver_SF_Ctrl_Get_Flash_Image_Offset() + mtdInfo.offset);
    printf ("offset = %x, %p\r\n", mtdInfo.offset, xipaddr_base);

    bl_mtd_read(fwHandle, BOOT_HEADER_SIZE_BL602, sizeof(publicKey), publicKey);

	for (int i = 0; i < sizeof(publicKey); i ++) {
		printf ("%02x %02x\r\n", publicKey[i], xipaddr_base[BOOT_HEADER_SIZE_BL602 + i]);
	}
    bl_mtd_close(fwHandle);

    memset(&sha256Conext, 0 ,sizeof(mbedtls_sha256_context));
    mbedtls_sha256_init(&sha256Conext);
    if (0 != mbedtls_sha256_starts_ret(&sha256Conext, 0)) {
        printf ("mbedtls_sha256_starts_ret starts failed\r\n");
        return false;
    }

    if (0 != mbedtls_sha256_update_ret(&sha256Conext, publicKey, sizeof(publicKey))) {
        printf ("mbedtls_sha256_update_ret update failed\r\n");
        return false; 
    }

    if (0 != mbedtls_sha256_finish_ret(&sha256Conext, publicKeyHash)) {
        printf ("mbedtls_sha256_finish_ret update failed\r\n");
        return false; 
    }
    mbedtls_sha256_free(&sha256Conext);

    printf ("===========================================================\r\n");
    memcpy(publicKey, publicKeyHash, sizeof(publicKeyHash));

    for (int i = 0; i < sizeof(publicKeyHash); i ++) {
        printf ("%02x\r\n", publicKey[i]);
    }

    hosal_efuse_read(EFUSE_PUBLIC_KEY_HASH_OFFSET, (uint32_t *)publicKeyHash, sizeof(publicKeyHash));

    printf ("===========================================================, %d\r\n", memcmp(publicKey, publicKeyHash, sizeof(publicKeyHash)));
    for (int i = 0; i < sizeof(publicKeyHash); i ++) {
        printf ("%02x\r\n", publicKeyHash[i]);
    }

    bl_mtd_open(BOOT_PARTITION_FW_NAME, &(fwHandle), BL_MTD_OPEN_FLAG_BACKUP);
    bl_mtd_info(fwHandle, &mtdInfo);
    xipaddr_base = (uint8_t *)((uint32_t)(&_ld_ram_addr0) + ptEntry.Address[1] - ptEntry.Address[0]);

    enum uncompress_status status = uncompress_lzma2(xipaddr_base, NULL, xz_cache, NULL);

    // uint32_t xz_dest_size = 0;
    // iret = xz_decompress(xz_cache, sizeof(xz_cache), xipaddr_base, xz_dest, &xz_dest_size);
    printf ("status = %d\r\n", status);
    // printf ("===========================================================\r\n");

    for (int i = 0; i < sizeof(publicKeyHash); i ++) {
        printf ("%02x\r\n", xz_cache[BOOT_HEADER_SIZE_BL602 + i]);
    }

    return true;
}
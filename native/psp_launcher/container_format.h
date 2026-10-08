#ifndef TH10_PSP_UNIFIED_CONTAINER_FORMAT_H
#define TH10_PSP_UNIFIED_CONTAINER_FORMAT_H

#include <stdint.h>

/*
 * DATA.PSAR format v2.  All integers are little-endian and every payload
 * offset is absolute from the first byte of DATA.PSAR (not from the PBP).
 * The table contains two runtime PBPs plus the fixed Slim+ GE4 companion.
 */
#define TH10_UNIFIED_PSAR_MAGIC "TH10UP02"
#define TH10_UNIFIED_PSAR_MAGIC_BYTES 8u
#define TH10_UNIFIED_PSAR_VERSION 2u
#define TH10_UNIFIED_PSAR_ENTRY_COUNT 3u

#define TH10_UNIFIED_PROFILE_PSP1000 0x00001000u
#define TH10_UNIFIED_PROFILE_PSP2000_PLUS 0x00002000u
#define TH10_UNIFIED_COMPANION_GE4 0x00004734u

/*
 * Exact build/final60/go_me1_slimplus_pc_20260902/run1 companion. The PSP
 * enforces this size+CRC before launch; the offline packer/auditor additionally
 * pin SHA-256 3DC5C753...D841D.
 */
#define TH10_UNIFIED_GE4_SIZE 2150u
#define TH10_UNIFIED_GE4_CRC32 0xDAEBF3F3u

#pragma pack(push, 1)
typedef struct Th10UnifiedPsarEntry {
    uint32_t profile_id;
    uint32_t model_min;
    uint32_t model_max;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint32_t payload_crc32;
} Th10UnifiedPsarEntry;

typedef struct Th10UnifiedPsarHeader {
    char magic[TH10_UNIFIED_PSAR_MAGIC_BYTES];
    uint32_t version;
    uint32_t entry_count;
    Th10UnifiedPsarEntry entries[TH10_UNIFIED_PSAR_ENTRY_COUNT];
} Th10UnifiedPsarHeader;
#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(Th10UnifiedPsarEntry) == 24u,
              "DATA.PSAR entry layout changed");
static_assert(sizeof(Th10UnifiedPsarHeader) == 88u,
              "DATA.PSAR header layout changed");
#endif

#endif

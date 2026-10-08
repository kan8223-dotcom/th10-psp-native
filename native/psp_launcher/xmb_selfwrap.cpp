#include "xmb_selfwrap.hpp"
#include "xmb_sound.hpp"

#include <pspiofilemgr.h>

#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C"
{
#include <png.h>
#include <zlib.h>
}

namespace
{
// 東方風神録 v1.00a, the release the runtime is built against.
constexpr std::uint64_t kTh10DatBytes = 27696219ull;
constexpr std::uint64_t kThBgmDatBytes = 403789620ull;
constexpr std::uint32_t kPbpHeaderBytes = 40u;
constexpr std::uint32_t kMaxSfoBytes = 64u * 1024u;
constexpr std::uint32_t kPsfHeaderBytes = 20u;
constexpr std::uint32_t kPsfEntryBytes = 16u;
constexpr std::uint16_t kPsfBinaryFormat = 0x0004u;
constexpr std::uint32_t kIconSlotBytes = 64u * 1024u;
constexpr std::uint32_t kPictureSlotBytes = 512u * 1024u;
// SND0 stays below 500,000 bytes, the size usually given as the XMB's limit
// for SND0.AT3 (not measured here).
constexpr std::uint32_t kSoundSlotBytes = 480u * 1024u;
constexpr std::size_t kMaxPath = 640u;
constexpr std::uint32_t kMaxThaEntries = 4096u;
constexpr std::uint32_t kMaxThaIndexBytes = 1024u * 1024u;
constexpr std::uint32_t kMaxTitleBytes = 8u * 1024u * 1024u;
constexpr std::uint32_t kMaxTextureDimension = 1024u;
// Every archive cipher changes at most the first 30720 bytes of an entry.
constexpr std::uint32_t kThaHeadBytes = 32u * 1024u;
constexpr std::uint32_t kMaxRunFileBytes = 8u * 1024u;
constexpr std::uint32_t kMaxBgmFormatBytes = 64u * 1024u;
constexpr std::uint32_t kBgmFormatRecordBytes = 52u;
// The XMB music: the title screen's theme (platform/Title.cpp plays
// bgm/th10_02.wav) from its first sample: 27 s, a 20 ms fade-in so the XMB's
// loop restarts without a click, and a 3 s fade-out.
constexpr char kTitleThemeName[] = "th10_02.wav";
constexpr std::uint32_t kClipStartBytes = 0u;
constexpr std::uint32_t kClipSeconds = 27u;
constexpr std::uint32_t kClipFadeInSamples = 882u;
constexpr std::uint32_t kClipFadeOutSamples = 3u * 44100u;
// The neutral SND0: about a second of the encoder's silent frame.
constexpr std::uint32_t kPlaceholderSoundFrames = 43u;
// The identity chunk behind the frames: header, 8-byte identity, role, and
// at least one padding byte so the chunk keeps RIFF's even size.
constexpr std::size_t kAt3IdentityChunkBytes = 8u + 10u;

constexpr unsigned char kPbpMagic[4] = {0x00u, 'P', 'B', 'P'};
constexpr unsigned char kPngMagic[8] = {
    0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au};
constexpr char kXmbMarker[8] = {'T', 'H', '1', '0', 'X', 'M', 'B', '3'};
constexpr unsigned char kSelfwrapChunkType[4] = {'t', 'h', 'S', 'b'};
constexpr unsigned char kPlaceholderIdentity[8] = {
    'T', 'H', '1', '0', 'P', 'L', 'N', '3'};
constexpr unsigned char kWrappedIdentity[8] = {
    'T', 'H', '1', '0', 'X', 'M', 'B', '3'};
constexpr char kXmbSfoKey[] = "TH10_XMB_SLOT";

enum SelfwrapError
{
    kSelfwrapBadArgument = -1,
    kSelfwrapBadPbp = -2,
    kSelfwrapSourceFailure = -3,
    kSelfwrapImageFailure = -4,
    kSelfwrapReserveFailure = -5,
    kSelfwrapCommitFailure = -6,
    // The running PBP is readable but its backing VFS refuses a write-open.
    // Leave the canonical file untouched and defer self-wrapping; callers
    // continue launching the neutral executable normally.
    kSelfwrapWriteOpenDenied = -7,
    // The fixed media slots contain complete PNGs without our private
    // ownership identity. Never overwrite user/foreign media implicitly.
    kSelfwrapForeignMedia = -8,
};

static_assert(static_cast<int>(kSelfwrapWriteOpenDenied) ==
                  TH10_UNIFIED_SELFWRAP_DEFERRED,
              "launcher/selfwrap deferred result mismatch");

struct ByteBuffer
{
    unsigned char *data;
    std::size_t size;
    std::size_t capacity;
};

struct Image
{
    std::uint32_t width;
    std::uint32_t height;
    unsigned char *rgba;
};

struct ThaEntry
{
    std::uint32_t offset;
    std::uint32_t packed_size;
    std::uint32_t size;
    std::uint32_t cipher;
    bool found;
};

struct PbpInfo
{
    unsigned char header[kPbpHeaderBytes];
    std::uint32_t offsets[8];
    std::uint32_t reserve_start;
    std::uint32_t reserve_end;
    std::uint64_t file_size;
    bool foreign_media;
    bool placeholder;
    bool wrapped;
};

enum class PngIdentity
{
    Invalid,
    Foreign,
    Placeholder,
    Wrapped,
};

void FreeBuffer(ByteBuffer *buffer)
{
    if (!buffer) return;
    std::free(buffer->data);
    buffer->data = nullptr;
    buffer->size = 0u;
    buffer->capacity = 0u;
}

void FreeImage(Image *image)
{
    if (!image) return;
    std::free(image->rgba);
    image->rgba = nullptr;
    image->width = 0u;
    image->height = 0u;
}

std::uint16_t ReadLe16(const unsigned char *data)
{
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(data[1] << 8u);
}

std::uint32_t ReadLe32(const unsigned char *data)
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8u) |
           (static_cast<std::uint32_t>(data[2]) << 16u) |
           (static_cast<std::uint32_t>(data[3]) << 24u);
}

std::uint32_t ReadBe32(const unsigned char *data)
{
    return (static_cast<std::uint32_t>(data[0]) << 24u) |
           (static_cast<std::uint32_t>(data[1]) << 16u) |
           (static_cast<std::uint32_t>(data[2]) << 8u) |
           static_cast<std::uint32_t>(data[3]);
}

void WriteBe32(unsigned char *data, std::uint32_t value)
{
    data[0] = static_cast<unsigned char>(value >> 24u);
    data[1] = static_cast<unsigned char>(value >> 16u);
    data[2] = static_cast<unsigned char>(value >> 8u);
    data[3] = static_cast<unsigned char>(value);
}

bool CheckedImageBytes(std::uint32_t width, std::uint32_t height,
                       std::size_t channels, std::size_t *bytes)
{
    if (!width || !height || !channels) return false;
    const std::uint64_t total = static_cast<std::uint64_t>(width) * height * channels;
    if (total > static_cast<std::uint64_t>(SIZE_MAX)) return false;
    *bytes = static_cast<std::size_t>(total);
    return true;
}

bool ReadExact(SceUID fd, void *destination, std::size_t bytes)
{
    auto *out = static_cast<unsigned char *>(destination);
    std::size_t done = 0u;
    while (done < bytes)
    {
        const std::size_t remaining = bytes - done;
        const unsigned int request = remaining > 0x7fffffffu
                                         ? 0x7fffffffu
                                         : static_cast<unsigned int>(remaining);
        const int got = sceIoRead(fd, out + done, request);
        if (got <= 0) return false;
        done += static_cast<std::size_t>(got);
    }
    return true;
}

bool ReadExactAt(SceUID fd, std::uint64_t offset, void *destination,
                 std::size_t bytes)
{
    if (offset > 0x7fffffffffffffffull) return false;
    const SceOff wanted = static_cast<SceOff>(offset);
    if (sceIoLseek(fd, wanted, PSP_SEEK_SET) != wanted) return false;
    return ReadExact(fd, destination, bytes);
}

bool WriteExact(SceUID fd, const void *source, std::size_t bytes)
{
    const auto *in = static_cast<const unsigned char *>(source);
    std::size_t done = 0u;
    while (done < bytes)
    {
        const std::size_t remaining = bytes - done;
        const unsigned int request = remaining > 0x7fffffffu
                                         ? 0x7fffffffu
                                         : static_cast<unsigned int>(remaining);
        const int wrote = sceIoWrite(fd, in + done, request);
        if (wrote <= 0) return false;
        done += static_cast<std::size_t>(wrote);
    }
    return true;
}

bool WriteExactAt(SceUID fd, std::uint64_t offset, const void *source,
                  std::size_t bytes)
{
    if (offset > 0x7fffffffffffffffull) return false;
    const SceOff wanted = static_cast<SceOff>(offset);
    if (sceIoLseek(fd, wanted, PSP_SEEK_SET) != wanted) return false;
    return WriteExact(fd, source, bytes);
}

bool JoinPath(char *out, std::size_t out_size, const char *root,
              const char *leaf)
{
    if (!out || !out_size || !root || !root[0] || !leaf || !leaf[0]) return false;
    const std::size_t root_length = std::strlen(root);
    const bool has_slash = root_length && root[root_length - 1u] == '/';
    const int length = std::snprintf(out, out_size, "%s%s%s", root,
                                     has_slash ? "" : "/", leaf);
    return length >= 0 && static_cast<std::size_t>(length) < out_size;
}

bool CopyPath(char *out, std::size_t out_size, const char *path)
{
    if (!out || !out_size || !path) return false;
    const int length = std::snprintf(out, out_size, "%s", path);
    return length >= 0 && static_cast<std::size_t>(length) < out_size;
}

bool HasHeaderAndSize(const char *path, const char magic[4],
                      std::uint64_t expected_size)
{
    SceIoStat stat{};
    if (!path || sceIoGetstat(path, &stat) < 0 ||
        static_cast<std::uint64_t>(stat.st_size) != expected_size)
    {
        return false;
    }
    const SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return false;
    char header[4];
    bool ok = ReadExact(fd, header, sizeof(header)) &&
              std::memcmp(header, magic, sizeof(header)) == 0;
    if (sceIoClose(fd) < 0) ok = false;
    return ok;
}

// th10.dat (THA1) is read as the runtime reads it: game/ResourceCodec.cpp
// transform_resource, game/ArchiveLifecycle.cpp load_index,
// game/ResourceArchive.cpp read and the cipher table in
// platform/FileSystem.cpp.
struct ThaCipher
{
    unsigned char key;
    unsigned char step;
    std::int32_t block;
    std::int32_t limit;
};

constexpr std::uint32_t kThaMagic = 0x31414854u; // "THA1"
constexpr ThaCipher kThaHeaderCipher = {0x1bu, 0x37u, 16, 16};
constexpr ThaCipher kThaEntryCiphers[8] = {
    {0x1bu, 0x37u, 64, 10240}, {0x51u, 0xe9u, 64, 12288},
    {0xc1u, 0x51u, 128, 12800}, {0x03u, 0x19u, 1024, 30720},
    {0xabu, 0xcdu, 512, 10240}, {0x12u, 0x34u, 128, 12800},
    {0x35u, 0x97u, 128, 10240}, {0x99u, 0x37u, 1024, 8192}};

// transform_resource in its decrypting direction. `length` is the whole
// entry and decides the tail that stays untouched; only the first
// min(length, limit) bytes change, so `data` need hold just `held` bytes.
bool ThaDecrypt(unsigned char *data, std::uint32_t held, std::uint32_t length,
                const ThaCipher &cipher)
{
    std::int32_t block = cipher.block;
    std::int32_t limit = cipher.limit;
    if (block <= 0 || limit < 0 || length > 0x7fffffffu) return false;
    const std::uint32_t copied =
        length < static_cast<std::uint32_t>(limit)
            ? length
            : static_cast<std::uint32_t>(limit);
    if (copied > held) return false;
    if (!copied) return true;
    auto *temporary = static_cast<unsigned char *>(std::malloc(copied));
    if (!temporary) return false;
    std::memcpy(temporary, data, copied);
    const std::int32_t total = static_cast<std::int32_t>(length);
    const std::int32_t remainder = total % block;
    std::int32_t remaining =
        total - (total & 1) - (remainder < block / 4 ? remainder : 0);
    std::int32_t offset = 0;
    unsigned char key = cipher.key;
    bool ok = true;
    while (remaining > 0 && limit > 0)
    {
        if (block > remaining) block = remaining;
        if (static_cast<std::uint32_t>(offset + block) > copied)
        {
            ok = false;
            break;
        }
        std::int32_t sequential = offset;
        for (std::int32_t lane = 0; lane < 2; ++lane)
        {
            for (std::int32_t index = block - 1 - lane; index >= 0; index -= 2)
            {
                data[offset + index] =
                    static_cast<unsigned char>(temporary[sequential] ^ key);
                key = static_cast<unsigned char>(key + cipher.step);
                ++sequential;
            }
        }
        offset += block;
        remaining -= block;
        limit -= block;
    }
    std::free(temporary);
    return ok;
}

// The 16-byte archive header and the index geometry it encodes.
bool ReadThaHeader(SceUID fd, std::uint64_t file_size,
                   std::uint32_t *index_capacity, std::uint32_t *index_length,
                   std::uint32_t *count)
{
    unsigned char header[16];
    if (!ReadExactAt(fd, 0u, header, sizeof(header)) ||
        !ThaDecrypt(header, sizeof(header), sizeof(header), kThaHeaderCipher) ||
        ReadLe32(header) != kThaMagic)
    {
        return false;
    }
    *index_capacity = ReadLe32(header + 4u) - 0x075bcd15u;
    *index_length = ReadLe32(header + 8u) - 0x3ade68b1u;
    *count = ReadLe32(header + 12u) + 0xf7e7f8acu;
    return *index_capacity && *index_capacity <= kMaxThaIndexBytes &&
           *index_length && *index_length <= file_size - sizeof(header) &&
           *count && *count <= kMaxThaEntries;
}

bool HasThaHeaderAndSize(const char *path, std::uint64_t expected_size)
{
    SceIoStat stat{};
    if (!path || sceIoGetstat(path, &stat) < 0 ||
        static_cast<std::uint64_t>(stat.st_size) != expected_size)
    {
        return false;
    }
    const SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return false;
    std::uint32_t capacity = 0u;
    std::uint32_t length = 0u;
    std::uint32_t count = 0u;
    bool ok = ReadThaHeader(fd, expected_size, &capacity, &length, &count);
    if (sceIoClose(fd) < 0) ok = false;
    return ok;
}

bool IsCompleteDataRoot(const char *root)
{
    char archive_path[kMaxPath];
    char bgm_path[kMaxPath];
    return JoinPath(archive_path, sizeof(archive_path), root, "th10.dat") &&
           JoinPath(bgm_path, sizeof(bgm_path), root, "thbgm.dat") &&
           HasThaHeaderAndSize(archive_path, kTh10DatBytes) &&
           HasHeaderAndSize(bgm_path, "ZWAV", kThBgmDatBytes);
}

bool IsRunSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
           c == '\r';
}

bool RunWordIs(const char *word, std::size_t length, const char *wanted)
{
    return std::strlen(wanted) == length && std::memcmp(word, wanted, length) == 0;
}

// The runtime (native/main.cpp) reads th10run.txt beside the EBOOT as words
// of at most 255 bytes ("%255s"), pairs them key/value (--dump-entry takes
// one more word) and takes the game files from the last `--data <folder>`,
// otherwise from the EBOOT's own folder. Returns 1 with that folder in out,
// 0 when there is no such option, negative when it cannot be held.
int ReadRunDataOption(const char *appdir, char *out, std::size_t out_size)
{
    char path[kMaxPath];
    if (!JoinPath(path, sizeof(path), appdir, "th10run.txt"))
        return kSelfwrapBadArgument;
    const SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return 0;
    auto *text = static_cast<char *>(std::malloc(kMaxRunFileBytes));
    if (!text)
    {
        sceIoClose(fd);
        return kSelfwrapSourceFailure;
    }
    std::size_t size = 0u;
    while (size < kMaxRunFileBytes)
    {
        const int got = sceIoRead(fd, text + size,
                                  static_cast<unsigned int>(kMaxRunFileBytes - size));
        if (got <= 0) break;
        size += static_cast<std::size_t>(got);
    }
    sceIoClose(fd);

    enum { kKey, kValue, kSkip } state = kKey;
    const char *key = nullptr;
    std::size_t key_length = 0u;
    int found = 0;
    std::size_t cursor = 0u;
    while (found >= 0)
    {
        while (cursor < size && IsRunSpace(text[cursor])) ++cursor;
        if (cursor == size) break;
        const char *word = text + cursor;
        std::size_t length = 0u;
        while (cursor < size && !IsRunSpace(text[cursor]) && length < 255u)
        {
            ++cursor;
            ++length;
        }
        if (state == kKey)
        {
            key = word;
            key_length = length;
            state = kValue;
        }
        else if (state == kValue)
        {
            state = kKey;
            if (RunWordIs(key, key_length, "--dump-entry"))
            {
                state = kSkip;
            }
            else if (RunWordIs(key, key_length, "--data"))
            {
                if (length >= out_size)
                {
                    found = kSelfwrapBadArgument;
                }
                else
                {
                    std::memcpy(out, word, length);
                    out[length] = '\0';
                    found = 1;
                }
            }
        }
        else
        {
            state = kKey;
        }
    }
    std::free(text);
    return found;
}

// The archive's LZSS (decode_lzss, 0x435dc0): a set bit is an 8-bit literal,
// a clear one a 13-bit offset into the 8 KiB dictionary (0 ends the stream)
// and a 4-bit length + 3. The input is an entry's decrypted head, then the
// rest of it from the file; past its end the bits are zero, as for the
// runtime's ResourceBits.
struct LzssInput
{
    const unsigned char *head;
    std::uint32_t head_size;
    std::uint32_t head_cursor;
    SceUID fd;
    std::uint32_t remaining;
    unsigned char io[4096];
    std::size_t cursor;
    std::size_t available;
    unsigned char current;
    unsigned char mask;
    bool failed;
};

unsigned char LzssReadByte(LzssInput *input)
{
    if (input->head_cursor < input->head_size)
        return input->head[input->head_cursor++];
    if (input->cursor == input->available)
    {
        if (!input->remaining) return 0u;
        const unsigned int request = input->remaining > sizeof(input->io)
                                         ? sizeof(input->io)
                                         : input->remaining;
        const int got = sceIoRead(input->fd, input->io, request);
        if (got <= 0)
        {
            input->failed = true;
            input->remaining = 0u;
            return 0u;
        }
        input->cursor = 0u;
        input->available = static_cast<std::size_t>(got);
        input->remaining -= static_cast<std::uint32_t>(got);
    }
    return input->io[input->cursor++];
}

std::uint32_t LzssGetBits(LzssInput *input, unsigned int count)
{
    std::uint32_t result = 0u;
    for (unsigned int i = 0u; i < count; ++i)
    {
        if (!input->mask)
        {
            input->current = LzssReadByte(input);
            input->mask = 0x80u;
        }
        result <<= 1u;
        if (input->current & input->mask) result |= 1u;
        input->mask >>= 1u;
    }
    return result;
}

// Exactly destination_size bytes, the size the archive records.
bool DecompressLzss(LzssInput *input, unsigned char *destination,
                    std::uint32_t destination_size)
{
    if (!destination || !destination_size) return false;
    unsigned char dictionary[8192]{};
    std::uint32_t dictionary_head = 1u;
    std::uint32_t produced = 0u;
    while (produced < destination_size && !input->failed)
    {
        if (LzssGetBits(input, 1u))
        {
            const unsigned char byte =
                static_cast<unsigned char>(LzssGetBits(input, 8u));
            destination[produced++] = byte;
            dictionary[dictionary_head] = byte;
            dictionary_head = (dictionary_head + 1u) & 0x1fffu;
        }
        else
        {
            const std::uint32_t offset = LzssGetBits(input, 13u);
            if (!offset) return false;
            const std::uint32_t length = LzssGetBits(input, 4u) + 3u;
            if (length > destination_size - produced) return false;
            for (std::uint32_t i = 0u; i < length; ++i)
            {
                const unsigned char byte = dictionary[(offset + i) & 0x1fffu];
                destination[produced++] = byte;
                dictionary[dictionary_head] = byte;
                dictionary_head = (dictionary_head + 1u) & 0x1fffu;
            }
        }
    }
    return produced == destination_size && !input->failed;
}

bool AsciiNameEquals(const unsigned char *name, std::size_t length,
                     const char *wanted)
{
    const std::size_t wanted_length = std::strlen(wanted);
    if (length != wanted_length) return false;
    for (std::size_t i = 0u; i < length; ++i)
    {
        unsigned char a = name[i];
        unsigned char b = static_cast<unsigned char>(wanted[i]);
        if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// The archive index (load_index, parse_index): LZSS-packed and encrypted at
// the end of the file. An entry is its NUL-terminated name padded to 4
// bytes, then offset, size and a reserved word; its packed bytes run to the
// next entry's offset, the last entry's to the index.
bool LoadArchiveIndex(SceUID fd, ThaEntry *title, ThaEntry *bgm_format)
{
    std::uint32_t capacity = 0u;
    std::uint32_t length = 0u;
    std::uint32_t count = 0u;
    if (!ReadThaHeader(fd, kTh10DatBytes, &capacity, &length, &count))
        return false;
    const std::uint32_t index_offset =
        static_cast<std::uint32_t>(kTh10DatBytes - length);
    const ThaCipher index_cipher = {0x3eu, 0x9bu, 128,
                                    static_cast<std::int32_t>(length)};
    auto *packed = static_cast<unsigned char *>(std::malloc(length));
    auto *table = static_cast<unsigned char *>(std::malloc(capacity));
    bool valid = packed && table &&
                 ReadExactAt(fd, index_offset, packed, length) &&
                 ThaDecrypt(packed, length, length, index_cipher);
    if (valid)
    {
        LzssInput input{};
        input.head = packed;
        input.head_size = length;
        input.fd = -1;
        valid = DecompressLzss(&input, table, capacity);
    }
    std::free(packed);

    // A found entry waits here for the next entry's offset, its packed end.
    ThaEntry *pending = nullptr;
    std::size_t cursor = 0u;
    for (std::uint32_t i = 0u; i < count && valid; ++i)
    {
        const std::size_t name_start = cursor;
        while (cursor < capacity && table[cursor]) ++cursor;
        if (cursor >= capacity)
        {
            valid = false;
            break;
        }
        const std::size_t name_length = cursor - name_start;
        cursor = name_start + ((name_length + 4u) & ~static_cast<std::size_t>(3u));
        if (cursor > capacity || capacity - cursor < 12u)
        {
            valid = false;
            break;
        }
        const std::uint32_t data_offset = ReadLe32(table + cursor);
        const std::uint32_t size = ReadLe32(table + cursor + 4u);
        cursor += 12u;
        if (data_offset < 16u || data_offset >= index_offset || !size)
        {
            valid = false;
            break;
        }
        if (pending)
        {
            if (data_offset <= pending->offset)
            {
                valid = false;
                break;
            }
            pending->packed_size = data_offset - pending->offset;
            pending = nullptr;
        }
        ThaEntry *selected = nullptr;
        if (AsciiNameEquals(table + name_start, name_length, "title.anm"))
            selected = title;
        else if (AsciiNameEquals(table + name_start, name_length, "thbgm.fmt"))
            selected = bgm_format;
        if (selected)
        {
            if (selected->found)
            {
                valid = false;
                break;
            }
            unsigned char key = 0u;
            for (std::size_t k = 0u; k < name_length; ++k)
                key = static_cast<unsigned char>(key + table[name_start + k]);
            selected->offset = data_offset;
            selected->size = size;
            selected->cipher = key & 7u;
            selected->found = true;
            pending = selected;
        }
    }
    if (valid && pending)
        pending->packed_size = index_offset - pending->offset;
    std::free(table);
    return valid && title->found && title->packed_size &&
           title->size <= kMaxTitleBytes && bgm_format->found &&
           bgm_format->packed_size && bgm_format->size <= kMaxBgmFormatBytes;
}

// ResourceArchive::read: decrypt with the cipher picked by the name's byte
// sum, then LZSS unless the entry is stored (packed size == size).
bool DecompressEntry(SceUID fd, const ThaEntry &entry, ByteBuffer *output)
{
    const std::uint32_t head_size =
        entry.packed_size < kThaHeadBytes ? entry.packed_size : kThaHeadBytes;
    if (entry.cipher >= 8u || !entry.size || !head_size) return false;
    auto *head = static_cast<unsigned char *>(std::malloc(head_size));
    output->data = static_cast<unsigned char *>(std::malloc(entry.size));
    bool ok = head && output->data &&
              ReadExactAt(fd, entry.offset, head, head_size) &&
              ThaDecrypt(head, head_size, entry.packed_size,
                         kThaEntryCiphers[entry.cipher]);
    if (ok && entry.packed_size == entry.size)
    {
        std::memcpy(output->data, head, head_size);
        ok = ReadExact(fd, output->data + head_size, entry.size - head_size);
    }
    else if (ok)
    {
        // fd stands just past the head.
        LzssInput input{};
        input.head = head;
        input.head_size = head_size;
        input.fd = fd;
        input.remaining = entry.packed_size - head_size;
        ok = DecompressLzss(&input, output->data, entry.size);
    }
    std::free(head);
    if (!ok)
    {
        FreeBuffer(output);
        return false;
    }
    output->capacity = entry.size;
    output->size = entry.size;
    return true;
}

bool CropNonzero(const Image &source, Image *cropped)
{
    std::uint32_t left = source.width;
    std::uint32_t top = source.height;
    std::uint32_t right = 0u;
    std::uint32_t bottom = 0u;
    bool found = false;
    for (std::uint32_t y = 0u; y < source.height; ++y)
    {
        for (std::uint32_t x = 0u; x < source.width; ++x)
        {
            const unsigned char *pixel =
                source.rgba + (static_cast<std::size_t>(y) * source.width + x) * 4u;
            if ((pixel[0] | pixel[1] | pixel[2] | pixel[3]) == 0u) continue;
            if (!found || x < left) left = x;
            if (!found || x > right) right = x;
            if (!found || y < top) top = y;
            if (!found || y > bottom) bottom = y;
            found = true;
        }
    }
    if (!found) return false;
    cropped->width = right - left + 1u;
    cropped->height = bottom - top + 1u;
    std::size_t bytes = 0u;
    if (!CheckedImageBytes(cropped->width, cropped->height, 4u, &bytes)) return false;
    cropped->rgba = static_cast<unsigned char *>(std::malloc(bytes));
    if (!cropped->rgba) return false;
    const std::size_t row_bytes = static_cast<std::size_t>(cropped->width) * 4u;
    for (std::uint32_t y = 0u; y < cropped->height; ++y)
    {
        const unsigned char *source_row = source.rgba +
            (static_cast<std::size_t>(top + y) * source.width + left) * 4u;
        std::memcpy(cropped->rgba + static_cast<std::size_t>(y) * row_bytes,
                    source_row, row_bytes);
    }
    return true;
}

// title.anm is a chain of AnmChunk headers (game/AnmResources.hpp): 0x40
// bytes, then the sprite offsets; the texture name at +0x1c, the THTX image
// at +0x30 when the byte at +0x34 is set, the next chunk at +0x38 (0: last).
// A chunk's sprites and THTX lie between it and the next chunk.
constexpr std::size_t kAnmChunkBytes = 0x40u;

bool FindAnmChunk(const ByteBuffer &anm, const char *name,
                  std::size_t *chunk_at, std::size_t *chunk_end)
{
    std::size_t at = 0u;
    for (unsigned int guard = 0u; guard < 4096u; ++guard)
    {
        if (at > anm.size || anm.size - at < kAnmChunkBytes) return false;
        const unsigned char *chunk = anm.data + at;
        const std::uint32_t next = ReadLe32(chunk + 0x38u);
        if (next && (next < kAnmChunkBytes || next > anm.size - at)) return false;
        const std::size_t end = next ? at + next : anm.size;
        const std::uint32_t name_offset = ReadLe32(chunk + 0x1cu);
        if (name_offset < kAnmChunkBytes || name_offset >= end - at) return false;
        const unsigned char *chunk_name = chunk + name_offset;
        const std::size_t name_room = end - at - name_offset;
        std::size_t name_length = 0u;
        while (name_length < name_room && chunk_name[name_length]) ++name_length;
        if (name_length == name_room) return false;
        if (AsciiNameEquals(chunk_name, name_length, name))
        {
            *chunk_at = at;
            *chunk_end = end;
            return true;
        }
        if (!next) return false;
        at += next;
    }
    return false;
}

// The chunk's THTX image as RGBA; format 1 is BGRA8888, 5 is ARGB4444.
bool DecodeChunkTexture(const ByteBuffer &anm, std::size_t chunk_at,
                        std::size_t chunk_end, Image *image)
{
    const unsigned char *chunk = anm.data + chunk_at;
    const std::size_t chunk_bytes = chunk_end - chunk_at;
    const std::uint32_t thtx = ReadLe32(chunk + 0x30u);
    if (!chunk[0x34u] || thtx < kAnmChunkBytes || thtx > chunk_bytes ||
        chunk_bytes - thtx < 16u)
    {
        return false;
    }
    const unsigned char *header = chunk + thtx;
    if (std::memcmp(header, "THTX", 4u) != 0) return false;
    const std::uint16_t format = ReadLe16(header + 6u);
    const std::uint32_t width = ReadLe16(header + 8u);
    const std::uint32_t height = ReadLe16(header + 10u);
    const std::uint32_t stored = ReadLe32(header + 12u);
    if (!width || !height || width > kMaxTextureDimension ||
        height > kMaxTextureDimension || (format != 1u && format != 5u))
    {
        return false;
    }
    const std::size_t source_bpp = format == 1u ? 4u : 2u;
    std::size_t source_bytes = 0u;
    std::size_t rgba_bytes = 0u;
    if (!CheckedImageBytes(width, height, source_bpp, &source_bytes) ||
        !CheckedImageBytes(width, height, 4u, &rgba_bytes) ||
        source_bytes > stored || stored > chunk_bytes - thtx - 16u)
    {
        return false;
    }
    image->rgba = static_cast<unsigned char *>(std::malloc(rgba_bytes));
    if (!image->rgba) return false;
    image->width = width;
    image->height = height;
    const unsigned char *pixels = header + 16u;
    const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
    for (std::size_t i = 0u; i < pixel_count; ++i)
    {
        unsigned char *out = image->rgba + i * 4u;
        if (format == 1u)
        {
            out[0] = pixels[i * 4u + 2u];
            out[1] = pixels[i * 4u + 1u];
            out[2] = pixels[i * 4u + 0u];
            out[3] = pixels[i * 4u + 3u];
        }
        else
        {
            const std::uint16_t value = ReadLe16(pixels + i * 2u);
            out[0] = static_cast<unsigned char>(((value >> 8u) & 0x0fu) * 17u);
            out[1] = static_cast<unsigned char>(((value >> 4u) & 0x0fu) * 17u);
            out[2] = static_cast<unsigned char>((value & 0x0fu) * 17u);
            out[3] = static_cast<unsigned char>(((value >> 12u) & 0x0fu) * 17u);
        }
    }
    return true;
}

// The chunk's first sprite (u32 id, then x, y, width, height as floats) in
// texture pixels: {x, y, width, height}.
bool FirstSpriteRect(const ByteBuffer &anm, std::size_t chunk_at,
                     std::size_t chunk_end, const Image &texture,
                     std::uint32_t rect[4])
{
    const unsigned char *chunk = anm.data + chunk_at;
    const std::size_t chunk_bytes = chunk_end - chunk_at;
    if (static_cast<std::int32_t>(ReadLe32(chunk)) < 1 ||
        chunk_bytes < kAnmChunkBytes + 4u)
    {
        return false;
    }
    const std::uint32_t sprite = ReadLe32(chunk + kAnmChunkBytes);
    if (sprite < kAnmChunkBytes || sprite > chunk_bytes ||
        chunk_bytes - sprite < 20u)
    {
        return false;
    }
    float values[4];
    std::memcpy(values, chunk + sprite + 4u, sizeof(values));
    const std::uint32_t limits[4] = {texture.width, texture.height,
                                     texture.width, texture.height};
    for (unsigned int i = 0u; i < 4u; ++i)
    {
        if (!(values[i] >= 0.0f) || values[i] > static_cast<float>(limits[i]))
            return false;
        rect[i] = static_cast<std::uint32_t>(values[i]);
    }
    return rect[2] && rect[3] && rect[2] <= texture.width - rect[0] &&
           rect[3] <= texture.height - rect[1];
}

bool CopyRegion(const Image &source, const std::uint32_t rect[4], Image *region)
{
    std::size_t bytes = 0u;
    if (!CheckedImageBytes(rect[2], rect[3], 4u, &bytes)) return false;
    region->rgba = static_cast<unsigned char *>(std::malloc(bytes));
    if (!region->rgba) return false;
    region->width = rect[2];
    region->height = rect[3];
    const std::size_t row_bytes = static_cast<std::size_t>(rect[2]) * 4u;
    for (std::uint32_t y = 0u; y < rect[3]; ++y)
    {
        std::memcpy(region->rgba + y * row_bytes,
                    source.rgba + (static_cast<std::size_t>(rect[1] + y) *
                                       source.width + rect[0]) * 4u,
                    row_bytes);
    }
    return true;
}

// The title logo: the first sprite of title/title_logo.png (the texture also
// holds a smaller copy below it), cropped to its visible pixels.
bool DecodeTitleLogo(const ByteBuffer &anm, Image *logo)
{
    std::size_t at = 0u;
    std::size_t end = 0u;
    Image texture{};
    if (!FindAnmChunk(anm, "title/title_logo.png", &at, &end) ||
        !DecodeChunkTexture(anm, at, end, &texture))
    {
        return false;
    }
    std::uint32_t rect[4];
    Image region{};
    bool ok = FirstSpriteRect(anm, at, end, texture, rect) &&
              CopyRegion(texture, rect, &region);
    FreeImage(&texture);
    ok = ok && CropNonzero(region, logo);
    FreeImage(&region);
    return ok;
}

// The 640x480 title background: title/title00a.png holds its left 512
// columns, title/title00b.png the right 128.
bool DecodeTitleBackground(const ByteBuffer &anm, Image *background)
{
    std::size_t at = 0u;
    std::size_t end = 0u;
    Image left{};
    Image right{};
    bool ok = FindAnmChunk(anm, "title/title00a.png", &at, &end) &&
              DecodeChunkTexture(anm, at, end, &left) &&
              FindAnmChunk(anm, "title/title00b.png", &at, &end) &&
              DecodeChunkTexture(anm, at, end, &right) &&
              left.height == right.height;
    std::size_t bytes = 0u;
    ok = ok && CheckedImageBytes(left.width + right.width, left.height, 4u, &bytes);
    if (ok)
    {
        background->rgba = static_cast<unsigned char *>(std::malloc(bytes));
        ok = background->rgba != nullptr;
    }
    if (ok)
    {
        background->width = left.width + right.width;
        background->height = left.height;
        const std::size_t left_bytes = static_cast<std::size_t>(left.width) * 4u;
        const std::size_t right_bytes = static_cast<std::size_t>(right.width) * 4u;
        for (std::uint32_t y = 0u; y < background->height; ++y)
        {
            unsigned char *row = background->rgba + y * (left_bytes + right_bytes);
            std::memcpy(row, left.rgba + y * left_bytes, left_bytes);
            std::memcpy(row + left_bytes, right.rgba + y * right_bytes, right_bytes);
        }
    }
    FreeImage(&left);
    FreeImage(&right);
    return ok;
}

bool ResizeRegion(const Image &source, std::uint32_t source_x,
                  std::uint32_t source_y, std::uint32_t source_width,
                  std::uint32_t source_height, std::uint32_t destination_width,
                  std::uint32_t destination_height, Image *destination)
{
    if (!source.rgba || !source_width || !source_height ||
        source_x > source.width || source_y > source.height ||
        source_width > source.width - source_x ||
        source_height > source.height - source_y)
    {
        return false;
    }
    std::size_t bytes = 0u;
    if (!CheckedImageBytes(destination_width, destination_height, 4u, &bytes))
        return false;
    destination->rgba = static_cast<unsigned char *>(std::malloc(bytes));
    if (!destination->rgba) return false;
    destination->width = destination_width;
    destination->height = destination_height;

    for (std::uint32_t y = 0u; y < destination_height; ++y)
    {
        const std::uint64_t y_fixed = destination_height > 1u
            ? static_cast<std::uint64_t>(y) * (source_height - 1u) * 65536u /
                  (destination_height - 1u)
            : 0u;
        const std::uint32_t y0 = static_cast<std::uint32_t>(y_fixed >> 16u);
        const std::uint32_t y1 = y0 + 1u < source_height ? y0 + 1u : y0;
        const std::uint32_t fy = static_cast<std::uint32_t>(y_fixed & 0xffffu);
        for (std::uint32_t x = 0u; x < destination_width; ++x)
        {
            const std::uint64_t x_fixed = destination_width > 1u
                ? static_cast<std::uint64_t>(x) * (source_width - 1u) * 65536u /
                      (destination_width - 1u)
                : 0u;
            const std::uint32_t x0 = static_cast<std::uint32_t>(x_fixed >> 16u);
            const std::uint32_t x1 = x0 + 1u < source_width ? x0 + 1u : x0;
            const std::uint32_t fx = static_cast<std::uint32_t>(x_fixed & 0xffffu);
            const unsigned char *p00 = source.rgba +
                (static_cast<std::size_t>(source_y + y0) * source.width + source_x + x0) * 4u;
            const unsigned char *p10 = source.rgba +
                (static_cast<std::size_t>(source_y + y0) * source.width + source_x + x1) * 4u;
            const unsigned char *p01 = source.rgba +
                (static_cast<std::size_t>(source_y + y1) * source.width + source_x + x0) * 4u;
            const unsigned char *p11 = source.rgba +
                (static_cast<std::size_t>(source_y + y1) * source.width + source_x + x1) * 4u;
            unsigned char *out = destination->rgba +
                (static_cast<std::size_t>(y) * destination_width + x) * 4u;
            for (unsigned int channel = 0u; channel < 4u; ++channel)
            {
                const std::uint64_t top =
                    static_cast<std::uint64_t>(p00[channel]) * (65536u - fx) +
                    static_cast<std::uint64_t>(p10[channel]) * fx;
                const std::uint64_t bottom =
                    static_cast<std::uint64_t>(p01[channel]) * (65536u - fx) +
                    static_cast<std::uint64_t>(p11[channel]) * fx;
                const std::uint64_t value =
                    top * (65536u - fy) + bottom * fy + 0x80000000ull;
                out[channel] = static_cast<unsigned char>(value >> 32u);
            }
        }
    }
    return true;
}

bool MakeThumbnail(const Image &source, std::uint32_t max_width,
                   std::uint32_t max_height, Image *thumbnail)
{
    std::uint32_t width = source.width;
    std::uint32_t height = source.height;
    if (width > max_width || height > max_height)
    {
        if (static_cast<std::uint64_t>(width) * max_height >
            static_cast<std::uint64_t>(height) * max_width)
        {
            width = max_width;
            height = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(source.height) * max_width +
                 source.width / 2u) /
                source.width);
        }
        else
        {
            height = max_height;
            width = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(source.width) * max_height +
                 source.height / 2u) /
                source.height);
        }
    }
    if (!width) width = 1u;
    if (!height) height = 1u;
    return ResizeRegion(source, 0u, 0u, source.width, source.height,
                        width, height, thumbnail);
}

void CompositeOpaque(Image *destination, const Image &source, int left, int top)
{
    for (std::uint32_t y = 0u; y < source.height; ++y)
    {
        const int destination_y = top + static_cast<int>(y);
        if (destination_y < 0 || destination_y >= static_cast<int>(destination->height))
            continue;
        for (std::uint32_t x = 0u; x < source.width; ++x)
        {
            const int destination_x = left + static_cast<int>(x);
            if (destination_x < 0 || destination_x >= static_cast<int>(destination->width))
                continue;
            const unsigned char *in = source.rgba +
                (static_cast<std::size_t>(y) * source.width + x) * 4u;
            unsigned char *out = destination->rgba +
                (static_cast<std::size_t>(destination_y) * destination->width +
                 static_cast<std::uint32_t>(destination_x)) * 4u;
            const unsigned int alpha = in[3];
            for (unsigned int channel = 0u; channel < 3u; ++channel)
            {
                out[channel] = static_cast<unsigned char>(
                    (static_cast<unsigned int>(in[channel]) * alpha +
                     static_cast<unsigned int>(out[channel]) * (255u - alpha) + 127u) /
                    255u);
            }
            out[3] = 255u;
        }
    }
}

bool BuildIcon(const Image &logo, Image *icon)
{
    icon->width = 144u;
    icon->height = 80u;
    std::size_t bytes = 0u;
    if (!CheckedImageBytes(icon->width, icon->height, 4u, &bytes)) return false;
    icon->rgba = static_cast<unsigned char *>(std::malloc(bytes));
    if (!icon->rgba) return false;
    for (std::uint32_t y = 0u; y < icon->height; ++y)
    {
        const unsigned int red = 4u + (8u * y) / 79u;
        const unsigned int green = 2u + (6u * y) / 79u;
        const unsigned int blue = 8u + (16u * y) / 79u;
        for (std::uint32_t x = 0u; x < icon->width; ++x)
        {
            unsigned char *pixel = icon->rgba +
                (static_cast<std::size_t>(y) * icon->width + x) * 4u;
            pixel[0] = static_cast<unsigned char>(red);
            pixel[1] = static_cast<unsigned char>(green);
            pixel[2] = static_cast<unsigned char>(blue);
            pixel[3] = 255u;
        }
    }
    Image scaled{};
    if (!MakeThumbnail(logo, 136u, 72u, &scaled))
    {
        FreeImage(icon);
        return false;
    }
    CompositeOpaque(icon, scaled,
                    (static_cast<int>(icon->width) - static_cast<int>(scaled.width)) / 2,
                    (static_cast<int>(icon->height) - static_cast<int>(scaled.height)) / 2);
    FreeImage(&scaled);
    for (std::uint32_t x = 0u; x < icon->width; ++x)
    {
        unsigned char *top = icon->rgba + static_cast<std::size_t>(x) * 4u;
        unsigned char *bottom = icon->rgba +
            (static_cast<std::size_t>(icon->height - 1u) * icon->width + x) * 4u;
        top[0] = bottom[0] = 150u;
        top[1] = bottom[1] = 145u;
        top[2] = bottom[2] = 175u;
    }
    for (std::uint32_t y = 0u; y < icon->height; ++y)
    {
        unsigned char *left = icon->rgba +
            static_cast<std::size_t>(y) * icon->width * 4u;
        unsigned char *right = left + static_cast<std::size_t>(icon->width - 1u) * 4u;
        left[0] = right[0] = 150u;
        left[1] = right[1] = 145u;
        left[2] = right[2] = 175u;
    }
    return true;
}

bool AddLogoShadow(Image *picture, const Image &logo)
{
    const std::size_t pixels = static_cast<std::size_t>(logo.width) * logo.height;
    auto *horizontal = static_cast<unsigned char *>(std::malloc(pixels));
    auto *blurred = static_cast<unsigned char *>(std::malloc(pixels));
    if (!horizontal || !blurred)
    {
        std::free(horizontal);
        std::free(blurred);
        return false;
    }
    constexpr unsigned int kernel[5] = {1u, 4u, 6u, 4u, 1u};
    for (std::uint32_t y = 0u; y < logo.height; ++y)
    {
        for (std::uint32_t x = 0u; x < logo.width; ++x)
        {
            unsigned int sum = 0u;
            for (int tap = -2; tap <= 2; ++tap)
            {
                const int sample_x = static_cast<int>(x) + tap;
                if (sample_x < 0 || sample_x >= static_cast<int>(logo.width)) continue;
                sum += logo.rgba[(static_cast<std::size_t>(y) * logo.width +
                                  static_cast<std::uint32_t>(sample_x)) * 4u + 3u] *
                       kernel[tap + 2];
            }
            horizontal[static_cast<std::size_t>(y) * logo.width + x] =
                static_cast<unsigned char>((sum + 8u) / 16u);
        }
    }
    for (std::uint32_t y = 0u; y < logo.height; ++y)
    {
        for (std::uint32_t x = 0u; x < logo.width; ++x)
        {
            unsigned int sum = 0u;
            for (int tap = -2; tap <= 2; ++tap)
            {
                const int sample_y = static_cast<int>(y) + tap;
                if (sample_y < 0 || sample_y >= static_cast<int>(logo.height)) continue;
                sum += horizontal[static_cast<std::size_t>(sample_y) * logo.width + x] *
                       kernel[tap + 2];
            }
            blurred[static_cast<std::size_t>(y) * logo.width + x] =
                static_cast<unsigned char>((sum + 8u) / 16u);
        }
    }
    std::free(horizontal);

    for (std::uint32_t y = 0u; y < logo.height; ++y)
    {
        const int destination_y = 22 + static_cast<int>(y);
        if (destination_y < 0 || destination_y >= static_cast<int>(picture->height)) continue;
        for (std::uint32_t x = 0u; x < logo.width; ++x)
        {
            const int destination_x = 26 + static_cast<int>(x);
            if (destination_x < 0 || destination_x >= static_cast<int>(picture->width)) continue;
            const unsigned int alpha =
                (static_cast<unsigned int>(blurred[static_cast<std::size_t>(y) * logo.width + x]) *
                 140u + 127u) /
                255u;
            unsigned char *out = picture->rgba +
                (static_cast<std::size_t>(destination_y) * picture->width +
                 static_cast<std::uint32_t>(destination_x)) * 4u;
            constexpr unsigned int shadow[3] = {20u, 0u, 40u};
            for (unsigned int channel = 0u; channel < 3u; ++channel)
            {
                out[channel] = static_cast<unsigned char>(
                    (shadow[channel] * alpha +
                     static_cast<unsigned int>(out[channel]) * (255u - alpha) + 127u) /
                    255u);
            }
        }
    }
    std::free(blurred);
    return true;
}

bool BuildPicture(const Image &logo, const Image &background, Image *picture)
{
    std::uint32_t crop_height = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(background.width) * 272u / 480u);
    if (!crop_height) return false;
    if (crop_height > background.height) crop_height = background.height;
    const std::uint32_t crop_top =
        static_cast<std::uint32_t>((background.height - crop_height) * 45u / 100u);
    if (!ResizeRegion(background, 0u, crop_top, background.width, crop_height,
                      480u, 272u, picture))
    {
        return false;
    }
    Image scaled_logo{};
    if (!MakeThumbnail(logo, 300u, 120u, &scaled_logo) ||
        !AddLogoShadow(picture, scaled_logo))
    {
        FreeImage(&scaled_logo);
        FreeImage(picture);
        return false;
    }
    CompositeOpaque(picture, scaled_logo, 24, 20);
    FreeImage(&scaled_logo);
    return true;
}

struct PngSink
{
    unsigned char *data;
    std::size_t size;
    std::size_t capacity;
};

void PngWrite(png_structp png, png_bytep data, png_size_t length)
{
    auto *sink = static_cast<PngSink *>(png_get_io_ptr(png));
    if (!sink || sink->size > sink->capacity ||
        length > sink->capacity - sink->size)
    {
        png_error(png, "TH10 XMB PNG reserve exhausted");
        return;
    }
    std::memcpy(sink->data + sink->size, data, length);
    sink->size += length;
}

void PngFlush(png_structp)
{
}

bool EncodePng(const Image &image, bool with_alpha,
               std::size_t maximum_bytes, ByteBuffer *png_output)
{
    std::size_t row_bytes = 0u;
    const std::size_t channels = with_alpha ? 4u : 3u;
    if (!image.rgba || !CheckedImageBytes(image.width, 1u, channels,
                                          &row_bytes) ||
        row_bytes == SIZE_MAX ||
        image.height > SIZE_MAX / (row_bytes + 1u))
    {
        return false;
    }
    const std::size_t filtered_bytes = (row_bytes + 1u) * image.height;
    const uLong zlib_source = static_cast<uLong>(filtered_bytes);
    if (static_cast<std::size_t>(zlib_source) != filtered_bytes) return false;
    const std::size_t compressed_bound =
        static_cast<std::size_t>(compressBound(zlib_source));
    if (compressed_bound > SIZE_MAX - 1024u) return false;
    const std::size_t conservative_size = compressed_bound + 1024u;
    if (conservative_size > maximum_bytes) return false;

    auto *storage = static_cast<unsigned char *>(std::malloc(conservative_size));
    auto *row = static_cast<unsigned char *>(std::malloc(row_bytes));
    if (!storage || !row)
    {
        std::free(storage);
        std::free(row);
        return false;
    }
    PngSink sink{storage, 0u, conservative_size};
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png)
    {
        std::free(storage);
        std::free(row);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info)
    {
        png_destroy_write_struct(&png, nullptr);
        std::free(storage);
        std::free(row);
        return false;
    }
    if (setjmp(png_jmpbuf(png)))
    {
        png_destroy_write_struct(&png, &info);
        std::free(storage);
        std::free(row);
        return false;
    }
    png_set_write_fn(png, &sink, PngWrite, PngFlush);
    png_set_IHDR(png, info, image.width, image.height, 8,
                 with_alpha ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_compression_level(png, 6);
    png_write_info(png, info);
    for (std::uint32_t y = 0u; y < image.height; ++y)
    {
        const unsigned char *source = image.rgba +
            static_cast<std::size_t>(y) * image.width * 4u;
        for (std::uint32_t x = 0u; x < image.width; ++x)
        {
            row[x * channels + 0u] = source[x * 4u + 0u];
            row[x * channels + 1u] = source[x * 4u + 1u];
            row[x * channels + 2u] = source[x * 4u + 2u];
            if (with_alpha) row[x * channels + 3u] = source[x * 4u + 3u];
        }
        png_write_row(png, row);
    }
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    std::free(row);
    if (sink.size < sizeof(kPngMagic) ||
        std::memcmp(sink.data, kPngMagic, sizeof(kPngMagic)) != 0 ||
        sink.size > maximum_bytes)
    {
        std::free(storage);
        return false;
    }
    png_output->data = storage;
    png_output->size = sink.size;
    png_output->capacity = conservative_size;
    return true;
}

bool PadPngToSlot(ByteBuffer *png, std::size_t slot_bytes,
                  const unsigned char identity[8], unsigned char role)
{
    // libpng emits a 12-byte zero-length IEND as the final chunk. Insert one
    // private ancillary identity/padding chunk immediately before it, making
    // the complete PNG section exactly the immutable PBP slot length.
    constexpr std::size_t kIendBytes = 12u;
    constexpr std::size_t kChunkOverhead = 12u;
    constexpr std::size_t kIdentityBytes = 9u;
    if (!png || !png->data || png->size < sizeof(kPngMagic) + kIendBytes ||
        slot_bytes > UINT32_MAX || png->size > slot_bytes ||
        slot_bytes - png->size < kChunkOverhead + kIdentityBytes)
    {
        return false;
    }
    const unsigned char *iend = png->data + png->size - kIendBytes;
    if (ReadBe32(iend) != 0u || std::memcmp(iend + 4u, "IEND", 4u) != 0)
        return false;

    const std::size_t payload_bytes =
        slot_bytes - png->size - kChunkOverhead;
    if (payload_bytes > UINT32_MAX) return false;
    auto *fixed = static_cast<unsigned char *>(std::malloc(slot_bytes));
    if (!fixed) return false;
    const std::size_t prefix_bytes = png->size - kIendBytes;
    std::memcpy(fixed, png->data, prefix_bytes);
    unsigned char *chunk = fixed + prefix_bytes;
    WriteBe32(chunk, static_cast<std::uint32_t>(payload_bytes));
    std::memcpy(chunk + 4u, kSelfwrapChunkType,
                sizeof(kSelfwrapChunkType));
    std::memcpy(chunk + 8u, identity, sizeof(kWrappedIdentity));
    chunk[8u + sizeof(kWrappedIdentity)] = role;
    std::memset(chunk + 8u + kIdentityBytes, 0,
                payload_bytes - kIdentityBytes);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, chunk + 4u,
                static_cast<uInt>(sizeof(kSelfwrapChunkType) + payload_bytes));
    WriteBe32(chunk + 8u + payload_bytes, static_cast<std::uint32_t>(crc));
    std::memcpy(chunk + kChunkOverhead + payload_bytes, iend, kIendBytes);

    std::free(png->data);
    png->data = fixed;
    png->size = slot_bytes;
    png->capacity = slot_bytes;
    return true;
}

// thbgm.fmt: 52-byte records (name[16], the track's offset in thbgm.dat, a
// reserved word, intro bytes, track bytes, then its WAVEFORMATEX), ending
// with an empty name.
bool FindTitleThemeClip(const ByteBuffer &format, Th10XmbClip *clip)
{
    for (std::size_t at = 0u; format.size - at >= kBgmFormatRecordBytes;
         at += kBgmFormatRecordBytes)
    {
        const unsigned char *record = format.data + at;
        if (!record[0]) break;
        std::size_t name_length = 0u;
        while (name_length < 16u && record[name_length]) ++name_length;
        if (name_length == 16u ||
            !AsciiNameEquals(record, name_length, kTitleThemeName))
        {
            continue;
        }
        const std::uint32_t offset = ReadLe32(record + 16u);
        const std::uint32_t bytes = ReadLe32(record + 28u);
        // PCM, 2 channels, 44100 Hz, 4-byte blocks, 16 bits.
        if (ReadLe16(record + 32u) != 1u || ReadLe16(record + 34u) != 2u ||
            ReadLe32(record + 36u) != 44100u || ReadLe16(record + 44u) != 4u ||
            ReadLe16(record + 46u) != 16u || offset < 16u || (offset & 3u) ||
            offset > kThBgmDatBytes || bytes > kThBgmDatBytes - offset ||
            bytes <= kClipStartBytes)
        {
            return false;
        }
        const std::uint32_t available =
            (bytes - kClipStartBytes) / 4u / kTh10At3FrameSamples;
        const std::uint32_t wanted = kClipSeconds * 44100u / kTh10At3FrameSamples;
        clip->pcm_offset = offset + kClipStartBytes;
        clip->frames = wanted < available ? wanted : available;
        clip->fade_in = kClipFadeInSamples;
        clip->fade_out = kClipFadeOutSamples;
        return clip->frames > 0u;
    }
    return false;
}

void WriteLe32(unsigned char *data, std::uint32_t value)
{
    data[0] = static_cast<unsigned char>(value);
    data[1] = static_cast<unsigned char>(value >> 8u);
    data[2] = static_cast<unsigned char>(value >> 16u);
    data[3] = static_cast<unsigned char>(value >> 24u);
}

// The AT3 counterpart of PadPngToSlot: one private chunk behind "data"
// (identity, role, zeros) ends the RIFF exactly at the slot's end. Parsers
// stop at "data" and play the frames it and "fact" describe.
bool PadAt3ToSlot(ByteBuffer *at3, std::size_t slot_bytes,
                  const unsigned char identity[8], unsigned char role)
{
    if (!at3 || !at3->data || at3->size < kTh10At3HeaderBytes ||
        at3->capacity < slot_bytes || slot_bytes > UINT32_MAX ||
        slot_bytes - at3->size < kAt3IdentityChunkBytes ||
        (at3->size & 1u) || (slot_bytes & 1u))
    {
        return false;
    }
    unsigned char *chunk = at3->data + at3->size;
    const std::size_t payload = slot_bytes - at3->size - 8u;
    std::memcpy(chunk, kSelfwrapChunkType, sizeof(kSelfwrapChunkType));
    WriteLe32(chunk + 4u, static_cast<std::uint32_t>(payload));
    std::memcpy(chunk + 8u, identity, sizeof(kWrappedIdentity));
    chunk[8u + sizeof(kWrappedIdentity)] = role;
    std::memset(chunk + 9u + sizeof(kWrappedIdentity), 0, payload - 9u);
    WriteLe32(at3->data + 4u, static_cast<std::uint32_t>(slot_bytes - 8u));
    at3->size = slot_bytes;
    return true;
}

std::uint32_t g_last_sound_encode_us = 0u;

// The launcher's progress display (th10_unified_set_progress), in permille
// of the whole generation: the archive and the title images take the first
// 14 %, the SND0 encode (most of the wait on the PSP) runs to 98 %, the write
// and its check finish it.
void (*g_progress)(unsigned int permille) = nullptr;

void Report(unsigned int permille)
{
    if (g_progress) g_progress(permille);
}

void ReportSound(std::uint32_t done, std::uint32_t total)
{
    Report(140u + static_cast<unsigned int>(840ull * done / (total ? total : 1u)));
}

bool EncodeTitleTheme(const char *bgm_path, const Th10XmbClip &clip,
                      ByteBuffer *sound)
{
    sound->data = static_cast<unsigned char *>(std::malloc(kSoundSlotBytes));
    if (!sound->data) return false;
    sound->capacity = kSoundSlotBytes;
    sound->size = th10_xmb_encode_clip(bgm_path, clip, sound->data,
                                       kSoundSlotBytes - kAt3IdentityChunkBytes,
                                       &g_last_sound_encode_us, ReportSound);
    if (!sound->size ||
        !PadAt3ToSlot(sound, kSoundSlotBytes, kWrappedIdentity, 'S'))
    {
        FreeBuffer(sound);
        return false;
    }
    return true;
}

bool BuildPlaceholderSound(ByteBuffer *sound)
{
    sound->data = static_cast<unsigned char *>(std::malloc(kSoundSlotBytes));
    if (!sound->data) return false;
    sound->capacity = kSoundSlotBytes;
    th10_xmb_at3_header(sound->data, kPlaceholderSoundFrames);
    for (std::uint32_t i = 0u; i < kPlaceholderSoundFrames; ++i)
        th10_xmb_at3_silent_frame(sound->data + kTh10At3HeaderBytes +
                                  i * kTh10At3FrameBytes);
    sound->size = kTh10At3HeaderBytes +
                  static_cast<std::size_t>(kPlaceholderSoundFrames) * kTh10At3FrameBytes;
    if (!PadAt3ToSlot(sound, kSoundSlotBytes, kPlaceholderIdentity, 'S'))
    {
        FreeBuffer(sound);
        return false;
    }
    return true;
}

bool GenerateAssets(const char *data_root, ByteBuffer *icon_png,
                    ByteBuffer *picture_png, ByteBuffer *sound_at3)
{
    char archive_path[kMaxPath];
    char bgm_path[kMaxPath];
    if (!JoinPath(archive_path, sizeof(archive_path), data_root, "th10.dat") ||
        !JoinPath(bgm_path, sizeof(bgm_path), data_root, "thbgm.dat"))
    {
        return false;
    }
    const SceUID archive = sceIoOpen(archive_path, PSP_O_RDONLY, 0);
    if (archive < 0) return false;
    ThaEntry title{};
    ThaEntry bgm_format{};
    ByteBuffer format{};
    ByteBuffer title_anm{};
    bool ok = LoadArchiveIndex(archive, &title, &bgm_format) &&
              DecompressEntry(archive, bgm_format, &format) &&
              DecompressEntry(archive, title, &title_anm);
    if (sceIoClose(archive) < 0) ok = false;
    Th10XmbClip clip{};
    ok = ok && FindTitleThemeClip(format, &clip);
    FreeBuffer(&format);
    Report(80u);

    Image logo{};
    Image background{};
    ok = ok && DecodeTitleLogo(title_anm, &logo) &&
         DecodeTitleBackground(title_anm, &background);
    FreeBuffer(&title_anm);
    Report(100u);
    if (!ok)
    {
        FreeImage(&logo);
        FreeImage(&background);
        return false;
    }

    Image icon{};
    Image picture{};
    ok = BuildIcon(logo, &icon) && BuildPicture(logo, background, &picture);
    FreeImage(&logo);
    FreeImage(&background);
    if (!ok)
    {
        FreeImage(&icon);
        FreeImage(&picture);
        return false;
    }
    ok = EncodePng(icon, false, kIconSlotBytes, icon_png) &&
         PadPngToSlot(icon_png, kIconSlotBytes, kWrappedIdentity, 'I');
    if (ok)
    {
        ok = EncodePng(picture, false, kPictureSlotBytes, picture_png) &&
             PadPngToSlot(picture_png, kPictureSlotBytes,
                          kWrappedIdentity, 'P');
    }
    FreeImage(&icon);
    FreeImage(&picture);
    Report(140u);
    ok = ok && EncodeTitleTheme(bgm_path, clip, sound_at3);
    if (!ok)
    {
        FreeBuffer(icon_png);
        FreeBuffer(picture_png);
        FreeBuffer(sound_at3);
    }
    return ok;
}

bool GeneratePlaceholderAssets(ByteBuffer *icon_png,
                               ByteBuffer *picture_png,
                               ByteBuffer *sound_at3)
{
    Image icon{144u, 80u, nullptr};
    Image picture{480u, 272u, nullptr};
    std::size_t icon_bytes = 0u;
    std::size_t picture_bytes = 0u;
    if (!CheckedImageBytes(icon.width, icon.height, 4u, &icon_bytes) ||
        !CheckedImageBytes(picture.width, picture.height, 4u,
                           &picture_bytes))
    {
        return false;
    }
    icon.rgba = static_cast<unsigned char *>(std::calloc(1u, icon_bytes));
    picture.rgba =
        static_cast<unsigned char *>(std::calloc(1u, picture_bytes));
    bool ok = icon.rgba && picture.rgba &&
              EncodePng(icon, true, kIconSlotBytes, icon_png) &&
              PadPngToSlot(icon_png, kIconSlotBytes,
                           kPlaceholderIdentity, 'I') &&
              EncodePng(picture, true, kPictureSlotBytes, picture_png) &&
              PadPngToSlot(picture_png, kPictureSlotBytes,
                           kPlaceholderIdentity, 'P') &&
              BuildPlaceholderSound(sound_at3);
    FreeImage(&icon);
    FreeImage(&picture);
    if (!ok)
    {
        FreeBuffer(icon_png);
        FreeBuffer(picture_png);
        FreeBuffer(sound_at3);
    }
    return ok;
}

bool PbpOffsetsMonotonic(const std::uint32_t offsets[8], std::uint64_t file_size)
{
    if (offsets[0] != kPbpHeaderBytes) return false;
    for (unsigned int i = 0u; i + 1u < 8u; ++i)
    {
        if (offsets[i] > offsets[i + 1u]) return false;
    }
    return offsets[7] <= file_size;
}

bool FindSfoSlotContract(SceUID fd, const std::uint32_t offsets[8])
{
    if (offsets[1] <= offsets[0]) return false;
    const std::uint32_t section_bytes = offsets[1] - offsets[0];
    if (section_bytes < kPsfHeaderBytes || section_bytes > kMaxSfoBytes)
        return false;
    auto *buffer = static_cast<unsigned char *>(std::malloc(section_bytes));
    if (!buffer) return false;
    if (!ReadExactAt(fd, offsets[0], buffer, section_bytes) ||
        std::memcmp(buffer, "\0PSF", 4u) != 0)
    {
        std::free(buffer);
        return false;
    }
    const std::uint32_t key_table = ReadLe32(buffer + 8u);
    const std::uint32_t data_table = ReadLe32(buffer + 12u);
    const std::uint32_t count = ReadLe32(buffer + 16u);
    const std::uint64_t directory_end = static_cast<std::uint64_t>(
        kPsfHeaderBytes) + static_cast<std::uint64_t>(count) * kPsfEntryBytes;
    if (count > 1024u || directory_end > key_table || key_table > data_table ||
        data_table > section_bytes)
    {
        std::free(buffer);
        return false;
    }

    bool found = false;
    bool valid = true;
    for (std::uint32_t i = 0u; i < count && valid; ++i)
    {
        const unsigned char *entry =
            buffer + kPsfHeaderBytes + i * kPsfEntryBytes;
        const std::uint32_t key_at = key_table + ReadLe16(entry);
        const std::uint16_t format = ReadLe16(entry + 2u);
        const std::uint32_t value_length = ReadLe32(entry + 4u);
        const std::uint32_t value_capacity = ReadLe32(entry + 8u);
        const std::uint32_t value_relative = ReadLe32(entry + 12u);
        if (key_at < key_table || key_at >= data_table ||
            value_length > value_capacity || value_relative > section_bytes - data_table ||
            value_capacity > section_bytes - data_table - value_relative)
        {
            valid = false;
            break;
        }
        std::uint32_t key_end = key_at;
        while (key_end < data_table && buffer[key_end] != 0u) ++key_end;
        if (key_end == data_table)
        {
            valid = false;
            break;
        }
        const std::size_t key_bytes = key_end - key_at;
        if (key_bytes != sizeof(kXmbSfoKey) - 1u ||
            std::memcmp(buffer + key_at, kXmbSfoKey, key_bytes) != 0)
            continue;
        if (found || format != kPsfBinaryFormat || value_length != 20u ||
            value_capacity != 20u)
        {
            valid = false;
            break;
        }
        const unsigned char *value = buffer + data_table + value_relative;
        found = std::memcmp(value, kXmbMarker, sizeof(kXmbMarker)) == 0 &&
                ReadLe32(value + 8u) == kIconSlotBytes &&
                ReadLe32(value + 12u) == kPictureSlotBytes &&
                ReadLe32(value + 16u) == kSoundSlotBytes;
        if (!found) valid = false;
    }
    std::free(buffer);
    return valid && found;
}

bool PngChunkTypeEquals(const unsigned char type[4], const char expected[5])
{
    return std::memcmp(type, expected, 4u) == 0;
}

PngIdentity ValidatePngSection(SceUID fd, std::uint32_t begin,
                               std::uint32_t end,
                               std::uint32_t expected_width,
                               std::uint32_t expected_height,
                               unsigned char expected_role)
{
    if (end <= begin || end - begin < 45u ||
        end - begin > kPictureSlotBytes)
        return PngIdentity::Invalid;
    unsigned char signature[sizeof(kPngMagic)];
    if (!ReadExactAt(fd, begin, signature, sizeof(signature)) ||
        std::memcmp(signature, kPngMagic, sizeof(signature)) != 0)
    {
        return PngIdentity::Invalid;
    }

    std::uint32_t cursor = begin + sizeof(kPngMagic);
    unsigned int chunk_index = 0u;
    bool saw_ihdr = false;
    bool saw_idat = false;
    bool saw_selfwrap_chunk = false;
    bool previous_was_identity = false;
    bool contract_shape = false;
    unsigned char color_type = 0xffu;
    PngIdentity identity = PngIdentity::Foreign;
    while (cursor < end)
    {
        unsigned char chunk_header[8];
        if (end - cursor < 12u ||
            !ReadExactAt(fd, cursor, chunk_header, sizeof(chunk_header)))
        {
            return PngIdentity::Invalid;
        }
        const std::uint32_t length = ReadBe32(chunk_header);
        if (length > end - cursor - 12u) return PngIdentity::Invalid;
        const unsigned char *type = chunk_header + 4u;
        const bool is_ihdr = PngChunkTypeEquals(type, "IHDR");
        const bool is_idat = PngChunkTypeEquals(type, "IDAT");
        const bool is_iend = PngChunkTypeEquals(type, "IEND");
        const bool is_identity =
            std::memcmp(type, kSelfwrapChunkType,
                        sizeof(kSelfwrapChunkType)) == 0;
        if ((chunk_index == 0u) != is_ihdr || (is_ihdr && saw_ihdr) ||
            (is_iend && length != 0u))
        {
            return PngIdentity::Invalid;
        }

        uLong crc = crc32(0L, Z_NULL, 0);
        crc = crc32(crc, type, 4u);
        unsigned char ihdr[13]{};
        unsigned char identity_header[9]{};
        bool identity_padding_zero = true;
        unsigned char data[4096];
        std::uint32_t consumed = 0u;
        while (consumed < length)
        {
            const std::uint32_t remaining = length - consumed;
            const std::size_t bytes = remaining < sizeof(data)
                                          ? static_cast<std::size_t>(remaining)
                                          : sizeof(data);
            if (!ReadExactAt(fd, static_cast<std::uint64_t>(cursor) + 8u + consumed,
                             data, bytes))
            {
                return PngIdentity::Invalid;
            }
            if (is_ihdr && consumed < sizeof(ihdr))
            {
                const std::size_t copy = bytes < sizeof(ihdr) - consumed
                                             ? bytes
                                             : sizeof(ihdr) - consumed;
                std::memcpy(ihdr + consumed, data, copy);
            }
            if (is_identity)
            {
                for (std::size_t i = 0u; i < bytes; ++i)
                {
                    const std::uint32_t position =
                        consumed + static_cast<std::uint32_t>(i);
                    if (position < sizeof(identity_header))
                        identity_header[position] = data[i];
                    else if (data[i] != 0u)
                        identity_padding_zero = false;
                }
            }
            crc = crc32(crc, data, static_cast<uInt>(bytes));
            consumed += static_cast<std::uint32_t>(bytes);
        }
        unsigned char stored_crc[4];
        if (!ReadExactAt(fd, static_cast<std::uint64_t>(cursor) + 8u + length,
                         stored_crc, sizeof(stored_crc)) ||
            static_cast<std::uint32_t>(crc) != ReadBe32(stored_crc))
        {
            return PngIdentity::Invalid;
        }
        cursor += 12u + length;
        ++chunk_index;

        if (is_ihdr)
        {
            if (length != sizeof(ihdr) || ihdr[10] != 0u || ihdr[11] != 0u ||
                ihdr[12] > 1u)
                return PngIdentity::Invalid;
            color_type = ihdr[9];
            contract_shape = ReadBe32(ihdr) == expected_width &&
                ReadBe32(ihdr + 4u) == expected_height && ihdr[8] == 8u &&
                (color_type == PNG_COLOR_TYPE_RGB ||
                 color_type == PNG_COLOR_TYPE_RGBA) && ihdr[12] == 0u;
            saw_ihdr = true;
        }
        if (is_idat)
        {
            if (!saw_ihdr) return PngIdentity::Invalid;
            saw_idat = true;
        }
        if (is_identity)
        {
            const bool ownership_shape = !saw_selfwrap_chunk && saw_idat &&
                length >= sizeof(identity_header) && identity_padding_zero &&
                identity_header[8] == expected_role;
            if (ownership_shape &&
                std::memcmp(identity_header, kPlaceholderIdentity,
                            sizeof(kPlaceholderIdentity)) == 0)
            {
                identity = PngIdentity::Placeholder;
            }
            else if (ownership_shape &&
                     std::memcmp(identity_header, kWrappedIdentity,
                                 sizeof(kWrappedIdentity)) == 0)
            {
                identity = PngIdentity::Wrapped;
            }
            else
            {
                identity = PngIdentity::Foreign;
            }
            saw_selfwrap_chunk = true;
        }
        if (is_iend)
        {
            if (!saw_ihdr || !saw_idat)
                return PngIdentity::Invalid;
            if (!contract_shape || cursor != end || !saw_selfwrap_chunk ||
                !previous_was_identity)
                return PngIdentity::Foreign;
            const bool color_matches =
                (identity == PngIdentity::Placeholder &&
                 color_type == PNG_COLOR_TYPE_RGBA) ||
                (identity == PngIdentity::Wrapped &&
                 color_type == PNG_COLOR_TYPE_RGB);
            return color_matches ? identity : PngIdentity::Foreign;
        }
        previous_was_identity = is_identity;
    }
    return PngIdentity::Invalid;
}

// The SND0 slot: exactly the header th10_xmb_at3_header writes (RIFF size
// aside), its frames, then the identity chunk to the slot's end. Another
// RIFF there is foreign media; ours with a broken tail (a torn write) is
// invalid and may be repaired.
PngIdentity ValidateAt3Section(SceUID fd, std::uint32_t begin,
                               std::uint32_t end, unsigned char expected_role)
{
    if (end <= begin || end - begin != kSoundSlotBytes) return PngIdentity::Invalid;
    unsigned char header[kTh10At3HeaderBytes];
    if (!ReadExactAt(fd, begin, header, sizeof(header))) return PngIdentity::Invalid;
    // The data chunk's size (the header's last field) gives the frames.
    const std::uint32_t data_bytes = ReadLe32(header + kTh10At3HeaderBytes - 4u);
    const std::uint32_t frames =
        data_bytes / static_cast<std::uint32_t>(kTh10At3FrameBytes);
    unsigned char expected[kTh10At3HeaderBytes];
    th10_xmb_at3_header(expected, frames);
    if (std::memcmp(header, "RIFF", 4u) != 0 ||
        ReadLe32(header + 4u) != end - begin - 8u ||
        std::memcmp(header + 8u, expected + 8u, sizeof(header) - 8u) != 0 ||
        frames < 16u || data_bytes % kTh10At3FrameBytes ||
        static_cast<std::uint64_t>(frames) * kTh10At3FrameBytes >
            end - begin - kTh10At3HeaderBytes - kAt3IdentityChunkBytes)
    {
        return PngIdentity::Foreign;
    }
    const std::uint32_t chunk =
        begin + static_cast<std::uint32_t>(kTh10At3HeaderBytes) +
        frames * static_cast<std::uint32_t>(kTh10At3FrameBytes);
    unsigned char chunk_header[8u + 9u];
    if (!ReadExactAt(fd, chunk, chunk_header, sizeof(chunk_header)) ||
        std::memcmp(chunk_header, kSelfwrapChunkType,
                    sizeof(kSelfwrapChunkType)) != 0 ||
        ReadLe32(chunk_header + 4u) != end - chunk - 8u ||
        chunk_header[16] != expected_role)
    {
        return PngIdentity::Invalid;
    }
    unsigned char data[4096];
    for (std::uint32_t at = chunk + sizeof(chunk_header); at < end;)
    {
        const std::uint32_t bytes =
            end - at < sizeof(data) ? end - at : static_cast<std::uint32_t>(sizeof(data));
        if (!ReadExactAt(fd, at, data, bytes)) return PngIdentity::Invalid;
        for (std::uint32_t i = 0u; i < bytes; ++i)
            if (data[i]) return PngIdentity::Invalid;
        at += bytes;
    }
    if (std::memcmp(chunk_header + 8u, kPlaceholderIdentity,
                    sizeof(kPlaceholderIdentity)) == 0)
        return PngIdentity::Placeholder;
    if (std::memcmp(chunk_header + 8u, kWrappedIdentity,
                    sizeof(kWrappedIdentity)) == 0)
        return PngIdentity::Wrapped;
    return PngIdentity::Foreign;
}

bool InspectPbp(const char *path, PbpInfo *info)
{
    if (!path || !info) return false;
    info->foreign_media = false;
    info->placeholder = false;
    info->wrapped = false;
    SceIoStat stat{};
    if (sceIoGetstat(path, &stat) < 0 || stat.st_size < kPbpHeaderBytes)
        return false;
    info->file_size = static_cast<std::uint64_t>(stat.st_size);
    const SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return false;
    bool ok = ReadExact(fd, info->header, sizeof(info->header)) &&
              std::memcmp(info->header, kPbpMagic, sizeof(kPbpMagic)) == 0;
    if (ok)
    {
        for (unsigned int i = 0u; i < 8u; ++i)
            info->offsets[i] = ReadLe32(info->header + 8u + i * 4u);
        ok = PbpOffsetsMonotonic(info->offsets, info->file_size) &&
             FindSfoSlotContract(fd, info->offsets);
        info->reserve_start = info->offsets[1];
        info->reserve_end = info->offsets[6];
    }
    if (ok)
    {
        const std::uint32_t icon_start = info->offsets[1];
        const std::uint64_t picture_start64 =
            static_cast<std::uint64_t>(icon_start) + kIconSlotBytes;
        const std::uint64_t sound_start64 = picture_start64 + kPictureSlotBytes;
        const std::uint64_t data_start64 = sound_start64 + kSoundSlotBytes;
        ok = data_start64 <= UINT32_MAX &&
             info->offsets[2] == picture_start64 &&
             info->offsets[3] == picture_start64 &&
             info->offsets[4] == picture_start64 &&
             info->offsets[5] == sound_start64 &&
             info->offsets[6] == data_start64;
        if (ok)
        {
            const std::uint32_t picture_start =
                static_cast<std::uint32_t>(picture_start64);
            const std::uint32_t sound_start =
                static_cast<std::uint32_t>(sound_start64);
            const PngIdentity icon = ValidatePngSection(
                fd, icon_start, picture_start, 144u, 80u, 'I');
            const PngIdentity picture = ValidatePngSection(
                fd, picture_start, sound_start, 480u, 272u, 'P');
            const PngIdentity sound = ValidateAt3Section(
                fd, sound_start, info->reserve_end, 'S');
            info->foreign_media = icon == PngIdentity::Foreign ||
                                  picture == PngIdentity::Foreign ||
                                  sound == PngIdentity::Foreign;
            info->placeholder = icon == PngIdentity::Placeholder &&
                                picture == PngIdentity::Placeholder &&
                                sound == PngIdentity::Placeholder;
            info->wrapped = icon == PngIdentity::Wrapped &&
                            picture == PngIdentity::Wrapped &&
                            sound == PngIdentity::Wrapped;
        }
    }
    if (sceIoClose(fd) < 0) ok = false;
    return ok;
}

bool RangeMatches(SceUID fd, std::uint32_t offset, const unsigned char *expected,
                  std::size_t bytes)
{
    if (sceIoLseek(fd, static_cast<SceOff>(offset), PSP_SEEK_SET) !=
        static_cast<SceOff>(offset))
    {
        return false;
    }
    unsigned char buffer[4096];
    std::size_t done = 0u;
    while (done < bytes)
    {
        const std::size_t remaining = bytes - done;
        const unsigned int request = remaining > sizeof(buffer)
                                         ? sizeof(buffer)
                                         : static_cast<unsigned int>(remaining);
        if (!ReadExact(fd, buffer, request) ||
            std::memcmp(buffer, expected + done, request) != 0)
        {
            return false;
        }
        done += request;
    }
    return true;
}

bool SyncPathDevice(const char *path)
{
    if (!path) return false;
    const char *colon = std::strchr(path, ':');
    if (!colon) return false;
    const std::size_t length = static_cast<std::size_t>(colon - path + 1);
    if (!length || length >= 8u) return false;
    char device[8];
    std::memcpy(device, path, length);
    device[length] = '\0';
    return sceIoSync(device, 0) >= 0;
}

int OpenExistingForWrite(const char *path, std::uint64_t expected_size,
                         SceUID *opened)
{
    if (!opened) return kSelfwrapCommitFailure;
    *opened = -1;
    SceIoStat stat{};
    if (sceIoGetstat(path, &stat) < 0 || stat.st_size < 0 ||
        static_cast<std::uint64_t>(stat.st_size) != expected_size)
    {
        return kSelfwrapCommitFailure;
    }
    // Never add O_CREAT here.  If the canonical PBP disappeared between stat
    // and open, creating an empty file would turn a write-open failure into a
    // destructive mutation.  PPSSPP's running-file denial is returned as a
    // deferred selfwrap; real PSP storage accepts this existing-only handle.
    const SceUID fd = sceIoOpen(path, PSP_O_WRONLY, 0);
    if (fd < 0) return kSelfwrapWriteOpenDenied;
    SceIoStat after{};
    if (sceIoGetstat(path, &after) < 0 || after.st_size < 0 ||
        static_cast<std::uint64_t>(after.st_size) != expected_size)
    {
        sceIoClose(fd);
        return kSelfwrapCommitFailure;
    }
    *opened = fd;
    return 0;
}

int WriteAndVerifyAssets(const char *path, const PbpInfo &contract,
                         const ByteBuffer &icon,
                         const ByteBuffer &picture,
                         const ByteBuffer &sound,
                         PngIdentity expected_identity)
{
    if (icon.size != kIconSlotBytes || picture.size != kPictureSlotBytes ||
        sound.size != kSoundSlotBytes ||
        (expected_identity != PngIdentity::Placeholder &&
         expected_identity != PngIdentity::Wrapped))
    {
        return kSelfwrapReserveFailure;
    }
    const std::uint32_t icon_start = contract.offsets[1];
    const std::uint32_t picture_start = contract.offsets[4];
    const std::uint32_t sound_start = contract.offsets[5];
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return kSelfwrapCommitFailure;
    unsigned char current_header[kPbpHeaderBytes];
    bool ok = ReadExactAt(fd, 0u, current_header, sizeof(current_header)) &&
              std::memcmp(current_header, contract.header,
                          sizeof(current_header)) == 0;
    if (sceIoClose(fd) < 0) ok = false;
    if (!ok) return kSelfwrapCommitFailure;

    const int open_result =
        OpenExistingForWrite(path, contract.file_size, &fd);
    if (open_result < 0) return open_result;
    ok = WriteExactAt(fd, icon_start, icon.data, icon.size) &&
         WriteExactAt(fd, picture_start, picture.data, picture.size) &&
         WriteExactAt(fd, sound_start, sound.data, sound.size);
    if (sceIoClose(fd) < 0) ok = false;
    if (!SyncPathDevice(path)) ok = false;
    if (!ok) return kSelfwrapCommitFailure;

    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return kSelfwrapCommitFailure;
    const PngIdentity icon_identity = ValidatePngSection(
        fd, icon_start, picture_start, 144u, 80u, 'I');
    const PngIdentity picture_identity = ValidatePngSection(
        fd, picture_start, sound_start, 480u, 272u, 'P');
    const PngIdentity sound_identity = ValidateAt3Section(
        fd, sound_start, contract.reserve_end, 'S');
    ok = RangeMatches(fd, icon_start, icon.data, icon.size) &&
         RangeMatches(fd, picture_start, picture.data, picture.size) &&
         RangeMatches(fd, sound_start, sound.data, sound.size) &&
         icon_identity == expected_identity &&
         picture_identity == expected_identity &&
         sound_identity == expected_identity;
    if (sceIoClose(fd) < 0) ok = false;
    return ok ? 0 : kSelfwrapCommitFailure;
}

int CommitAssets(const char *path, const PbpInfo &initial,
                 const ByteBuffer &icon, const ByteBuffer &picture,
                 const ByteBuffer &sound, PngIdentity expected_identity)
{
    if (initial.foreign_media) return kSelfwrapForeignMedia;
    const int result = WriteAndVerifyAssets(
        path, initial, icon, picture, sound, expected_identity);
    if (result < 0) return result;

    PbpInfo verified{};
    if (!InspectPbp(path, &verified) ||
        (expected_identity == PngIdentity::Wrapped ? !verified.wrapped
                                                   : !verified.placeholder) ||
        verified.file_size != initial.file_size ||
        verified.reserve_start != initial.reserve_start ||
        verified.reserve_end != initial.reserve_end ||
        std::memcmp(verified.header, initial.header,
                    sizeof(initial.header)) != 0)
    {
        return kSelfwrapCommitFailure;
    }
    return 1;
}
} // namespace

extern "C" int th10_unified_find_original_data(
    const char *appdir, const char *launch_device, char *out,
    std::size_t out_size)
{
    if (!appdir || !appdir[0] || !launch_device || !launch_device[0] ||
        !out || !out_size)
    {
        return kSelfwrapBadArgument;
    }
    out[0] = '\0';
    // Only where the runtime will look: th10run.txt's --data, else appdir.
    char option[kMaxPath];
    const int has_option = ReadRunDataOption(appdir, option, sizeof(option));
    if (has_option < 0) return has_option;
    const char *root = has_option ? option : appdir;
    if (!IsCompleteDataRoot(root)) return 0;
    return CopyPath(out, out_size, root) ? 1 : kSelfwrapBadArgument;
}

extern "C" int th10_unified_selfwrap_needs_generation(
    const char *eboot_path, const char *data_root)
{
    if (!eboot_path || !eboot_path[0]) return kSelfwrapBadArgument;

    PbpInfo pbp{};
    if (!InspectPbp(eboot_path, &pbp)) return kSelfwrapBadPbp;
    if (pbp.foreign_media) return kSelfwrapForeignMedia;
    if (pbp.wrapped) return 0;

    return data_root && data_root[0] && IsCompleteDataRoot(data_root) ? 1 : 0;
}

extern "C" int th10_unified_try_selfwrap(
    const char *appdir, const char *eboot_path, const char *data_root)
{
    if (!appdir || !appdir[0] || !eboot_path || !eboot_path[0])
        return kSelfwrapBadArgument;

    PbpInfo pbp{};
    if (!InspectPbp(eboot_path, &pbp)) return kSelfwrapBadPbp;
    if (pbp.foreign_media) return kSelfwrapForeignMedia;
    if (pbp.wrapped) return 0;
    const bool have_original = data_root && data_root[0] &&
                               IsCompleteDataRoot(data_root);
    if (pbp.placeholder && !have_original) return 0;

    // Probe before archive decompression, PNG and ATRAC3 encoding. PPSSPP
    // refuses a write handle for the currently booted EBOOT; opening
    // existing-only and immediately closing it changes no bytes and lets that
    // environment defer cheaply while real PSP storage continues into the
    // in-place path.
    SceUID probe = -1;
    const int probe_result =
        OpenExistingForWrite(eboot_path, pbp.file_size, &probe);
    if (probe_result < 0) return probe_result;
    if (sceIoClose(probe) < 0) return kSelfwrapCommitFailure;

    ByteBuffer icon{};
    ByteBuffer picture{};
    ByteBuffer sound{};
    g_last_sound_encode_us = 0u;
    const bool generated =
        have_original ? GenerateAssets(data_root, &icon, &picture, &sound)
                      : GeneratePlaceholderAssets(&icon, &picture, &sound);
    if (!generated)
        return kSelfwrapImageFailure;
    const int result = CommitAssets(
        eboot_path, pbp, icon, picture, sound,
        have_original ? PngIdentity::Wrapped : PngIdentity::Placeholder);
    FreeBuffer(&icon);
    FreeBuffer(&picture);
    FreeBuffer(&sound);
    if (result == 1) Report(1000u);
    return result;
}

extern "C" void th10_unified_set_progress(void (*hook)(unsigned int permille))
{
    g_progress = hook;
}

extern "C" unsigned int th10_unified_last_sound_encode_us(void)
{
    return g_last_sound_encode_us;
}

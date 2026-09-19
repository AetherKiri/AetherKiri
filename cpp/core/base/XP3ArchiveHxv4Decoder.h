#ifndef XP3ArchiveHxv4DecoderH
#define XP3ArchiveHxv4DecoderH

#include <cstdint>
#include <cstddef>
#include <vector>
#include "XP3Archive.h"
#include "tjsString.h"

namespace XP3ArchiveHxv4Decoder {
    void RegisterArchive(const tTVPXP3Archive* archive_ptr, const uint8_t* table_blob, size_t blob_size, uint16_t flags, const std::vector<uint32_t>& protected_hashes);
    void UnregisterArchive(const tTVPXP3Archive* archive_ptr);
    bool Decode(const tTVPXP3Archive* archive_ptr, uint32_t file_hash, uint64_t offset, void* buffer, uint32_t size);
    uint32_t GetFileHashByName(const tTVPXP3Archive* archive_ptr, const ttstr& name);
    bool IsArchiveHxv4(const tTVPXP3Archive* archive_ptr);
    void ExecuteHxv4Crawler(const ttstr& initial_file, const ttstr& output_dir);
}

#endif
#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include "StorageIntf.h" 
#include "Hxv4Crypto.h" 


class tTVPXP3Archive;
struct tTVPXP3ExtractionFilterInfo;
namespace TJS {
    class tTJSVariant;
}

namespace AetherKiri {
namespace Hxv4 {

struct DripOp { uint32_t param; uint32_t op; };

class Hxv4Session {
public:
    Hxv4Session();
    ~Hxv4Session();

    void SetDripData(const std::vector<uint8_t>& dripData);
    bool InitializeDripVM(const ttstr& json_path);
    void RegisterChunkState(const uint8_t* chunk_data, uint32_t chunk_size);
    bool DecryptChunk(uint32_t file_hash, uint8_t* buffer, size_t size, uint64_t offset);
    void ClearState();

public:
    bool m_dripDataLoaded;
    std::vector<uint8_t> m_dripData;
    std::vector<uint8_t> m_key; 
     // Cryptographic state variable
    std::vector<uint8_t> m_nonce0;
    std::vector<uint8_t> m_nonce1;
    std::vector<uint32_t> m_holderWords;
    std::vector<uint32_t> m_contextU32;
    std::vector<std::vector<DripOp>> m_dripLanes;
    std::vector<DripOp*> m_dripLanesPtrs;
    
    std::unordered_map<uint32_t, FilterRuntimeState> m_fileStates;
    std::unordered_map<uint64_t, ttstr> m_prefixToPath;
    std::mutex m_stateMutex;
};

class Hxv4DecoderPlugin {
public:
    static void Initialize();

   
    static void Teardown();
    static std::shared_ptr<Hxv4Session> GetCurrentSession();

private:
    static bool StorageResolverCallback(const ttstr& requested, ttstr& resolved);
    static void UnknownChunkCallback(tTVPXP3Archive *archive, tTJSBinaryStream *stream, tjs_int64 stream_offset, const tjs_uint8 *chunk_name, const tjs_uint8 *chunk_data, tjs_uint size);
    static void ExtractionFilterCallback(tTVPXP3ExtractionFilterInfo *info, TJS::tTJSVariant *ctx);

private:
    static std::shared_ptr<Hxv4Session> s_currentSession;
    static std::mutex s_providerMutex;
    static bool s_initialized;
    static void (*s_prevExtractionFilter)(tTVPXP3ExtractionFilterInfo *info, TJS::tTJSVariant *ctx);
};

} // namespace Hxv4
} // namespace AetherKiri

#include "Hxv4Decoder.h"
#include "StorageIntf.h"
#include "XP3Archive.h"
#include "DebugIntf.h"
#include <zlib.h>
#include <sodium.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstring>
#include "UtilStreams.h"

namespace AetherKiri {
namespace Hxv4 {

class Hxv4VirtualMedia : public iTVPStorageMedia {
    std::atomic<int> m_refCount{1};
public:
    void AddRef() override { m_refCount++; }
    void Release() override { if (--m_refCount == 0) delete this; }
    void GetName(ttstr &name) override { name = TJS_W("virtual"); }
    void NormalizeDomainName(ttstr &name) override {}
    void NormalizePathName(ttstr &name) override {}
    bool CheckExistentStorage(const ttstr &name) override { 
        return name.GetLen() >= 11 && !TJS_strcmp(name.c_str() + name.GetLen() - 11, TJS_W("startup.tjs")); 
    }
    tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags) override {
        if (name.GetLen() >= 11 && !TJS_strcmp(name.c_str() + name.GetLen() - 11, TJS_W("startup.tjs"))) {
            static const char* script = "Scripts.execStorage(\"system/Initialize.tjs\");";
            return new tTVPMemoryStream(script, std::strlen(script));
        }
        return nullptr;
    }
    void GetListAt(const ttstr &name, iTVPStorageLister *lister) override {}
    void GetLocallyAccessibleName(ttstr &name) override { name = TJS_W(""); }
};

static Hxv4VirtualMedia* s_virtualMedia = nullptr;

std::shared_ptr<Hxv4Session> Hxv4DecoderPlugin::s_currentSession = nullptr;
std::mutex Hxv4DecoderPlugin::s_providerMutex;
bool Hxv4DecoderPlugin::s_initialized = false;
void (*Hxv4DecoderPlugin::s_prevExtractionFilter)(tTVPXP3ExtractionFilterInfo*, TJS::tTJSVariant*) = nullptr;

struct RawChunkData {
    ttstr archiveName;
    uint16_t flags;
    std::vector<uint8_t> payload;
    std::vector<uint32_t> protected_hashes;
    std::vector<ttstr> protected_names;
};
static std::vector<RawChunkData> s_rawChunks;
static std::mutex s_rawChunksMutex;
static bool s_lazyLoadAttempted = false;

static std::vector<uint8_t> HexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = (uint8_t)strtol(byteString.c_str(), nullptr, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

Hxv4Session::Hxv4Session() : m_dripDataLoaded(false) {}
Hxv4Session::~Hxv4Session() { ClearState(); }

void Hxv4Session::ClearState() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_dripDataLoaded = false;
    if (!m_key.empty()) { std::fill(m_key.begin(), m_key.end(), 0); m_key.clear(); }
    if (!m_nonce0.empty()) { std::fill(m_nonce0.begin(), m_nonce0.end(), 0); m_nonce0.clear(); }
    if (!m_nonce1.empty()) { std::fill(m_nonce1.begin(), m_nonce1.end(), 0); m_nonce1.clear(); }
    m_holderWords.clear();
    m_contextU32.clear();
    m_dripLanes.clear();
    m_dripLanesPtrs.clear();
    m_fileStates.clear();
    m_prefixToPath.clear();
}

bool Hxv4Session::InitializeDripVM(const ttstr& json_path) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    iTJSTextReadStream* stream = TJSCreateTextStreamForRead(json_path, ttstr(L"UTF-8"));
    if (!stream) {
        TVPAddImportantLog(TJS_W("Hxv4DecoderPlugin Error: Failed to open drip_program.json stream!"));
        return false;
    }
    ttstr json_str; stream->Read(json_str, 0); stream->Destruct();
    
    try {
        auto j = nlohmann::json::parse(json_str.AsStdString());
        m_key = HexToBytes(j["hxv4_key"].get<std::string>());
        m_nonce0 = HexToBytes(j["hxv4_nonce0"].get<std::string>());
        m_nonce1 = HexToBytes(j["hxv4_nonce1"].get<std::string>());
        auto holders = j["holder_words"];
        for (const auto& w : holders) m_holderWords.push_back(w.get<uint32_t>());
        auto ctx = j["context_u32"];
        for (const auto& w : ctx) m_contextU32.push_back(w.get<uint32_t>());
        
        auto lanes = j["lanes"];
        m_dripLanes.resize(128); m_dripLanesPtrs.resize(128, nullptr);
        for (const auto& lane_obj : lanes) {
            size_t idx = lane_obj["index"].get<size_t>();
            if (idx >= 128) continue;
            
            auto records = lane_obj["records"];
            for (const auto& record : records) {
                DripOp op;
                op.param = record[0].get<uint32_t>();
                op.op = record[1].get<uint32_t>();
                m_dripLanes[idx].push_back(op);
            }
            m_dripLanesPtrs[idx] = m_dripLanes[idx].data();
        }
        m_dripDataLoaded = true;
        return true;
    } catch (const std::exception& e) { 
        TVPAddImportantLog(ttstr(TJS_W("Hxv4DecoderPlugin JSON Error: ")) + ttstr(e.what()));
        return false; 
    }
}

void Hxv4Session::SetDripData(const std::vector<uint8_t>& dripData) {}
void Hxv4Session::RegisterChunkState(const uint8_t* chunk_data, uint32_t chunk_size) { }

bool Hxv4Session::DecryptChunk(uint32_t file_hash, uint8_t* buffer, size_t size, uint64_t offset) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_dripDataLoaded || !buffer || size == 0 || size > 64 * 1024 * 1024 || offset > UINT64_MAX - size) return false;
    auto it = m_fileStates.find(file_hash);
    if (it != m_fileStates.end()) {
        it->second.Apply(buffer, size, offset);
        return true;
    }
    return false;
}

void Hxv4DecoderPlugin::Initialize() {
    std::lock_guard<std::mutex> lock(s_providerMutex);
    if (s_initialized) return;
    s_currentSession = std::make_shared<Hxv4Session>();
    s_virtualMedia = new Hxv4VirtualMedia();
    s_virtualMedia->AddRef();
    TVPRegisterStorageMedia(s_virtualMedia);
    TVPRegisterStorageResolver(&Hxv4DecoderPlugin::StorageResolverCallback);
    TVPRegisterXP3UnknownChunkFilter(&Hxv4DecoderPlugin::UnknownChunkCallback);
    s_prevExtractionFilter = TVPXP3ArchiveExtractionFilter;
    TVPSetXP3ArchiveExtractionFilter(&Hxv4DecoderPlugin::ExtractionFilterCallback);
    s_lazyLoadAttempted = false;
    s_rawChunks.clear();
    s_initialized = true;
}

void Hxv4DecoderPlugin::Teardown() {
    std::lock_guard<std::mutex> lock(s_providerMutex);
    if (!s_initialized) return;
    TVPUnregisterStorageResolver(&Hxv4DecoderPlugin::StorageResolverCallback);
    TVPUnregisterXP3UnknownChunkFilter(&Hxv4DecoderPlugin::UnknownChunkCallback);
    if (s_virtualMedia) { TVPUnregisterStorageMedia(s_virtualMedia); s_virtualMedia->Release(); s_virtualMedia = nullptr; }
    if (TVPXP3ArchiveExtractionFilter == &Hxv4DecoderPlugin::ExtractionFilterCallback) TVPSetXP3ArchiveExtractionFilter(s_prevExtractionFilter);
    s_currentSession.reset();
    s_rawChunks.clear();
    s_initialized = false;
}

std::shared_ptr<Hxv4Session> Hxv4DecoderPlugin::GetCurrentSession() {
    std::lock_guard<std::mutex> lock(s_providerMutex); return s_currentSession;
}

/**
 * UnknownChunkCallback
 * This callback is called by the core engine whenever it finds a rare XP3 chunk.
 * For Hxv4, this function directly “snatches” the XChaCha20 payload
 * into RAM using an open core stream, preventing Android from blocking
 * the file stream later on (preventing the ‘Cannot open storage’ and Use-After-Free bugs).
 */
void Hxv4DecoderPlugin::UnknownChunkCallback(tTVPXP3Archive *archive, tTJSBinaryStream *stream, tjs_int64 stream_offset, const tjs_uint8 *chunk_name, const tjs_uint8 *chunk_data, tjs_uint size) {
    if (std::memcmp(chunk_name, "Hxv4", 4) == 0 && size >= 14) {
        uint64_t hxv4_offset; uint32_t hxv4_size; uint16_t hxv4_flags;
        std::memcpy(&hxv4_offset, chunk_data, 8);
        std::memcpy(&hxv4_size, chunk_data + 8, 4);
        std::memcpy(&hxv4_flags, chunk_data + 12, 2);
        
        if (hxv4_size > 16 && hxv4_size < 64 * 1024 * 1024) {
            RawChunkData rd;
            rd.archiveName = archive->GetName();
            rd.flags = hxv4_flags;
            rd.payload.resize(hxv4_size);
            
            uint64_t old_pos = stream->GetPosition();
            stream->SetPosition(hxv4_offset + stream_offset);
            stream->ReadBuffer(rd.payload.data(), hxv4_size);
            stream->SetPosition(old_pos);
            
            tjs_uint count = archive->GetCount();
            for(tjs_uint i = 0; i < count; i++) {
                ttstr arc_item_name = archive->GetName(i);
                if (arc_item_name != TJS_W("startup.tjs")) {
                    rd.protected_hashes.push_back(archive->GetFileHash(i));
                    rd.protected_names.push_back(arc_item_name);
                }
            }
            
            std::lock_guard<std::mutex> lock(s_rawChunksMutex);
            s_rawChunks.push_back(std::move(rd));
        }
    }
}

/**
 * TryLazyLoadDripVM
 * Called “lazily” just before file decryption.
 * Initializes the virtual machine (DripVM) from JSON, then decrypts the
 * XChaCha20 payload stored in RAM, and maps the structure of the encrypted files
 * using a prefix hash obfuscated by the developer.
 */
static void TryLazyLoadDripVM(Hxv4Session* session) {
    if (!session->m_dripDataLoaded) {
        if (s_lazyLoadAttempted) return;
        s_lazyLoadAttempted = true;
        ttstr jsonPath = TVPGetAppPath() + TJS_W("keys.json");
        if (!TVPIsExistentStorageNoSearch(jsonPath)) return;
        if (!session->InitializeDripVM(jsonPath)) return;
        //TVPAddImportantLog(TJS_W("(info) Hxv4DecoderPlugin: Lazy Loaded drip_program.json"));
    }

    std::vector<RawChunkData> local_chunks;
    {
        std::lock_guard<std::mutex> lock(s_rawChunksMutex);
        if (s_rawChunks.empty()) return;
        local_chunks.swap(s_rawChunks);
    }

    for (const auto& chunk : local_chunks) {
        if (sodium_init() < 0) continue;
        uint32_t dec_size = chunk.payload.size() - 16;
        std::vector<uint8_t> dec_payload(dec_size);
        const uint8_t* nonce = (chunk.flags & 1) ? session->m_nonce1.data() : session->m_nonce0.data();
        
        if (crypto_aead_xchacha20poly1305_ietf_decrypt_detached(dec_payload.data(), NULL, chunk.payload.data() + 16, dec_size, chunk.payload.data(), NULL, 0, nonce, session->m_key.data()) == 0) {
            uint32_t uncomp_size = *(uint32_t*)(dec_payload.data());
            if (uncomp_size > 0 && uncomp_size < 64 * 1024 * 1024) {
                std::vector<uint8_t> table(uncomp_size);
                unsigned long destlen = uncomp_size;
                if (uncompress(table.data(), &destlen, dec_payload.data() + 4, dec_size - 4) == Z_OK) {
                    const uint8_t* p = table.data();
                    const uint8_t* p_end = p + destlen;
                    auto read_i32 = [&]() -> int32_t { int32_t v = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; p+=4; return v; };
                    auto read_i64 = [&]() -> int64_t { int64_t hi=(int64_t)(read_i32())&0xFFFFFFFFLL; uint32_t lo=(uint32_t)(read_i32()); return (hi<<32)|lo; };

                    std::lock_guard<std::mutex> slock(session->m_stateMutex);
                    tjs_char delim[2] = { TVPArchiveDelimiter, 0 };

                    if (p < p_end && *p++ == 0x81) {
                        int32_t domain_count = read_i32();
                        for (int i = 0; i < domain_count; i += 2) {
                            if (p >= p_end || *p++ != 0x03) break; p += read_i32();
                            if (p >= p_end || *p++ != 0x81) break; int32_t item_count = read_i32();
                            for (int j = 0; j < item_count; j += 2) {
                                if (p >= p_end || *p++ != 0x03) break;
                                int32_t fhash_len = read_i32();
                                uint64_t hash_prefix = 0; std::memcpy(&hash_prefix, p, 8); p += fhash_len;
                                if (p >= p_end || *p++ != 0x81) break; read_i32();
                                p++; uint64_t packed = read_i64();
                                p++; uint64_t key = read_i64();
                                
                                uint32_t archive_slot = (packed >> 16) & 0xFFFF;
                                uint32_t filter_flag = packed & 0xFFFF;
                                if (archive_slot == 0 && filter_flag < chunk.protected_hashes.size()) {
                                    uint32_t file_hash = chunk.protected_hashes[filter_flag];
                                    ttstr physical_name = chunk.protected_names[filter_flag];
                                    uint32_t key_lo = key & 0xFFFFFFFF; uint32_t key_hi = key >> 32;
                                    if (!(chunk.flags & 1)) { key_lo ^= session->m_holderWords[2]; key_hi ^= session->m_holderWords[3]; }
                                    
                                    session->m_fileStates[file_hash] = SetupDripVM(((uint64_t)key_hi << 32) | key_lo, session);
                                    session->m_prefixToPath[hash_prefix] = chunk.archiveName + delim + physical_name;
                                    //TVPAddImportantLog(TJS_W("Hxv4 Decoder: Mapped Prefix ") + ttstr((tjs_int64)hash_prefix) + TJS_W(" to ") + physical_name);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void Hxv4DecoderPlugin::ExtractionFilterCallback(tTVPXP3ExtractionFilterInfo *info, TJS::tTJSVariant *ctx) {
    auto session = GetCurrentSession();
    bool handled = false;
    if (session) {
        TryLazyLoadDripVM(session.get());
        handled = session->DecryptChunk(info->FileHash, (uint8_t*)info->Buffer, info->BufferSize, info->Offset);
    }
    if (!handled && s_prevExtractionFilter) s_prevExtractionFilter(info, ctx);
}

/**
 * StorageResolverCallback
 * Intercepts file requests (TVPGetPlacedPath) to resolve virtual file paths
 * that have been obfuscated by the developer. If the hash prefix matches the extracted map,
 * this will redirect KiriKiri to the correct file.
 */
bool Hxv4DecoderPlugin::StorageResolverCallback(const ttstr& requested, ttstr& resolved) {
    auto session = GetCurrentSession();
    if (!session) return false;
    TryLazyLoadDripVM(session.get());
    if (!session->m_dripDataLoaded) return false;

    if (requested.GetLen() >= 11 && !TJS_strcmp(requested.c_str() + requested.GetLen() - 11, TJS_W("startup.tjs"))) {
        resolved = TJS_W("virtual://hxv4/startup.tjs");
        return true;
    }

    uint64_t hash_prefix = Hxv4_CalcHashPrefix(requested.c_str());
    std::lock_guard<std::mutex> lock(session->m_stateMutex);
    
    // DEBUG LOGGING
    // TVPAddImportantLog(TJS_W("Hxv4 StorageResolver: Requested = ") + requested + 
    //                    TJS_W(", Prefix = ") + ttstr((tjs_int64)hash_prefix));

    auto it = session->m_prefixToPath.find(hash_prefix);
    if (it != session->m_prefixToPath.end()) {
        resolved = it->second;
        // TVPAddImportantLog(TJS_W("Hxv4 StorageResolver: Resolved to -> ") + resolved);
        return true;
    }
    return false; 
}

} // namespace Hxv4
} // namespace AetherKiri

#define NCB_MODULE_NAME TJS_W("hxv4_decoder.dll")
#include "ncbind.hpp"
static void Hxv4DecoderPlugin_Initialize() { AetherKiri::Hxv4::Hxv4DecoderPlugin::Initialize(); }
static void Hxv4DecoderPlugin_Teardown() { AetherKiri::Hxv4::Hxv4DecoderPlugin::Teardown(); }
NCB_POST_REGIST_CALLBACK(Hxv4DecoderPlugin_Initialize);
NCB_PRE_UNREGIST_CALLBACK(Hxv4DecoderPlugin_Teardown);
extern "C" void TVPRegisterHxv4ProviderPluginAnchor() {}

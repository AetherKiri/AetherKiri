//---------------------------------------------------------------------------
// Built-in decoders for protected XP3 archives.
//---------------------------------------------------------------------------

#ifndef XP3ArchiveCxDecoderH
#define XP3ArchiveCxDecoderH

#include <cstddef>
#include <cstdint>

bool TVPIsBuiltinXP3CxScheme(std::uint32_t fingerprint);
bool TVPShouldUseBuiltinXP3CxDecoder(std::uint32_t fingerprint,
                                    const void *header,
                                    std::size_t headerSize);
bool TVPActivateBuiltinXP3CxDecoder(std::uint32_t fingerprint);
void TVPResetBuiltinXP3CxDecoder();
bool TVPDecodeBuiltinXP3Cx(std::uint32_t hash, std::uint64_t offset,
                           void *buffer, std::uint32_t size);

#endif

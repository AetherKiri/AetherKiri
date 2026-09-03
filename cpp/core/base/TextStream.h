#pragma once
#include "StorageIntf.h"

#include <optional>

iTJSTextReadStream *TVPCreateTextStreamForRead(const ttstr &name,
                                               const ttstr &mode);
iTJSTextWriteStream *TVPCreateTextStreamForWrite(const ttstr &name,
                                                 const ttstr &mode);

std::string checkTextEncoding(const void *buf, size_t size,
                              std::uint8_t &bomSize);

// Return the byte offset of a KiriKiri payload appended to a BMP save
// thumbnail.  The stream position is restored before returning.  This is
// Scripts.loadDataPack uses this stream-level form of the same detection that
// the text-stream reader applies to its in-memory payload.
std::optional<tjs_uint64> TVPFindEmbeddedBmpPayloadOffset(
    tTJSBinaryStream *stream);

void TVPSetDefaultReadEncoding(const ttstr &encoding);

const tjs_char *TVPGetDefaultReadEncoding();

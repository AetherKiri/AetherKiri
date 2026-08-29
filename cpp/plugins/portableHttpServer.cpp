#include "CharacterSet.h"
#include "DebugIntf.h"
#include "EventIntf.h"
#include "StorageIntf.h"
#include "ncbind.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("httpserv.dll")

namespace {

constexpr std::size_t kMaxHeaderBytes = 64 * 1024;
constexpr std::size_t kMaxRequestBodyBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxResponseBodyBytes = 64 * 1024 * 1024;

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
int socketError() { return WSAGetLastError(); }
void closeSocket(SocketHandle socket) {
    if(socket != kInvalidSocket)
        closesocket(socket);
}
void shutdownSocket(SocketHandle socket) {
    if(socket != kInvalidSocket)
        shutdown(socket, SD_BOTH);
}
bool isInterruptedSocketError(int error) {
    return error == WSAEINTR || error == WSAEWOULDBLOCK;
}
void setSocketReadTimeout(SocketHandle socket, int milliseconds) {
    const DWORD timeout = static_cast<DWORD>(std::max(1, milliseconds));
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char *>(&timeout), sizeof(timeout));
}
void ensureSocketRuntime() {
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
int socketError() { return errno; }
void closeSocket(SocketHandle socket) {
    if(socket != kInvalidSocket)
        ::close(socket);
}
void shutdownSocket(SocketHandle socket) {
    if(socket != kInvalidSocket)
        ::shutdown(socket, SHUT_RDWR);
}
bool isInterruptedSocketError(int error) {
    return error == EINTR || error == EAGAIN || error == EWOULDBLOCK;
}
void setSocketReadTimeout(SocketHandle socket, int milliseconds) {
    const int bounded = std::max(1, milliseconds);
    timeval timeout{};
    timeout.tv_sec = bounded / 1000;
    timeout.tv_usec = (bounded % 1000) * 1000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}
void ensureSocketRuntime() {}
#endif

std::string toUtf8(const ttstr &value) {
    const tjs_int length = TVPWideCharToUtf8String(value.c_str(), nullptr);
    if(length <= 0)
        return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    if(TVPWideCharToUtf8String(value.c_str(), result.data()) < 0)
        return {};
    return result;
}

ttstr fromUtf8(const std::string &value) {
    if(value.empty())
        return ttstr();
    const tjs_int length = TVPUtf8ToWideCharString(value.data(),
                                                   static_cast<tjs_uint>(
                                                       std::min<std::size_t>(
                                                           value.size(),
                                                           std::numeric_limits<tjs_uint>::max())),
                                                   nullptr);
    if(length <= 0)
        return ttstr();
    std::vector<tjs_char> buffer(static_cast<std::size_t>(length) + 1, 0);
    TVPUtf8ToWideCharString(value.data(), static_cast<tjs_uint>(
                                               std::min<std::size_t>(
                                                   value.size(),
                                                   std::numeric_limits<tjs_uint>::max())),
                            buffer.data());
    return ttstr(buffer.data(), length);
}

std::string encodeText(const ttstr &text, tjs_int codepage) {
    // UTF-8 is the native portable code page.  For legacy code pages, use the
    // engine's narrow conversion when the host locale provides one; if that
    // conversion cannot represent the text, retaining UTF-8 is safer than
    // truncating or emitting an invalid response.
    if(codepage == 65001 || codepage == 0)
        return toUtf8(text);
    const tjs_int length = text.GetNarrowStrLen();
    if(length >= 0) {
        std::string result(static_cast<std::size_t>(length), '\0');
        if(length > 0)
            text.ToNarrowStr(result.data(), length);
        return result;
    }
    return toUtf8(text);
}

void setDict(iTJSDispatch2 *dict, const tjs_char *name,
             const tTJSVariant &value) {
    if(dict && name)
        dict->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, dict);
}

bool getProperty(iTJSDispatch2 *object, const tjs_char *name,
                 tTJSVariant &value) {
    if(!object || !name)
        return false;
    return TJS_SUCCEEDED(object->PropGet(TJS_IGNOREPROP, name, nullptr,
                                         &value, object)) &&
        value.Type() != tvtVoid;
}

std::string trimAscii(std::string value) {
    const auto isSpace = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while(!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while(!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

std::string lowerAscii(std::string value) {
    for(char &c : value) {
        if(c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

int hexDigit(char c) {
    if(c >= '0' && c <= '9')
        return c - '0';
    if(c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if(c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

std::string urlDecode(const std::string &value) {
    std::string result;
    result.reserve(value.size());
    for(std::size_t i = 0; i < value.size(); ++i) {
        if(value[i] == '%' && i + 2 < value.size()) {
            const int high = hexDigit(value[i + 1]);
            const int low = hexDigit(value[i + 2]);
            if(high >= 0 && low >= 0) {
                result.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        result.push_back(value[i] == '+' ? ' ' : value[i]);
    }
    return result;
}

void parseForm(const std::string &encoded,
               std::map<std::string, std::string> &form) {
    std::size_t begin = 0;
    while(begin <= encoded.size()) {
        const std::size_t end = encoded.find('&', begin);
        const std::string item = encoded.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        const std::size_t equal = item.find('=');
        const std::string key = urlDecode(item.substr(0, equal));
        if(!key.empty()) {
            const std::string value = equal == std::string::npos
                ? std::string() : urlDecode(item.substr(equal + 1));
            form[key] = value;
        }
        if(end == std::string::npos)
            break;
        begin = end + 1;
    }
}

struct HttpRequestData {
    std::string method;
    std::string target;
    std::string path;
    std::string host;
    std::string client;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> form;
    std::string body;
};

struct HttpResponseData {
    int status = 500;
    std::string contentType;
    std::string location;
    std::vector<std::uint8_t> body;
};

struct PendingRequest {
    explicit PendingRequest(HttpRequestData value) : request(std::move(value)) {}

    HttpRequestData request;
    HttpResponseData response;
    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    bool cancelled = false;
};

bool sendAll(SocketHandle socket, const std::uint8_t *data, std::size_t size) {
    while(size > 0) {
#if defined(_WIN32)
        const int sent = send(socket, reinterpret_cast<const char *>(data),
                              static_cast<int>(std::min<std::size_t>(
                                  size, std::numeric_limits<int>::max())), 0);
#else
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        const ssize_t sent = send(socket, data, size, flags);
#endif
        if(sent <= 0)
            return false;
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

bool readRequest(SocketHandle socket, HttpRequestData &request) {
    std::string input;
    input.reserve(4096);
    std::size_t headerEnd = std::string::npos;
    std::size_t delimiterLength = 0;
    while(input.size() <= kMaxHeaderBytes) {
        const std::size_t crlf = input.find("\r\n\r\n");
        const std::size_t lf = input.find("\n\n");
        if(crlf != std::string::npos &&
           (lf == std::string::npos || crlf < lf)) {
            headerEnd = crlf;
            delimiterLength = 4;
            break;
        }
        if(lf != std::string::npos) {
            headerEnd = lf;
            delimiterLength = 2;
            break;
        }
        char buffer[4096];
#if defined(_WIN32)
        const int count = recv(socket, buffer, sizeof(buffer), 0);
#else
        const ssize_t count = recv(socket, buffer, sizeof(buffer), 0);
#endif
        if(count <= 0)
            return false;
        input.append(buffer, static_cast<std::size_t>(count));
    }
    if(headerEnd == std::string::npos || headerEnd > kMaxHeaderBytes)
        return false;

    const std::string headerBlock = input.substr(0, headerEnd);
    std::size_t lineBegin = 0;
    std::vector<std::string> lines;
    while(lineBegin <= headerBlock.size()) {
        const std::size_t lineEnd = headerBlock.find('\n', lineBegin);
        std::string line = headerBlock.substr(
            lineBegin, lineEnd == std::string::npos ? std::string::npos
                                                     : lineEnd - lineBegin);
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        if(lineEnd == std::string::npos)
            break;
        lineBegin = lineEnd + 1;
    }
    if(lines.empty())
        return false;
    const std::size_t firstSpace = lines[0].find(' ');
    const std::size_t secondSpace = firstSpace == std::string::npos
        ? std::string::npos : lines[0].find(' ', firstSpace + 1);
    if(firstSpace == std::string::npos || secondSpace == std::string::npos)
        return false;
    request.method = lines[0].substr(0, firstSpace);
    request.target = lines[0].substr(firstSpace + 1,
                                     secondSpace - firstSpace - 1);
    if(request.method.empty() || request.target.empty() ||
       request.target.size() > 8192)
        return false;

    std::size_t contentLength = 0;
    for(std::size_t i = 1; i < lines.size(); ++i) {
        const std::size_t colon = lines[i].find(':');
        if(colon == std::string::npos)
            continue;
        const std::string name = trimAscii(lines[i].substr(0, colon));
        const std::string value = trimAscii(lines[i].substr(colon + 1));
        if(name.empty())
            continue;
        request.headers[name] = value;
        const std::string lowered = lowerAscii(name);
        if(lowered == "content-length") {
            char *end = nullptr;
            errno = 0;
            const unsigned long long parsed =
                std::strtoull(value.c_str(), &end, 10);
            if(errno != 0 || !end || *end != '\0' ||
               parsed > kMaxRequestBodyBytes)
                return false;
            contentLength = static_cast<std::size_t>(parsed);
        } else if(lowered == "transfer-encoding" &&
                  lowerAscii(value) != "identity") {
            // Chunked decoding belongs to the full POCO server; rejecting it
            // explicitly avoids handing an encoded body to a script callback.
            return false;
        }
    }

    const std::size_t bodyStart = headerEnd + delimiterLength;
    while(input.size() < bodyStart + contentLength) {
        char buffer[4096];
#if defined(_WIN32)
        const int count = recv(socket, buffer, sizeof(buffer), 0);
#else
        const ssize_t count = recv(socket, buffer, sizeof(buffer), 0);
#endif
        if(count <= 0)
            return false;
        input.append(buffer, static_cast<std::size_t>(count));
        if(input.size() > bodyStart + contentLength)
            break;
    }
    if(input.size() < bodyStart + contentLength)
        return false;
    request.body.assign(input.data() + bodyStart, contentLength);

    const std::size_t query = request.target.find('?');
    const std::string rawPath = query == std::string::npos
        ? request.target : request.target.substr(0, query);
    request.path = urlDecode(rawPath.empty() ? "/" : rawPath);
    if(query != std::string::npos)
        parseForm(request.target.substr(query + 1), request.form);
    std::string contentType;
    for(const auto &header : request.headers) {
        if(lowerAscii(header.first) == "content-type") {
            contentType = lowerAscii(header.second);
            break;
        }
    }
    if(contentType.find("application/x-www-form-urlencoded") !=
       std::string::npos)
        parseForm(request.body, request.form);
    const auto host = std::find_if(request.headers.begin(), request.headers.end(),
        [](const auto &item) { return lowerAscii(item.first) == "host"; });
    if(host != request.headers.end())
        request.host = host->second;
    return true;
}

tTJSVariant makeStringDictionary(
    const std::map<std::string, std::string> &values) {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return tTJSVariant();
    for(const auto &pair : values)
        setDict(dict, fromUtf8(pair.first).c_str(),
                tTJSVariant(fromUtf8(pair.second)));
    tTJSVariant result(dict, dict);
    dict->Release();
    return result;
}

tTJSVariant makeRequestVariant(const HttpRequestData &request) {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return tTJSVariant();
    setDict(dict, TJS_W("method"), tTJSVariant(fromUtf8(request.method)));
    setDict(dict, TJS_W("request"), tTJSVariant(fromUtf8(request.target)));
    setDict(dict, TJS_W("path"), tTJSVariant(fromUtf8(request.path)));
    setDict(dict, TJS_W("host"), tTJSVariant(fromUtf8(request.host)));
    setDict(dict, TJS_W("client"), tTJSVariant(fromUtf8(request.client)));
    setDict(dict, TJS_W("header"), makeStringDictionary(request.headers));
    setDict(dict, TJS_W("form"), makeStringDictionary(request.form));
    tTJSVariant result(dict, dict);
    dict->Release();
    return result;
}

bool readStorage(const ttstr &name, std::vector<std::uint8_t> &bytes) {
    bytes.clear();
    try {
        std::unique_ptr<tTJSBinaryStream> stream(
            TVPCreateStream(name, TJS_BS_READ));
        if(!stream)
            return false;
        const tjs_uint64 size = stream->GetSize();
        if(size > kMaxResponseBodyBytes)
            return false;
        bytes.resize(static_cast<std::size_t>(size));
        std::size_t offset = 0;
        while(offset < bytes.size()) {
            const tjs_uint count = static_cast<tjs_uint>(std::min<std::size_t>(
                bytes.size() - offset, std::numeric_limits<tjs_uint>::max()));
            stream->ReadBuffer(bytes.data() + offset, count);
            offset += count;
        }
        return true;
    } catch(...) {
        return false;
    }
}

bool variantToBytes(const tTJSVariant &value,
                    std::vector<std::uint8_t> &bytes) {
    bytes.clear();
    if(value.Type() == tvtOctet) {
        const tTJSVariantOctet *octet = value.AsOctetNoAddRef();
        if(!octet || octet->GetLength() > kMaxResponseBodyBytes)
            return false;
        if(octet->GetLength() > 0 && octet->GetData())
            bytes.assign(octet->GetData(),
                         octet->GetData() + octet->GetLength());
        return true;
    }
    if(value.Type() != tvtObject || !value.AsObjectNoAddRef())
        return false;
    iTJSDispatch2 *array = value.AsObjectNoAddRef();
    tTJSVariant lengthValue;
    if(!getProperty(array, TJS_W("length"), lengthValue))
        return false;
    const tjs_int64 length = static_cast<tjs_int64>(lengthValue);
    if(length < 0 || static_cast<std::uint64_t>(length) > kMaxResponseBodyBytes)
        return false;
    bytes.reserve(static_cast<std::size_t>(length));
    for(tjs_int64 index = 0; index < length; ++index) {
        tTJSVariant item;
        if(TJS_FAILED(array->PropGetByNum(TJS_IGNOREPROP,
                                          static_cast<tjs_int>(index), &item,
                                          array)))
            return false;
        const tjs_int64 number = static_cast<tjs_int64>(item);
        if(number < 0 || number > 255)
            return false;
        bytes.push_back(static_cast<std::uint8_t>(number));
    }
    return true;
}

int variantInt(iTJSDispatch2 *object, const tjs_char *name, int fallback) {
    tTJSVariant value;
    if(!getProperty(object, name, value))
        return fallback;
    try {
        return static_cast<int>(value);
    } catch(...) {
        return fallback;
    }
}

std::string variantString(iTJSDispatch2 *object, const tjs_char *name) {
    tTJSVariant value;
    if(!getProperty(object, name, value))
        return {};
    return toUtf8(ttstr(value));
}

HttpResponseData makeErrorResponse(int status, const std::string &message) {
    HttpResponseData response;
    response.status = status;
    response.contentType = "text/plain; charset=utf-8";
    response.body.assign(message.begin(), message.end());
    return response;
}

const char *reasonForStatus(int status) {
    switch(status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    default: return "HTTP Response";
    }
}

class PortableHTTPServerCompat {
public:
    static tjs_error TJS_INTF_METHOD factory(PortableHTTPServerCompat **result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *objthis) {
        if(!result)
            return TJS_E_FAIL;
        auto *server = new PortableHTTPServerCompat(objthis);
        if(numparams > 0 && param && param[0])
            server->port_ = std::clamp(static_cast<int>(*param[0]), 0, 65535);
        if(numparams > 1 && param && param[1])
            server->timeout_ = std::max(0, static_cast<int>(*param[1]));
        if(numparams > 2 && param && param[2])
            server->codepage_ = static_cast<int>(*param[2]);
        *result = server;
        return TJS_S_OK;
    }

    explicit PortableHTTPServerCompat(iTJSDispatch2 *owner) : owner_(owner) {
        if(owner_)
            owner_->AddRef();
    }

    ~PortableHTTPServerCompat() {
        stop();
        if(owner_) {
            TVPCancelSourceEvents(owner_);
            owner_->Release();
            owner_ = nullptr;
        }
    }

    int start() {
        if(running_.load(std::memory_order_acquire))
            return port_;
        ensureSocketRuntime();
        SocketHandle listener = socket(AF_INET, SOCK_STREAM, 0);
        if(listener == kInvalidSocket) {
            log(TJS_W("could not create loopback socket"));
            return 0;
        }
        int reuse = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&reuse), sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<std::uint16_t>(port_));
        if(bind(listener, reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) != 0 || listen(listener, 8) != 0) {
            closeSocket(listener);
            log(TJS_W("could not bind/listen on loopback socket"));
            return 0;
        }
        sockaddr_in bound{};
#if defined(_WIN32)
        int boundLength = sizeof(bound);
#else
        socklen_t boundLength = sizeof(bound);
#endif
        if(getsockname(listener, reinterpret_cast<sockaddr *>(&bound),
                       &boundLength) != 0) {
            closeSocket(listener);
            return 0;
        }
        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            listener_ = listener;
        }
        port_ = ntohs(bound.sin_port);
        stopping_.store(false, std::memory_order_release);
        running_.store(true, std::memory_order_release);
        worker_ = std::thread(&PortableHTTPServerCompat::acceptLoop, this);
        return port_;
    }

    bool stop() {
        stopping_.store(true, std::memory_order_release);
        SocketHandle listener = kInvalidSocket;
        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            listener = listener_;
            listener_ = kInvalidSocket;
        }
        shutdownSocket(listener);
        closeSocket(listener);
        // Wake clients waiting for a script callback before joining the
        // accept thread. This keeps stop() bounded by an in-flight callback,
        // not by the configured HTTP timeout.
        cancelPending();
        if(worker_.joinable() &&
           worker_.get_id() != std::this_thread::get_id())
            worker_.join();
        running_.store(false, std::memory_order_release);
        return true;
    }

    int getPort() const { return port_; }
    int getTimeout() const { return timeout_; }
    int getCodePage() const { return codepage_; }
    void setCodePage(int value) { codepage_ = value; }
    bool getStarted() const {
        return running_.load(std::memory_order_acquire);
    }

    static tjs_error TJS_INTF_METHOD dispatchCb(tTJSVariant *, tjs_int,
                                                tTJSVariant **,
                                                PortableHTTPServerCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        self->dispatchPending();
        return TJS_S_OK;
    }

private:
    void log(const tjs_char *message) const {
        TVPAddLog(ttstr(TJS_W("AetherKiri httpserv: ")) + message);
    }

    void acceptLoop() {
        while(!stopping_.load(std::memory_order_acquire)) {
            SocketHandle listener;
            {
                std::lock_guard<std::mutex> lock(socketMutex_);
                listener = listener_;
            }
            if(listener == kInvalidSocket)
                break;
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(listener, &readSet);
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 250000;
            const int ready = select(static_cast<int>(listener) + 1,
                                     &readSet, nullptr, nullptr, &timeout);
            if(ready < 0) {
                const int error = socketError();
                if(isInterruptedSocketError(error))
                    continue;
                break;
            }
            if(ready == 0 || !FD_ISSET(listener, &readSet))
                continue;
            sockaddr_in peer{};
#if defined(_WIN32)
            int peerLength = sizeof(peer);
#else
            socklen_t peerLength = sizeof(peer);
#endif
            SocketHandle client = accept(
                listener, reinterpret_cast<sockaddr *>(&peer), &peerLength);
            if(client == kInvalidSocket) {
                if(stopping_.load(std::memory_order_acquire))
                    break;
                continue;
            }
            // A stalled client must not keep stop()/destruction blocked in
            // recv forever. The callback timeout is separate; this transport
            // timeout only bounds header/body acquisition.
            setSocketReadTimeout(client, 1000);
            char addressBuffer[INET6_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &peer.sin_addr, addressBuffer,
                      sizeof(addressBuffer));
            handleClient(client, addressBuffer);
        }
    }

    void handleClient(SocketHandle client, const char *peer) {
        struct SocketGuard {
            SocketHandle socket;
            ~SocketGuard() {
                shutdownSocket(socket);
                closeSocket(socket);
            }
        } guard{client};
        HttpRequestData request;
        request.client = peer ? peer : "";
        if(!readRequest(client, request)) {
            const HttpResponseData response = makeErrorResponse(400,
                                                                "bad request\n");
            sendResponse(client, response, false);
            return;
        }
        const bool head = lowerAscii(request.method) == "head";
        HttpResponseData response;
        if(!enqueueAndWait(std::move(request), response))
            response = makeErrorResponse(504, "request callback timed out\n");
        sendResponse(client, response, head);
    }

    bool enqueueAndWait(HttpRequestData request, HttpResponseData &response) {
        if(!owner_ || stopping_.load(std::memory_order_acquire))
            return false;
        auto pending = std::make_shared<PendingRequest>(std::move(request));
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if(stopping_.load(std::memory_order_acquire) || !owner_)
                return false;
            queue_.push_back(pending);
        }
        tTJSVariant token(static_cast<tjs_int64>(
            reinterpret_cast<std::uintptr_t>(pending.get())));
        // The event name must match the native method registered on the
        // SimpleHTTPServer object.  Posting a private/non-existent member
        // leaves the request queued until the timeout on hosts that do not
        // install an extra script shim (the portable host is one such host).
        static ttstr eventName(TJS_W("dispatchPending"));
        if(owner_)
            TVPPostEvent(owner_, owner_, eventName, 0, TVP_EPT_POST, 1, &token);

        std::unique_lock<std::mutex> lock(pending->mutex);
        const bool completed = timeout_ <= 0
            ? (pending->condition.wait(lock,
                                       [&] { return pending->completed; }), true)
            : pending->condition.wait_for(
                  lock, std::chrono::seconds(timeout_),
                  [&] { return pending->completed; });
        if(!completed) {
            pending->cancelled = true;
            return false;
        }
        response = pending->response;
        return true;
    }

    void dispatchPending() {
        while(true) {
            std::shared_ptr<PendingRequest> pending;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if(queue_.empty())
                    break;
                pending = queue_.front();
                queue_.pop_front();
            }
            {
                std::lock_guard<std::mutex> lock(pending->mutex);
                if(pending->cancelled || pending->completed) {
                    pending->completed = true;
                    pending->condition.notify_all();
                    continue;
                }
            }
            HttpResponseData response = invokeRequest(pending->request);
            {
                std::lock_guard<std::mutex> lock(pending->mutex);
                if(!pending->cancelled)
                    pending->response = std::move(response);
                pending->completed = true;
            }
            pending->condition.notify_all();
        }
    }

    HttpResponseData invokeRequest(const HttpRequestData &request) {
        if(!owner_)
            return makeErrorResponse(503, "server owner is unavailable\n");
        tTJSVariant requestValue = makeRequestVariant(request);
        tTJSVariant *args[] = {&requestValue};
        tTJSVariant result;
        try {
            const tjs_error error = owner_->FuncCall(
                0, TJS_W("onRequest"), nullptr, &result, 1, args, owner_);
            if(TJS_FAILED(error) || result.Type() != tvtObject ||
               !result.AsObjectNoAddRef())
                return makeErrorResponse(500,
                                         "onRequest did not return a response\n");
            iTJSDispatch2 *responseObject = result.AsObjectNoAddRef();
            HttpResponseData response;
            response.status = std::clamp(
                variantInt(responseObject, TJS_W("status"), 200), 100, 599);
            response.contentType = variantString(responseObject,
                                                  TJS_W("content_type"));
            response.location = variantString(responseObject,
                                               TJS_W("redirect"));
            tTJSVariant value;
            if(getProperty(responseObject, TJS_W("text"), value)) {
                const std::string text = encodeText(ttstr(value), codepage_);
                response.body.assign(text.begin(), text.end());
                if(response.contentType.empty())
                    response.contentType = "text/plain; charset=utf-8";
            } else if(getProperty(responseObject, TJS_W("file"), value)) {
                if(!readStorage(ttstr(value), response.body))
                    return makeErrorResponse(404, "response file not found\n");
                if(response.contentType.empty())
                    response.contentType = "application/octet-stream";
            } else if(getProperty(responseObject, TJS_W("binary"), value)) {
                if(!variantToBytes(value, response.body))
                    return makeErrorResponse(501, "invalid binary response\n");
                if(response.contentType.empty())
                    response.contentType = "application/octet-stream";
            } else if(response.location.empty()) {
                const std::string type = variantString(responseObject,
                                                        TJS_W("error_type"));
                const std::string description = variantString(
                    responseObject, TJS_W("error_desc"));
                response.status = response.status >= 200 &&
                        response.status < 300 ? 500 : response.status;
                const std::string message = type.empty() && description.empty()
                    ? "no response body\n" : type + ": " + description + "\n";
                response.contentType = "text/plain; charset=utf-8";
                response.body.assign(message.begin(), message.end());
            }
            if(!response.location.empty() && response.status != 301 &&
               response.status != 302 && response.status != 307 &&
               response.status != 308)
                response.status = 301;
            if(response.body.size() > kMaxResponseBodyBytes)
                return makeErrorResponse(413, "response body is too large\n");
            return response;
        } catch(const eTJS &exception) {
            TVPAddLog(ttstr(TJS_W("AetherKiri httpserv callback error: ")) +
                      exception.GetMessage());
            return makeErrorResponse(500, "onRequest raised an exception\n");
        } catch(...) {
            TVPAddLog(TJS_W("AetherKiri httpserv callback error: unknown"));
            return makeErrorResponse(500, "onRequest raised an exception\n");
        }
    }

    void sendResponse(SocketHandle client, const HttpResponseData &response,
                      bool head) {
        const bool statusHasNoBody = (response.status >= 100 &&
                                      response.status < 200) ||
            response.status == 204 || response.status == 304;
        const std::size_t contentLength = statusHasNoBody
            ? 0 : response.body.size();
        std::string header = "HTTP/1.1 " + std::to_string(response.status) +
            " " + reasonForStatus(response.status) + "\r\n";
        if(!response.contentType.empty())
            header += "Content-Type: " + response.contentType + "\r\n";
        if(!response.location.empty())
            header += "Location: " + response.location + "\r\n";
        header += "Content-Length: " + std::to_string(contentLength) +
            "\r\nConnection: close\r\n\r\n";
        sendAll(client, reinterpret_cast<const std::uint8_t *>(header.data()),
                header.size());
        if(!head && !statusHasNoBody && !response.body.empty())
            sendAll(client, response.body.data(), response.body.size());
    }

    void cancelPending() {
        std::deque<std::shared_ptr<PendingRequest>> pending;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            pending.swap(queue_);
        }
        for(const auto &item : pending) {
            std::lock_guard<std::mutex> lock(item->mutex);
            item->cancelled = true;
            item->completed = true;
            item->condition.notify_all();
        }
    }

    iTJSDispatch2 *owner_ = nullptr;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::mutex socketMutex_;
    SocketHandle listener_ = kInvalidSocket;
    std::mutex queueMutex_;
    std::deque<std::shared_ptr<PendingRequest>> queue_;
    int port_ = 0;
    int timeout_ = 10;
    int codepage_ = 65001;
};

} // namespace

NCB_REGISTER_CLASS_DIFFER(SimpleHTTPServer, PortableHTTPServerCompat) {
    Factory(&PortableHTTPServerCompat::factory);
    NCB_PROPERTY_RO(port, PortableHTTPServerCompat::getPort);
    NCB_PROPERTY_RO(timeout, PortableHTTPServerCompat::getTimeout);
    NCB_PROPERTY(codepage, PortableHTTPServerCompat::getCodePage,
                 PortableHTTPServerCompat::setCodePage);
    NCB_PROPERTY_RO(started, PortableHTTPServerCompat::getStarted);
    NCB_METHOD(start);
    NCB_METHOD(stop);
    NCB_METHOD_RAW_CALLBACK(dispatchPending,
                            &PortableHTTPServerCompat::dispatchCb, 0);
    Variant(TJS_W("cpACP"), static_cast<tjs_int>(0));
    Variant(TJS_W("cpOEM"), static_cast<tjs_int>(1));
    Variant(TJS_W("cpUTF8"), static_cast<tjs_int>(65001));
    Variant(TJS_W("cpSJIS"), static_cast<tjs_int>(932));
    Variant(TJS_W("cpEUC"), static_cast<tjs_int>(20932));
    Variant(TJS_W("cpJIS"), static_cast<tjs_int>(50220));
}

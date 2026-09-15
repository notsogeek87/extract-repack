#include "lzma_decoder.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#ifdef ARCEXTRACT_WITH_LZMA
extern "C" {
#include "LzmaDec.h"
}
#endif

namespace arcextract {

namespace {

// parseMem() in the FreeArc sources: a number with an optional unit suffix.
uint64_t parseMem(const std::string& text, bool& ok) {
    size_t i = 0;
    uint64_t value = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + static_cast<uint64_t>(text[i] - '0');
        ++i;
    }
    if (i == 0) {
        ok = false;
        return 0;
    }
    uint64_t multiplier = 1;
    if (i < text.size()) {
        switch (text[i]) {
            case 'b': case 'B': multiplier = 1; break;
            case 'k': case 'K': multiplier = 1024ull; break;
            case 'm': case 'M': multiplier = 1024ull * 1024; break;
            case 'g': case 'G': multiplier = 1024ull * 1024 * 1024; break;
            default: ok = false; return 0;
        }
        ++i;
    }
    ok = i == text.size();
    return value * multiplier;
}

bool startsWith(const std::string& text, const char* prefix) {
    const size_t n = std::strlen(prefix);
    return text.size() >= n && text.compare(0, n, prefix) == 0;
}

int parseIntOr(const std::string& text, int fallback) {
    bool ok = true;
    const uint64_t value = parseMem(text, ok);
    return ok ? static_cast<int>(value) : fallback;
}

} // namespace

LzmaParams parseLzmaMethod(const std::string& method) {
    LzmaParams params;
    size_t start = 0;
    bool first = true;
    while (start <= method.size()) {
        const size_t colon = method.find(':', start);
        std::string token = method.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (!token.empty() && token[0] == '*') {
            token.erase(0, 1); // an optional parameter, same value either way
        }
        if (first) {
            first = false; // the method name itself ("lzma")
        } else if (startsWith(token, "lc")) {
            params.litContextBits = parseIntOr(token.substr(2), params.litContextBits);
        } else if (startsWith(token, "lp")) {
            params.litPosBits = parseIntOr(token.substr(2), params.litPosBits);
        } else if (startsWith(token, "pb")) {
            params.posStateBits = parseIntOr(token.substr(2), params.posStateBits);
        } else if (startsWith(token, "d")) {
            bool ok = true;
            const uint64_t size = parseMem(token.substr(1), ok);
            if (ok && size > 0) params.dictSize = static_cast<uint32_t>(std::min<uint64_t>(size, 0xFFFFFFFFull));
        } else if (!token.empty() && token[0] >= '0' && token[0] <= '9') {
            // An unnamed parameter: a bare size is taken as the dictionary.
            bool ok = true;
            const uint64_t size = parseMem(token, ok);
            if (ok && size > 0) params.dictSize = static_cast<uint32_t>(std::min<uint64_t>(size, 0xFFFFFFFFull));
        }
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return params;
}

#ifdef ARCEXTRACT_WITH_LZMA

namespace {

void* szAlloc(void*, size_t size) { return size == 0 ? nullptr : std::malloc(size); }
void szFree(void*, void* address) { std::free(address); }
ISzAlloc g_alloc = {szAlloc, szFree};

} // namespace

bool isLzmaSupported() { return true; }

bool lzmaDecode(const std::string& method, const std::vector<uint8_t>& in, uint64_t origSize,
                std::vector<uint8_t>& out) {
    const LzmaParams params = parseLzmaMethod(method);
    if (params.litContextBits < 0 || params.litContextBits > 8 || params.litPosBits < 0 || params.litPosBits > 4 ||
        params.posStateBits < 0 || params.posStateBits > 4) {
        return false;
    }

    // Standard 5-byte LZMA props blob, rebuilt from the method string so the
    // stock LzmaDec_AllocateProbs can be used: one packed byte followed by the
    // dictionary size, little-endian.
    Byte props[5];
    props[0] = static_cast<Byte>((params.posStateBits * 5 + params.litPosBits) * 9 + params.litContextBits);
    for (int i = 0; i < 4; ++i) {
        props[1 + i] = static_cast<Byte>((params.dictSize >> (8 * i)) & 0xFF);
    }

    CLzmaDec state;
    LzmaDec_Construct(&state);
    if (LzmaDec_AllocateProbs(&state, props, sizeof(props), &g_alloc) != SZ_OK) {
        return false;
    }
    LzmaDec_Init(&state);

    out.assign(static_cast<size_t>(origSize), 0);
    // Memory-to-memory, exactly as lzma_decompress2() does it: point the
    // decoder's window straight at the output buffer, then detach it before
    // freeing so the decoder never owns our memory.
    state.dic = out.empty() ? nullptr : out.data();
    state.dicBufSize = static_cast<SizeT>(out.size());

    SizeT inSize = static_cast<SizeT>(in.size());
    ELzmaStatus status;
    const SRes res = LzmaDec_DecodeToDic(&state, static_cast<SizeT>(out.size()), in.data(), &inSize,
                                         LZMA_FINISH_ANY, &status);
    const bool complete = res == SZ_OK && state.dicPos == out.size();

    state.dic = nullptr;
    LzmaDec_FreeProbs(&state, &g_alloc);
    if (!complete) {
        out.clear();
    }
    return complete;
}

#else // ARCEXTRACT_WITH_LZMA

bool isLzmaSupported() { return false; }

bool lzmaDecode(const std::string&, const std::vector<uint8_t>&, uint64_t, std::vector<uint8_t>&) {
    return false;
}

#endif // ARCEXTRACT_WITH_LZMA

} // namespace arcextract

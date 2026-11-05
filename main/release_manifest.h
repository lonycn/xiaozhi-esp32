#pragma once
#include <cstdint>
#include <string>

struct ReleaseManifest {
    std::string release_id, version, url, sha256, board_type, board_name, chip, update_policy;
    std::string assets_version, assets_sha256;
    size_t size = 0, assets_size = 0;
    uint32_t min_flash_size = 0;
    bool assets_required = false, development_only = false, valid = false;
};
namespace release_manifest {
inline bool Sha256(const std::string& s) { return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos; }
inline bool Decimal(const std::string& s, unsigned& value) {
    if (s.empty() || s.size() > 5 || s.find_first_not_of("0123456789") != std::string::npos) return false;
    value = 0; for (char c : s) value = value * 10 + unsigned(c - '0'); return true;
}
inline bool LanLiteral(const std::string& host) {
    if (host == "[::1]") return true;
    unsigned octets[4]{}; size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        auto end = host.find('.', start); auto part = host.substr(start, end == std::string::npos ? end : end - start);
        if ((part.size() > 1 && part[0] == '0') || !Decimal(part, octets[i]) || octets[i] > 255 ||
            ((i == 3) != (end == std::string::npos))) return false;
        start = end + 1;
    }
    return octets[0] == 10 || octets[0] == 127 || (octets[0] == 192 && octets[1] == 168) ||
           (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31);
}
inline std::string Origin(const std::string& url) {
    if (url.size() > 2048 || url.find_first_of("\r\n\t #\\") != std::string::npos) return {};
    auto delimiter = url.find("://"); if (delimiter == std::string::npos) return {};
    auto scheme = url.substr(0, delimiter);
    if (scheme != "http" && scheme != "https" && scheme != "ws" && scheme != "wss") return {};
    auto start = delimiter + 3, end = url.find_first_of("/?", start); auto authority = url.substr(start, end == std::string::npos ? end : end - start);
    if (authority.empty() || authority.find('@') != std::string::npos || authority.find('%') != std::string::npos) return {};
    auto port = authority.rfind(':');
    if (authority[0] == '[') { auto close = authority.find(']'); if (close == std::string::npos) return {}; port = close + 1 < authority.size() ? close + 1 : std::string::npos; }
    if (port != std::string::npos) { unsigned number; if (authority[port] != ':' || !Decimal(authority.substr(port + 1), number) || number < 1 || number > 65535) return {}; }
    return scheme + "://" + authority;
}
inline bool AllowedUrl(const std::string& url, bool lan = false, bool websocket = false) {
    auto origin = Origin(url); if (origin.empty()) return false;
    const auto secure = websocket ? "wss://" : "https://";
    if (origin.rfind(secure, 0) == 0) return true;
    const std::string plain = websocket ? "ws://" : "http://";
    if (!lan || origin.rfind(plain, 0) != 0) return false;
    auto host = origin.substr(plain.size());
    if (!host.empty() && host[0] == '[') { auto close = host.find(']'); host = host.substr(0, close + 1); }
    else host = host.substr(0, host.find(':'));
    return LanLiteral(host);
}
inline std::string TrustedOtaRoot(const std::string& compiled, const std::string& stored, bool controlled, bool lan) {
    if (controlled) return AllowedUrl(compiled, lan) ? compiled : "";
    return stored.empty() ? compiled : stored;
}
inline bool Https(const std::string& url) { return AllowedUrl(url); }
inline bool SameOrigin(const std::string& a, const std::string& b) {
    const auto origin = Origin(a); return !origin.empty() && origin == Origin(b);
}
inline bool SameArtifactUrl(const std::string& a, const std::string& b, bool lan = false) {
    return AllowedUrl(a, lan) && AllowedUrl(b, lan) &&
           a.substr(0, a.find('?')) == b.substr(0, b.find('?'));
}
inline bool Compatible(const ReleaseManifest& m, const char* type, const char* name, const char* chip, uint32_t flash, bool lan = false) {
    return m.valid && m.board_type == type && m.board_name == name && m.chip == chip &&
        m.min_flash_size > 0 && m.min_flash_size <= flash && m.size > 256 && m.size <= flash &&
        m.assets_size > 0 && !m.assets_required && (!m.development_only || lan) && Sha256(m.sha256) && Sha256(m.assets_sha256) &&
        AllowedUrl(m.url, lan) && (m.update_policy == "automatic" || m.update_policy == "manual");
}
}

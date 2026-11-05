#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace development_identity {
inline int Hex(char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

inline bool ParseMac(std::string_view text, std::array<uint8_t, 6>& output) {
    if (text.size() != 17)
        return false;
    for (size_t i = 0; i < output.size(); ++i) {
        const int high = Hex(text[i * 3]);
        const int low = Hex(text[i * 3 + 1]);
        if (high < 0 || low < 0 || (i < 5 && text[i * 3 + 2] != ':'))
            return false;
        output[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

inline bool IsLocalUnicast(const std::array<uint8_t, 6>& mac) { return (mac[0] & 3) == 2; }

inline bool ValidUuid(std::string_view text) {
    if (text.size() != 36)
        return false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (text[i] != '-')
                return false;
        } else if (Hex(text[i]) < 0) {
            return false;
        }
    }
    return true;
}
enum class StoredIdentityDecision { Factory, Restore, Invalid };
inline StoredIdentityDecision DecideStoredIdentity(bool has_dev_mac, std::string_view mac, std::string_view uuid) {
    if (!has_dev_mac) return StoredIdentityDecision::Factory;
    std::array<uint8_t, 6> parsed{};
    if (!ParseMac(mac, parsed) || !IsLocalUnicast(parsed) || !ValidUuid(uuid) ||
        uuid == "00000000-0000-0000-0000-000000000000") return StoredIdentityDecision::Invalid;
    return StoredIdentityDecision::Restore;
}
}  // namespace development_identity

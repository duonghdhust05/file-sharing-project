#ifndef FSP_SHA256_H
#define FSP_SHA256_H

#include <string>

namespace FSP::Auth {

// SHA-256 (FIPS 180-4) of `msg`, returned as 64 lowercase hex chars.
std::string sha256Hex(const std::string& msg);

}  // namespace FSP::Auth

#endif  // FSP_SHA256_H

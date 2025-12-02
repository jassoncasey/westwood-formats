#pragma once

#include <westwood/error.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace wwd {

/// Blowfish cipher implementation for Westwood MIX file decryption.
///
/// This implements the standard Blowfish block cipher (Bruce Schneier, 1993)
/// with the specific byte-order handling required for C&C MIX files.
///
/// Key size: 56 bytes (448 bits)
/// Block size: 8 bytes (64 bits)
/// Mode: ECB (Electronic Codebook)
class Blowfish {
public:
    static constexpr size_t KEY_SIZE = 56;
    static constexpr size_t BLOCK_SIZE = 8;

    /// Initialize Blowfish with a 56-byte key.
    explicit Blowfish(std::span<const uint8_t, KEY_SIZE> key);

    /// Decrypt a single 8-byte block in place.
    /// Uses Westwood's byte-order convention (LE words, swapped for cipher).
    void decrypt_block(uint8_t* block) const;

    /// Encrypt a single 8-byte block in place.
    void encrypt_block(uint8_t* block) const;

    /// Decrypt data in ECB mode. Size must be multiple of 8.
    void decrypt(std::span<uint8_t> data) const;

    /// Encrypt data in ECB mode. Size must be multiple of 8.
    void encrypt(std::span<uint8_t> data) const;

private:
    void encipher(uint32_t& left, uint32_t& right) const;
    void decipher(uint32_t& left, uint32_t& right) const;
    uint32_t f(uint32_t x) const;

    std::array<uint32_t, 18> p_;      // P-array
    std::array<uint32_t, 256> s0_;    // S-box 0
    std::array<uint32_t, 256> s1_;    // S-box 1
    std::array<uint32_t, 256> s2_;    // S-box 2
    std::array<uint32_t, 256> s3_;    // S-box 3
};

/// Derive Blowfish key from RSA-encrypted key source in MIX files.
///
/// The 80-byte key_source at offset 4 in encrypted MIX files is
/// RSA-decrypted using Westwood's public key to produce a 56-byte
/// Blowfish key.
///
/// @param key_source 80 bytes of RSA-encrypted key material
/// @return 56-byte Blowfish key
WWD_API Result<std::array<uint8_t, 56>> derive_blowfish_key(
    std::span<const uint8_t, 80> key_source);

} // namespace wwd

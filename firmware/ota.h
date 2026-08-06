#pragma once

#include <Arduino.h>

// Streaming application-only OTA writer. Accepts a plain Arduino .bin via the
// local portal. Sanity comes from the Update class (ESP32 image magic/header
// checks) plus a SHA-256 of the received image reported back to the uploader
// so it can be compared against the published checksum. No signing: raw USB
// flashing is unrestricted, and the portal is already gated by the on-screen
// AP credentials.
class OtaSession {
 public:
  bool begin(size_t total_len, String& err);
  bool write(const uint8_t* data, size_t len, String& err);
  bool finish(String& err);
  void abort();

  bool inProgress() const { return started_; }
  size_t imageBytes() const { return image_len_; }
  const String& imageSha256() const { return sha256_hex_; }

 private:
  bool started_ = false;
  size_t expected_total_ = 0;
  size_t image_len_ = 0;
  size_t received_ = 0;
  void* sha_ctx_ = nullptr;
  String sha256_hex_;
};

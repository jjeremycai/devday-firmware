#pragma once

#include <Arduino.h>

// Streaming RSA-2048/SHA-256 verified OTA writer. Accepts an application-only
// image with the trailer defined in config.h. The image is only made bootable
// after the signature verifies against the embedded production public key.
class OtaSession {
 public:
  bool begin(size_t total_len, String& err);
  // Feed raw bytes as they arrive; the last OTA_TRAILER_LEN bytes are held back.
  bool write(const uint8_t* data, size_t len, String& err);
  // Verify trailer, digest, and signature; commit the update if all pass.
  bool finish(String& err);
  void abort();

  bool inProgress() const { return started_; }
  size_t imageBytes() const { return image_len_; }

 private:
  bool started_ = false;
  size_t expected_total_ = 0;
  size_t image_len_ = 0;
  size_t received_ = 0;
  uint8_t tail_[8 + 256]; // OTA_TRAILER_LEN
  size_t tail_len_ = 0;
  void* sha_ctx_ = nullptr;
};

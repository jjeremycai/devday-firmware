#include "ota.h"

#include <Update.h>
#include <mbedtls/sha256.h>

bool OtaSession::begin(size_t total_len, String& err) {
  abort();
  if (total_len == 0 || total_len > (3UL * 1024UL * 1024UL)) {
    err = "bad_length";
    return false;
  }
  // Pass the real length so Update tracks progress against it and end() below
  // can reject a short image. UPDATE_SIZE_UNKNOWN would make every truncated
  // upload look complete.
  if (!Update.begin(total_len)) {
    err = "ota_begin_failed";
    return false;
  }
  auto* ctx = new mbedtls_sha256_context();
  mbedtls_sha256_init(ctx);
  mbedtls_sha256_starts(ctx, 0);
  sha_ctx_ = ctx;
  expected_total_ = total_len;
  received_ = 0;
  image_len_ = 0;
  sha256_hex_ = "";
  started_ = true;
  return true;
}

bool OtaSession::write(const uint8_t* data, size_t len, String& err) {
  if (!started_) {
    err = "not_started";
    return false;
  }
  received_ += len;
  if (received_ > expected_total_) {
    err = "too_much_data";
    return false;
  }
  mbedtls_sha256_update((mbedtls_sha256_context*)sha_ctx_, data, len);
  if (Update.write(const_cast<uint8_t*>(data), len) != len) {
    err = "flash_write_failed";
    return false;
  }
  image_len_ += len;
  return true;
}

bool OtaSession::finish(String& err) {
  if (!started_) {
    err = "not_started";
    return false;
  }
  if (received_ != expected_total_ || image_len_ == 0) {
    err = "truncated";
    abort();
    return false;
  }

  uint8_t hash[32];
  mbedtls_sha256_finish((mbedtls_sha256_context*)sha_ctx_, hash);
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", hash[i]);
  sha256_hex_ = hex;

  // Strict end(): fails unless exactly expected_total_ bytes were flashed, on
  // top of the ESP32 image magic/header checks. Then commits the new boot slot.
  if (!Update.end()) {
    err = Update.getError() == UPDATE_ERROR_MAGIC_BYTE ? "bad_image" : "truncated";
    abort();
    return false;
  }

  auto* ctx = (mbedtls_sha256_context*)sha_ctx_;
  mbedtls_sha256_free(ctx);
  delete ctx;
  sha_ctx_ = nullptr;
  started_ = false;
  return true;
}

void OtaSession::abort() {
  if (started_) Update.abort();
  if (sha_ctx_) {
    auto* ctx = (mbedtls_sha256_context*)sha_ctx_;
    mbedtls_sha256_free(ctx);
    delete ctx;
    sha_ctx_ = nullptr;
  }
  started_ = false;
}

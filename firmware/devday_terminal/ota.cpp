#include "ota.h"

#include <Update.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include "config.h"
#include "ota_pubkey.h"

bool OtaSession::begin(size_t total_len, String& err) {
  abort();
  if (total_len <= OTA_TRAILER_LEN || total_len > (3UL * 1024UL * 1024UL)) {
    err = "bad_length";
    return false;
  }
  if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
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
  tail_len_ = 0;
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

  // Invariant: tail_ always holds the last OTA_TRAILER_LEN unflushed bytes.
  if (tail_len_ + len > OTA_TRAILER_LEN) {
    size_t flush_total = tail_len_ + len - OTA_TRAILER_LEN;
    size_t from_tail = tail_len_ < flush_total ? tail_len_ : flush_total;
    mbedtls_sha256_update((mbedtls_sha256_context*)sha_ctx_, tail_, from_tail);
    if (Update.write(tail_, from_tail) != from_tail) {
      err = "flash_write_failed";
      return false;
    }
    memmove(tail_, tail_ + from_tail, tail_len_ - from_tail);
    tail_len_ -= from_tail;
    image_len_ += from_tail;
    size_t from_data = flush_total - from_tail;
    if (from_data > 0) {
      mbedtls_sha256_update((mbedtls_sha256_context*)sha_ctx_, data, from_data);
      if (Update.write(const_cast<uint8_t*>(data), from_data) != from_data) {
        err = "flash_write_failed";
        return false;
      }
      data += from_data;
      len -= from_data;
      image_len_ += from_data;
    }
  }
  if (len > 0) {
    memcpy(tail_ + tail_len_, data, len);
    tail_len_ += len;
  }
  return true;
}

bool OtaSession::finish(String& err) {
  if (!started_) {
    err = "not_started";
    return false;
  }
  if (received_ != expected_total_ || tail_len_ != OTA_TRAILER_LEN) {
    err = "truncated";
    abort();
    return false;
  }

  uint32_t magic = tail_[0] | (tail_[1] << 8) | (tail_[2] << 16) | ((uint32_t)tail_[3] << 24);
  uint32_t img_len = tail_[4] | (tail_[5] << 8) | (tail_[6] << 16) | ((uint32_t)tail_[7] << 24);
  if (magic != OTA_MAGIC || img_len != image_len_) {
    err = "bad_trailer";
    abort();
    return false;
  }

  uint8_t hash[32];
  mbedtls_sha256_finish((mbedtls_sha256_context*)sha_ctx_, hash);

  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int rc = mbedtls_pk_parse_public_key(&pk, OTA_PUBKEY_DER, OTA_PUBKEY_DER_LEN);
  if (rc == 0) {
    rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), tail_ + 8, OTA_SIG_LEN);
  }
  mbedtls_pk_free(&pk);
  if (rc != 0) {
    err = "bad_signature";
    abort();
    return false;
  }

  if (!Update.end(true)) {
    err = "ota_commit_failed";
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
  tail_len_ = 0;
  started_ = false;
}

#include <cassert>
#include <string>

#include "config.h"
#include "content.h"

int main() {
  CardContent content;
  contentDefaults(content, "");

  std::string long_detail(300, 'x');
  String payload((std::string("{\"schema\":1,\"refresh_after_s\":999999,\"build\":{\"detail\":\"") +
                  long_detail + "\"}}")
                     .c_str());
  assert(contentParse(payload, content));
  assert(content.build_detail.length() == CONTENT_TEXT_MAX_BYTES);
  assert(content.refresh_after_s == CONTENT_REFRESH_MAX_S);

  CardContent utf8_content;
  contentDefaults(utf8_content, "");
  std::string utf8_name(255, 'a');
  utf8_name += "\xC2\xB0";
  String utf8_payload((std::string("{\"schema\":1,\"dash\":{\"name\":\"") + utf8_name + "\"}}")
                          .c_str());
  assert(contentParse(utf8_payload, utf8_content));
  assert(utf8_content.dash_name.length() == 255);
  assert(utf8_content.dash_name.charAt(254) == 'a');

  return 0;
}

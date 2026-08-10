#include <cassert>
#include <string>

#include "config.h"
#include "content.h"

int main() {
  CardContent content;
  contentDefaults(content, "");
  assert(content.agenda_count == 0);
  assert(content.header_date.length() == 0);

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

  CardContent calendar_content;
  contentDefaults(calendar_content, "");
  String calendar_payload(
      "{\"schema\":1,\"dash\":{\"calendar\":[-1,1,3,9],\"calendar_today\":2}}");
  assert(contentParse(calendar_payload, calendar_content));
  assert(calendar_content.dash_calendar_count == 4);
  assert(calendar_content.dash_calendar[0] == 0);
  assert(calendar_content.dash_calendar[3] == 4);
  assert(calendar_content.dash_calendar_today == 2);

  CardContent stats_content;
  contentDefaults(stats_content, "");
  String stats_payload(
      "{\"schema\":1,\"dash\":{"
      "\"peak_day\":\"2.7B\",\"longest_streak\":\"64D\","
      "\"seven_day_total\":\"3.1B\",\"longest_run\":\"34H45M\"}}"
  );
  assert(contentParse(stats_payload, stats_content));
  assert(stats_content.dash_peak_day == "2.7B");
  assert(stats_content.dash_longest_streak == "64D");
  assert(stats_content.dash_seven_day_total == "3.1B");
  assert(stats_content.dash_longest_run == "34H45M");

  CardContent legacy_content;
  contentDefaults(legacy_content, "");
  assert(contentParse(
      "{\"schema\":1,\"dash\":{\"insight_left\":\"PEAK DAY 2.7B | LONGEST STREAK 64D\"}}",
      legacy_content));
  assert(legacy_content.dash_insight_left.length() > 0);
  assert(legacy_content.dash_peak_day.length() == 0);

  CardContent portrait_content;
  contentDefaults(portrait_content, "");
  const std::string primary_hex(CardContent::PET_BYTES * 2, '0');
  const std::string alternate_hex(CardContent::PET_BYTES * 2, 'f');
  String animated_payload(
      (std::string("{\"schema\":1,\"dash\":{\"avatar_hex\":\"") +
       primary_hex + "\",\"avatar_alt_hex\":\"" + alternate_hex + "\"}}")
          .c_str());
  assert(contentParse(animated_payload, portrait_content));
  assert(portrait_content.dash_avatar_present);
  assert(portrait_content.dash_avatar_alt_present);

  String still_payload(
      (std::string("{\"schema\":1,\"dash\":{\"avatar_hex\":\"") +
       primary_hex + "\"}}")
          .c_str());
  assert(contentParse(still_payload, portrait_content));
  assert(portrait_content.dash_avatar_present);
  assert(!portrait_content.dash_avatar_alt_present);

  return 0;
}

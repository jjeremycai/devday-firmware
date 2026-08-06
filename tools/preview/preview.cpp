// Host-side renderer for the Dev Day terminal cards. Compiles the real
// firmware/cards.cpp against stub Arduino/TFT headers and
// rasterizes each page to tools/preview/out/<page>.pgm (converted to PNG
// by build.sh). For the browser version see build_wasm.sh.
//
//   tools/preview/build.sh && open tools/preview/out/

#include "TFT_eSPI.h"

#include "cards.h"
#include "sample_content.h"

int main(int argc, char** argv) {
  const char* out_dir = argc > 1 ? argv[1] : "tools/preview/out";

  RenderStatus st = sampleStatus();
  CardContent content = sampleContent();

  auto dump = [&](const char* card, const CardContent& c, const RenderStatus& s, const char* name) {
    renderCard(card, c, s);
    char path[256];
    snprintf(path, sizeof path, "%s/%s.pgm", out_dir, name);
    epaperDumpPgm(path);
    printf("wrote %s\n", path);
  };

  displayBegin();
  for (const char* p : {"dash", "weather", "agenda", "build", "yours", "splash"}) {
    dump(p, content, st, p);
  }

  // Usage empty state: no dash payload yet (what a fresh device shows on KEY1).
  {
    CardContent empty = content;
    empty.dash_present = false;
    empty.dash_name = "";
    dump("dash", empty, st, "dash_empty");
  }

  // No portrait pushed: the bundled pet stands in, so a fresh unit is never
  // faceless.
  {
    CardContent bundled = content;
    bundled.dash_avatar_present = false;
    dump("dash", bundled, st, "dash_bundled_pet");
  }

  // A terminal still paired with an older sync script receives the original
  // 72x72 square; it must render centred in the pet's box, not garbled.
  {
    CardContent square = content;
    memset(square.dash_avatar, 0, sizeof square.dash_avatar);
    for (size_t y = 0; y < CardContent::AVATAR_SIZE; y++) {
      for (size_t x = 0; x < CardContent::AVATAR_SIZE; x++) {
        int dx = (int)x - 36, dy = (int)y - 36;
        int r2 = dx * dx + dy * dy;
        bool on = ((r2 / 90) % 2 == 0) ^ (((x * 7 + y * 13) & 7) < 3);
        size_t bit = y * CardContent::AVATAR_SIZE + x;
        if (on) square.dash_avatar[bit / 8] |= 0x80 >> (bit % 8);
      }
    }
    square.dash_avatar_present = true;
    square.dash_avatar_square = true;
    dump("dash", square, st, "dash_legacy_square");
  }

  // Overlong payload strings must be clipped, not run into their neighbours.
  {
    CardContent longs = content;
    longs.dash_name = "Bartholomew Fitzgerald-Montgomery III";
    longs.dash_handle = "@an_extremely_long_handle_that_will_not_fit";
    longs.dash_weather_detail = "Thunderstorms with heavy rain and gusting wind · H104° L61°";
    longs.weather_location = "Llanfairpwllgwyngyllgogerychwyrndrobwllllantysiliogogogoch, Wales";
    longs.agenda_title[0] = "Quarterly cross-functional planning and roadmap review";
    longs.agenda_detail[0] = "with design, platform, infra, and the whole extended team";
    dump("dash", longs, st, "dash_long");
    dump("weather", longs, st, "weather_long");
    dump("agenda", longs, st, "agenda_long");
  }

  // AP-portal variant: the credentials must be legible on every page the
  // portal can be started from, including dash and weather.
  st.ap_hint = "AP DevDay-7F3A   pass kx29vq41pz   open http://192.168.4.1";
  dump("dash", content, st, "dash_ap");
  dump("weather", content, st, "weather_ap");
  dump("agenda", content, st, "agenda_ap");
  return 0;
}

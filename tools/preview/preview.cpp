// Host-side renderer for the Dev Day terminal cards. Compiles the real
// firmware/cards.cpp against stub Arduino/TFT headers and
// rasterizes each page to tools/preview/out/<page>.pgm (converted to PNG
// by build.sh). For the browser version see build_wasm.sh.
//
//   tools/preview/build.sh && open tools/preview/out/

#include "TFT_eSPI.h"

#include "cards.h"
#include "pet_samples.h"
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

  // Exercise the real two-frame path, not just its parser. The alternate must
  // visibly differ, and returning to primary must reproduce the base frame.
  {
    char path[256];
    renderCard("dash", content, st);
    renderDashPetFrame(content, true);
    snprintf(path, sizeof path, "%s/%s.pgm", out_dir, "dash_pet_alt");
    epaperDumpPgm(path);
    printf("wrote %s\n", path);
    renderDashPetFrame(content, false);
    snprintf(path, sizeof path, "%s/%s.pgm", out_dir, "dash_pet_rest");
    epaperDumpPgm(path);
    printf("wrote %s\n", path);
  }

  // Every empty page shares the prompt treatment; render all three asks.
  {
    CardContent empty = content;
    empty.dash_present = false;
    empty.dash_name = "";
    empty.weather_now_temp = "";
    empty.agenda_count = 0;
    dump("dash", empty, st, "dash_empty");
    dump("weather", empty, st, "weather_empty");
    dump("agenda", empty, st, "agenda_empty");
  }

  // No portrait pushed: the bundled pet stands in, so a fresh unit is never
  // faceless.
  {
    CardContent bundled = content;
    bundled.dash_avatar_present = false;
    dump("dash", bundled, st, "dash_bundled_pet");
  }

  // Varied avatar shapes: the backdrop and silhouette knockout must hold up
  // against more than the bundled pet.
  for (size_t i = 0; i < kPetSampleCount; i++) {
    CardContent pet = content;
    memcpy(pet.dash_avatar, kPetSamples[i].bits, CardContent::PET_BYTES);
    pet.dash_avatar_present = true;
    pet.dash_avatar_alt_present = false;
    char name[256];
    snprintf(name, sizeof name, "dash_pet_%s", kPetSamples[i].name);
    dump("dash", pet, st, name);
  }

  // Overlong payload strings must be clipped, not run into their neighbours.
  {
    CardContent longs = content;
    longs.dash_name = "Bartholomew Fitzgerald-Montgomery III";
    longs.dash_handle = "@an_extremely_long_handle_that_will_not_fit";
    longs.weather_location = "Llanfairpwllgwyngyllgogerychwyrndrobwllllantysiliogogogoch, Wales";
    longs.agenda_title[0] = "Quarterly cross-functional planning and roadmap review";
    longs.agenda_detail[0] = "with design, platform, infra, and the whole extended team";
    longs.build_state = "maintenance required";
    longs.build_title = "Factory firmware with a deliberately long release title";
    longs.build_detail = "This diagnostic description is intentionally long enough to exercise both wrapped lines without touching the status cell.";
    longs.build_updated_at = "Sunday, August 9 at 11:59 PM";
    dump("dash", longs, st, "dash_long");
    dump("weather", longs, st, "weather_long");
    dump("agenda", longs, st, "agenda_long");
    dump("build", longs, st, "build_long");
  }

  // A quiet day is the common live case. It should use the available vertical
  // space intentionally instead of looking like the first row of a missing
  // table.
  {
    CardContent one = content;
    one.agenda_count = 1;
    dump("agenda", one, st, "agenda_single");
  }

  // Disconnected header variant: same Wi-Fi silhouette, crossed for offline.
  {
    RenderStatus offline = st;
    offline.wifi_connected = false;
    dump("dash", content, offline, "dash_wifi_off");
  }

  // AP-portal variant: the credentials must be legible on every page the
  // portal can be started from, including dash and weather.
  st.ap_hint = "AP DevDay-7F3A   pass kx29vq41pz   open http://192.168.4.1";
  dump("dash", content, st, "dash_ap");
  dump("weather", content, st, "weather_ap");
  dump("agenda", content, st, "agenda_ap");
  dump("build", content, st, "build_ap");
  dump("yours", content, st, "yours_ap");

  // Empty-state portal credentials replace the resident face in the same band.
  {
    CardContent empty = content;
    empty.dash_present = false;
    empty.dash_name = "";
    dump("dash", empty, st, "dash_empty_ap");
  }
  return 0;
}

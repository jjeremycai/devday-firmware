// Host-side renderer for the Dev Day terminal cards. Compiles the real
// firmware/devday_terminal/cards.cpp against stub Arduino/TFT headers and
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

  displayBegin();
  const char* pages[] = {"dash", "weather", "agenda", "quote", "brief", "build", "yours"};
  for (const char* p : pages) {
    renderCard(p, content, st);
    char path[256];
    snprintf(path, sizeof path, "%s/%s.pgm", out_dir, p);
    epaperDumpPgm(path);
    printf("wrote %s\n", path);
  }

  // AP-portal variant: credentials overlay on the brief page.
  st.ap_hint = "AP DevDay-7F3A   pass kx29-vq41-pz82   open http://192.168.4.1";
  renderCard("brief", content, st);
  char path[256];
  snprintf(path, sizeof path, "%s/brief_ap.pgm", out_dir);
  epaperDumpPgm(path);
  printf("wrote %s\n", path);
  return 0;
}

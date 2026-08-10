// Display driver configuration generated from the Seeed GFX Configuration Tool
// for the 7.5" (OG) DIY Kit (UC8179 800x480 monochrome ePaper).
#pragma once

#define BOARD_SCREEN_COMBO 502 // 7.5 inch monochrome ePaper Screen (UC8179)
// Dev Day production boards use EE04. Early/bench units used Seeed's original
// XIAO ePaper driver board, whose CS/DC/BUSY/RESET pins are different. Build
// those explicitly with DISPLAY_BOARD=legacy; never infer the revision from a
// retained e-ink image.
#if defined(DEV_DAY_DISPLAY_BOARD_LEGACY)
#define USE_XIAO_EPAPER_DRIVER_BOARD
#else
#define USE_XIAO_EPAPER_DISPLAY_BOARD_EE04
#endif

// Enable Adafruit GFX free fonts (Seeed_GFX leaves LOAD_GFXFF off by default).
#define LOAD_GFXFF
#ifndef GFXFF
#define GFXFF 1
#endif

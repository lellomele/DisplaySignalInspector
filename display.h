#ifndef SEGNALE_SCHERMO_DISPLAY_H
#define SEGNALE_SCHERMO_DISPLAY_H

#include <windows.h>
#include <stdbool.h>

typedef struct {
    UINT32 path_count;
    UINT32 mode_count;
    UINT32 awareness;
    DISPLAYCONFIG_PATH_INFO *paths;
    DISPLAYCONFIG_MODE_INFO *modes;
} DisplayConfig;

typedef struct {
    LUID adapter;
    UINT32 id;
} DisplayKey;

typedef struct {
    UINT32 width;
    UINT32 height;
    UINT32 hz;
    const wchar_t *label;
} DisplayPreset;

enum { PRESET_FULL_HD, PRESET_QHD, PRESET_UHD_30, PRESET_UHD_60, PRESET_QHD_30, PRESET_COUNT };
extern const DisplayPreset display_presets[PRESET_COUNT];

LONG display_read(DisplayConfig *config);
void display_free(DisplayConfig *config);
DisplayKey display_key(const DISPLAYCONFIG_PATH_INFO *path);
bool display_same_key(DisplayKey a, DisplayKey b);
const DISPLAYCONFIG_PATH_INFO *display_find(const DisplayConfig *config, DisplayKey key);
const DISPLAYCONFIG_SOURCE_MODE *display_source(const DisplayConfig *config,
                                                 const DISPLAYCONFIG_PATH_INFO *path);
const DISPLAYCONFIG_VIDEO_SIGNAL_INFO *display_signal(const DisplayConfig *config,
                                                       const DISPLAYCONFIG_PATH_INFO *path);
bool display_is_external(const DISPLAYCONFIG_PATH_INFO *path);
bool display_is_clone(const DisplayConfig *config, const DISPLAYCONFIG_PATH_INFO *path);
const wchar_t *display_connector(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY output);
double display_hz(DISPLAYCONFIG_RATIONAL rate);
bool display_matches(const DISPLAYCONFIG_VIDEO_SIGNAL_INFO *signal, const DisplayPreset *preset);
bool display_verified(const DisplayConfig *before, const DisplayConfig *after,
                      DisplayKey changed, const DisplayPreset *preset);
bool display_equivalent(const DisplayConfig *before, const DisplayConfig *after);
bool display_same_targets(const DisplayConfig *before, const DisplayConfig *after);
LONG display_validate(const DisplayConfig *config);
LONG display_prepare(const DisplayConfig *before, DisplayKey target,
                     unsigned preset, DisplayConfig *request);
LONG display_apply(const DisplayConfig *config, bool save);

#endif

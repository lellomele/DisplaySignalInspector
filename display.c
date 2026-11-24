#include "display.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

const DisplayPreset display_presets[PRESET_COUNT] = {
    {1920, 1080, 60, L"1920 × 1080 · 60 Hz"},
    {2560, 1440, 60, L"2560 × 1440 · 60 Hz (QHD)"},
    {3840, 2160, 30, L"3840 × 2160 · 30 Hz"},
    {3840, 2160, 60, L"3840 × 2160 · 60 Hz"},
    {2560, 1440, 30, L"2560 × 1440 · 30 Hz (QHD)"}
};

static bool same_adapter(LUID a, LUID b)
{
    return a.HighPart == b.HighPart && a.LowPart == b.LowPart;
}

void display_free(DisplayConfig *config)
{
    free(config->paths);
    free(config->modes);
    *config = (DisplayConfig){0};
}

LONG display_read(DisplayConfig *result)
{
    UINT32 flags = QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE
                                      | QDC_VIRTUAL_REFRESH_RATE_AWARE;
    for (unsigned attempt = 0; attempt < 5; ++attempt) {
        DisplayConfig config = {0};
        LONG status = GetDisplayConfigBufferSizes(flags, &config.path_count, &config.mode_count);
        if (status == ERROR_SUCCESS) {
            config.paths = calloc(config.path_count ? config.path_count : 1, sizeof(*config.paths));
            config.modes = calloc(config.mode_count ? config.mode_count : 1, sizeof(*config.modes));
            if (!config.paths || !config.modes) {
                display_free(&config);
                return ERROR_OUTOFMEMORY;
            }
            status = QueryDisplayConfig(flags, &config.path_count, config.paths,
                                        &config.mode_count, config.modes, NULL);
        }
        if (status == ERROR_SUCCESS) {
            config.awareness = SDC_VIRTUAL_MODE_AWARE;
            if (flags & QDC_VIRTUAL_REFRESH_RATE_AWARE)
                config.awareness |= SDC_VIRTUAL_REFRESH_RATE_AWARE;
            display_free(result);
            *result = config;
            return ERROR_SUCCESS;
        }
        display_free(&config);
        if (status == ERROR_INVALID_PARAMETER && (flags & QDC_VIRTUAL_REFRESH_RATE_AWARE))
            flags &= ~((UINT32)QDC_VIRTUAL_REFRESH_RATE_AWARE);
        else if (status != ERROR_INSUFFICIENT_BUFFER)
            return status;
    }
    return ERROR_RETRY;
}

DisplayKey display_key(const DISPLAYCONFIG_PATH_INFO *path)
{
    return (DisplayKey){ path->targetInfo.adapterId, path->targetInfo.id };
}

bool display_same_key(DisplayKey a, DisplayKey b)
{
    return a.id == b.id && same_adapter(a.adapter, b.adapter);
}

const DISPLAYCONFIG_PATH_INFO *display_find(const DisplayConfig *config, DisplayKey key)
{
    for (UINT32 i = 0; i < config->path_count; ++i) {
        if (display_same_key(display_key(&config->paths[i]), key))
            return &config->paths[i];
    }
    return NULL;
}

static bool virtual_path(const DISPLAYCONFIG_PATH_INFO *path)
{
    return (path->flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) != 0;
}

const DISPLAYCONFIG_SOURCE_MODE *display_source(const DisplayConfig *config,
                                                 const DISPLAYCONFIG_PATH_INFO *path)
{
    UINT32 index = virtual_path(path) ? path->sourceInfo.sourceModeInfoIdx
                                      : path->sourceInfo.modeInfoIdx;
    if (virtual_path(path) && index == DISPLAYCONFIG_PATH_SOURCE_MODE_IDX_INVALID)
        return NULL;
    if (index >= config->mode_count)
        return NULL;
    const DISPLAYCONFIG_MODE_INFO *mode = &config->modes[index];
    if (mode->infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE ||
        mode->id != path->sourceInfo.id || !same_adapter(mode->adapterId, path->sourceInfo.adapterId))
        return NULL;
    return &mode->sourceMode;
}

static UINT32 target_index(const DisplayConfig *config, const DISPLAYCONFIG_PATH_INFO *path)
{
    UINT32 index = virtual_path(path) ? path->targetInfo.targetModeInfoIdx
                                      : path->targetInfo.modeInfoIdx;
    if (virtual_path(path) && index == DISPLAYCONFIG_PATH_TARGET_MODE_IDX_INVALID)
        return DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    if (index >= config->mode_count)
        return DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    const DISPLAYCONFIG_MODE_INFO *mode = &config->modes[index];
    if (mode->infoType != DISPLAYCONFIG_MODE_INFO_TYPE_TARGET ||
        mode->id != path->targetInfo.id || !same_adapter(mode->adapterId, path->targetInfo.adapterId))
        return DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    return index;
}

const DISPLAYCONFIG_VIDEO_SIGNAL_INFO *display_signal(const DisplayConfig *config,
                                                       const DISPLAYCONFIG_PATH_INFO *path)
{
    UINT32 index = target_index(config, path);
    if (index == DISPLAYCONFIG_PATH_MODE_IDX_INVALID)
        return NULL;
    return &config->modes[index].targetMode.targetVideoSignalInfo;
}

const wchar_t *display_connector(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY output)
{
    switch (output) {
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI: return L"HDMI";
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL: return L"DisplayPort";
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI: return L"DVI";
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15: return L"VGA";
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_USB_TUNNEL: return L"DisplayPort USB-C";
    default: return NULL;
    }
}

bool display_is_external(const DISPLAYCONFIG_PATH_INFO *path)
{
    return path->targetInfo.targetAvailable && display_connector(path->targetInfo.outputTechnology);
}

bool display_is_clone(const DisplayConfig *config, const DISPLAYCONFIG_PATH_INFO *path)
{
    const DISPLAYCONFIG_SOURCE_MODE *source = display_source(config, path);
    for (UINT32 i = 0; i < config->path_count; ++i) {
        const DISPLAYCONFIG_PATH_INFO *other = &config->paths[i];
        if (display_same_key(display_key(other), display_key(path)))
            continue;
        if (same_adapter(path->sourceInfo.adapterId, other->sourceInfo.adapterId) &&
            path->sourceInfo.id == other->sourceInfo.id)
            return true;
        const DISPLAYCONFIG_SOURCE_MODE *other_source = display_source(config, other);
        if (source && other_source && source->position.x == other_source->position.x &&
            source->position.y == other_source->position.y)
            return true;
    }
    return false;
}

double display_hz(DISPLAYCONFIG_RATIONAL rate)
{
    return rate.Denominator ? (double)rate.Numerator / rate.Denominator : 0.0;
}

bool display_matches(const DISPLAYCONFIG_VIDEO_SIGNAL_INFO *signal, const DisplayPreset *preset)
{
    return signal && signal->activeSize.cx == preset->width &&
           signal->activeSize.cy == preset->height &&
           signal->scanLineOrdering == DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE &&
           fabs(display_hz(signal->vSyncFreq) - preset->hz) <= 0.11;
}

bool display_same_targets(const DisplayConfig *before, const DisplayConfig *after)
{
    if (before->path_count != after->path_count)
        return false;
    for (UINT32 i = 0; i < before->path_count; ++i) {
        const DISPLAYCONFIG_PATH_INFO *path = display_find(after, display_key(&before->paths[i]));
        if (!path || !path->targetInfo.targetAvailable)
            return false;
    }
    return true;
}

static bool same_source_mode(const DISPLAYCONFIG_SOURCE_MODE *a, const DISPLAYCONFIG_SOURCE_MODE *b)
{
    return a && b && a->width == b->width && a->height == b->height &&
           a->pixelFormat == b->pixelFormat && a->position.x == b->position.x &&
           a->position.y == b->position.y;
}

static bool same_other_signal(const DISPLAYCONFIG_VIDEO_SIGNAL_INFO *a,
                              const DISPLAYCONFIG_VIDEO_SIGNAL_INFO *b)
{
    return a && b && a->activeSize.cx == b->activeSize.cx && a->activeSize.cy == b->activeSize.cy &&
           fabs(display_hz(a->vSyncFreq) - display_hz(b->vSyncFreq)) < 0.01 &&
           a->scanLineOrdering == b->scanLineOrdering;
}

static bool verify(const DisplayConfig *before, const DisplayConfig *after,
                   DisplayKey changed, const DisplayPreset *preset)
{
    if (!display_same_targets(before, after) || (preset && !display_find(before, changed)))
        return false;
    for (UINT32 i = 0; i < before->path_count; ++i) {
        const DISPLAYCONFIG_PATH_INFO *old = &before->paths[i];
        DisplayKey key = display_key(old);
        const DISPLAYCONFIG_PATH_INFO *now = display_find(after, key);
        if (old->targetInfo.rotation != now->targetInfo.rotation ||
            old->targetInfo.outputTechnology != now->targetInfo.outputTechnology ||
            display_is_clone(before, old) != display_is_clone(after, now) ||
            !same_source_mode(display_source(before, old), display_source(after, now)))
            return false;
        const DISPLAYCONFIG_VIDEO_SIGNAL_INFO *signal = display_signal(after, now);
        if (preset && display_same_key(key, changed)) {
            if (!display_matches(signal, preset))
                return false;
        } else if (!same_other_signal(display_signal(before, old), signal) ||
                   old->targetInfo.scaling != now->targetInfo.scaling) {
            return false;
        }
    }
    return true;
}

bool display_verified(const DisplayConfig *before, const DisplayConfig *after,
                      DisplayKey changed, const DisplayPreset *preset)
{
    return verify(before, after, changed, preset);
}

bool display_equivalent(const DisplayConfig *before, const DisplayConfig *after)
{
    return verify(before, after, (DisplayKey){0}, NULL);
}

typedef struct {
    unsigned preset;
    UINT32 total_width;
    UINT32 total_height;
    UINT32 pixel_clock;
    UINT32 standard;
} Timing;

/* Numeric values of D3DKMDT_VSS_* used by Win32 signal structures. */
enum { VIDEO_CVT = 3, VIDEO_EIA_861B = 27, VIDEO_OTHER = 255 };

static const Timing timings[] = {
    { PRESET_FULL_HD, 2200, 1125, 148500000, VIDEO_EIA_861B },
    { PRESET_FULL_HD, 2200, 1125, 148351648, VIDEO_EIA_861B },
    { PRESET_QHD_30,  2720, 1461, 119000000, VIDEO_CVT },
    { PRESET_QHD_30,  2720, 1481, 120850000, VIDEO_OTHER },
    { PRESET_QHD,     2720, 1481, 241500000, VIDEO_CVT },
    { PRESET_QHD,     2640, 1481, 234590000, VIDEO_CVT },
    { PRESET_UHD_30,  4400, 2250, 297000000, VIDEO_EIA_861B },
    { PRESET_UHD_30,  4400, 2250, 296703297, VIDEO_EIA_861B },
    { PRESET_UHD_60,  4400, 2250, 594000000, VIDEO_EIA_861B },
    { PRESET_UHD_60,  4400, 2250, 593406593, VIDEO_EIA_861B }
};

static DISPLAYCONFIG_VIDEO_SIGNAL_INFO make_signal(const Timing *timing)
{
    const DisplayPreset *preset = &display_presets[timing->preset];
    return (DISPLAYCONFIG_VIDEO_SIGNAL_INFO){
        .pixelRate = timing->pixel_clock,
        .hSyncFreq = { timing->pixel_clock, timing->total_width },
        .vSyncFreq = { timing->pixel_clock, timing->total_width * timing->total_height },
        .activeSize = { preset->width, preset->height },
        .totalSize = { timing->total_width, timing->total_height },
        .videoStandard = timing->standard,
        .scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE
    };
}

static LONG submit(const DisplayConfig *config, UINT32 operation)
{
    return SetDisplayConfig(config->path_count, config->paths, config->mode_count, config->modes,
                            SDC_USE_SUPPLIED_DISPLAY_CONFIG | config->awareness | operation);
}

LONG display_validate(const DisplayConfig *config)
{
    return submit(config, SDC_VALIDATE);
}

static LONG validate_candidate(DisplayConfig *config, DISPLAYCONFIG_PATH_INFO *path, UINT32 index,
                               const DISPLAYCONFIG_VIDEO_SIGNAL_INFO *signal)
{
    config->modes[index].targetMode.targetVideoSignalInfo = *signal;
    path->targetInfo.refreshRate = signal->vSyncFreq;
    path->targetInfo.scanLineOrdering = signal->scanLineOrdering;
    return display_validate(config);
}

static bool fit_virtual_image(DisplayConfig *config, const DISPLAYCONFIG_PATH_INFO *path,
                              const DISPLAYCONFIG_VIDEO_SIGNAL_INFO *original,
                              const DisplayPreset *selected)
{
    if (!virtual_path(path))
        return false;
    UINT32 index = path->targetInfo.desktopModeInfoIdx;
    if (index == DISPLAYCONFIG_PATH_DESKTOP_IMAGE_IDX_INVALID || index >= config->mode_count)
        return false;
    DISPLAYCONFIG_MODE_INFO *mode = &config->modes[index];
    if (mode->infoType != DISPLAYCONFIG_MODE_INFO_TYPE_DESKTOP_IMAGE ||
        mode->id != path->targetInfo.id || !same_adapter(mode->adapterId, path->targetInfo.adapterId))
        return false;

    /* The desktop image shares the mode union with targetMode. Older MinGW
       headers provide the structure but omit its named union member. */
    DISPLAYCONFIG_DESKTOP_IMAGE_INFO image;
    _Static_assert(sizeof(image) <= sizeof(mode->targetMode), "Unexpected Win32 mode layout");
    memcpy(&image, &mode->targetMode, sizeof(image));
    LONG canvas_width = (LONG)selected->width;
    LONG canvas_height = (LONG)selected->height;
    if ((UINT32)image.PathSourceSize.x == original->activeSize.cy &&
        (UINT32)image.PathSourceSize.y == original->activeSize.cx &&
        original->activeSize.cx != original->activeSize.cy) {
        canvas_width = (LONG)selected->height;
        canvas_height = (LONG)selected->width;
    }

    LONGLONG content_width = (LONGLONG)image.DesktopImageClip.right - image.DesktopImageClip.left;
    LONGLONG content_height = (LONGLONG)image.DesktopImageClip.bottom - image.DesktopImageClip.top;
    if (content_width <= 0 || content_height <= 0)
        return false;
    LONG width = canvas_width;
    LONG height = canvas_height;
    if (canvas_width * content_height > canvas_height * content_width)
        width = (LONG)(canvas_height * content_width / content_height);
    else
        height = (LONG)(canvas_width * content_height / content_width);
    if (!width || !height)
        return false;
    LONG left = (canvas_width - width) / 2;
    LONG top = (canvas_height - height) / 2;
    image.PathSourceSize = (POINTL){ canvas_width, canvas_height };
    image.DesktopImageRegion = (RECTL){ left, top, left + width, top + height };
    memcpy(&mode->targetMode, &image, sizeof(image));
    return true;
}

LONG display_prepare(const DisplayConfig *before, DisplayKey target, unsigned preset, DisplayConfig *request)
{
    const DISPLAYCONFIG_PATH_INFO *original = display_find(before, target);
    if (!original || !display_is_external(original) || preset >= PRESET_COUNT)
        return ERROR_INVALID_PARAMETER;
    UINT32 index = target_index(before, original);
    const DISPLAYCONFIG_SOURCE_MODE *source = display_source(before, original);
    if (index == DISPLAYCONFIG_PATH_MODE_IDX_INVALID || !source)
        return ERROR_NOT_SUPPORTED;

    DisplayConfig config = { before->path_count, before->mode_count, before->awareness, NULL, NULL };
    config.paths = calloc(config.path_count, sizeof(*config.paths));
    config.modes = calloc(config.mode_count, sizeof(*config.modes));
    if (!config.paths || !config.modes) {
        display_free(&config);
        return ERROR_OUTOFMEMORY;
    }
    memcpy(config.paths, before->paths, config.path_count * sizeof(*config.paths));
    memcpy(config.modes, before->modes, config.mode_count * sizeof(*config.modes));
    DISPLAYCONFIG_PATH_INFO *path = &config.paths[original - before->paths];
    const DisplayPreset *selected = &display_presets[preset];
    UINT32 width = source->width;
    UINT32 height = source->height;
    if (path->targetInfo.rotation == DISPLAYCONFIG_ROTATION_ROTATE90 ||
        path->targetInfo.rotation == DISPLAYCONFIG_ROTATION_ROTATE270) {
        width = source->height;
        height = source->width;
    }
    bool virtual_fit = fit_virtual_image(&config, path, display_signal(before, original), selected);
    path->targetInfo.scaling = virtual_fit || (width == selected->width && height == selected->height)
                             ? DISPLAYCONFIG_SCALING_IDENTITY : DISPLAYCONFIG_SCALING_ASPECTRATIOCENTEREDMAX;
    path->flags &= ~((UINT32)DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE);

    DISPLAYCONFIG_TARGET_PREFERRED_MODE preferred = {0};
    preferred.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE;
    preferred.header.size = sizeof(preferred);
    preferred.header.adapterId = target.adapter;
    preferred.header.id = target.id;
    LONG status = ERROR_BAD_CONFIGURATION;
    if (DisplayConfigGetDeviceInfo(&preferred.header) == ERROR_SUCCESS &&
        display_matches(&preferred.targetMode.targetVideoSignalInfo, selected))
        status = validate_candidate(&config, path, index, &preferred.targetMode.targetVideoSignalInfo);

    for (size_t i = 0; status != ERROR_SUCCESS && i < ARRAYSIZE(timings); ++i) {
        if (timings[i].preset != preset)
            continue;
        DISPLAYCONFIG_VIDEO_SIGNAL_INFO signal = make_signal(&timings[i]);
        status = validate_candidate(&config, path, index, &signal);
        if (status == ERROR_BAD_CONFIGURATION || status == ERROR_INVALID_PARAMETER) {
            signal.videoStandard = VIDEO_OTHER;
            status = validate_candidate(&config, path, index, &signal);
        }
        if (status == ERROR_ACCESS_DENIED)
            break;
    }
    if (status != ERROR_SUCCESS) {
        display_free(&config);
        return status;
    }
    display_free(request);
    *request = config;
    return ERROR_SUCCESS;
}

LONG display_apply(const DisplayConfig *config, bool save)
{
    return submit(config, SDC_APPLY | (save ? SDC_SAVE_TO_DATABASE : 0));
}

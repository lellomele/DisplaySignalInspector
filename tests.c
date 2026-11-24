#include "edid.h"
#include <assert.h>
#include <stdio.h>

static DISPLAYCONFIG_VIDEO_SIGNAL_INFO signal(UINT32 width, UINT32 height, UINT32 n, UINT32 d)
{
    return (DISPLAYCONFIG_VIDEO_SIGNAL_INFO){
        .vSyncFreq = { n, d },
        .activeSize = { width, height },
        .scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE
    };
}

static void check_clone(void)
{
    DISPLAYCONFIG_PATH_INFO paths[2] = {0};
    DISPLAYCONFIG_MODE_INFO modes[3] = {0};
    paths[0].targetInfo.id = 20;
    paths[0].targetInfo.targetModeInfoIdx = 1;
    paths[0].targetInfo.targetAvailable = TRUE;
    paths[0].targetInfo.outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED;
    paths[0].flags = DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE | DISPLAYCONFIG_PATH_ACTIVE;
    paths[1] = paths[0];
    paths[1].targetInfo.id = 21;
    paths[1].targetInfo.targetModeInfoIdx = 2;
    paths[1].targetInfo.outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
    modes[0].infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
    modes[0].sourceMode = (DISPLAYCONFIG_SOURCE_MODE){ 2520, 1680, DISPLAYCONFIG_PIXELFORMAT_32BPP, {0, 0} };
    modes[1].infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
    modes[1].id = 20;
    modes[1].targetMode.targetVideoSignalInfo = signal(2520, 1680, 60, 1);
    modes[2].infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
    modes[2].id = 21;
    modes[2].targetMode.targetVideoSignalInfo = signal(3840, 2160, 30, 1);
    DisplayConfig before = { 2, 3, SDC_VIRTUAL_MODE_AWARE, paths, modes };

    DISPLAYCONFIG_PATH_INFO active_paths[2] = {paths[1], paths[0]};
    DISPLAYCONFIG_MODE_INFO active_modes[3] = {modes[0], modes[1], modes[2]};
    DisplayConfig active = { 2, 3, SDC_VIRTUAL_MODE_AWARE, active_paths, active_modes };
    DisplayKey hdmi = display_key(&paths[1]);
    const DisplayPreset *qhd = &display_presets[PRESET_QHD];
    assert(display_is_clone(&before, &paths[1]));
    assert(display_is_external(&paths[1]));
    assert(!display_is_external(&paths[0]));
    assert(display_equivalent(&before, &active));
    assert(!display_verified(&before, &active, hdmi, qhd));
    active_modes[2].targetMode.targetVideoSignalInfo = signal(2560, 1440, 60000, 1001);
    assert(display_verified(&before, &active, hdmi, qhd));
    assert(!display_equivalent(&before, &active));
    active_modes[1].targetMode.targetVideoSignalInfo.vSyncFreq.Numerator = 90;
    assert(!display_verified(&before, &active, hdmi, qhd));
    active_modes[1] = modes[1];
    active_modes[0].sourceMode.width = 2560;
    assert(!display_verified(&before, &active, hdmi, qhd));
    active_modes[0] = modes[0];
    active_paths[0].targetInfo.targetAvailable = FALSE;
    assert(!display_verified(&before, &active, hdmi, qhd));
    active_paths[0].targetInfo.targetAvailable = TRUE;
    active_paths[0].targetInfo.targetModeInfoIdx = DISPLAYCONFIG_PATH_TARGET_MODE_IDX_INVALID;
    assert(!display_verified(&before, &active, hdmi, qhd));
}

int main(void)
{
    check_clone();
    puts("PASS clone, timing, unchanged desktop and secondary signal, disconnect.");
    DisplayConfig active = {0};
    LONG status = display_read(&active);
    if (status != ERROR_SUCCESS) {
        printf("SKIP live display access (Windows %ld).\n", status);
        return 0;
    }
    printf("Live displays (%lu):\n", (unsigned long)active.path_count);
    for (UINT32 i = 0; i < active.path_count; ++i) {
        const DISPLAYCONFIG_PATH_INFO *path = &active.paths[i];
        const DISPLAYCONFIG_VIDEO_SIGNAL_INFO *video = display_signal(&active, path);
        if (video) {
            DisplayKey key = display_key(path);
            DISPLAYCONFIG_TARGET_DEVICE_NAME name = {0};
            name.header = (DISPLAYCONFIG_DEVICE_INFO_HEADER){
                DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(name), key.adapter, key.id
            };
            DisplayConfigGetDeviceInfo(&name.header);
            if(display_is_external(path)) {
                BYTE edid[128*32];
                unsigned blocks=read_edid(name.monitorDevicePath,edid,32);
                printf("EDID blocks=%u (expected=%u)\n",blocks,blocks ? 1u+edid[126] : 0);
                for(unsigned j=0;j<PRESET_COUNT;j++)
                    printf("%ux%u @%u EDID=%s\n",display_presets[j].width,display_presets[j].height,display_presets[j].hz,!wcscmp(edid_status(edid,blocks,&display_presets[j]),L"s\u00ec") ? "listed" : !wcscmp(edid_status(edid,blocks,&display_presets[j]),L"non rilevato") ? "not found" : "unknown");
            }
            printf("  %ls (output %u%s): %lux%lu @ %.2f Hz\n", name.monitorFriendlyDeviceName,
                   (unsigned)path->targetInfo.outputTechnology, display_is_clone(&active, path) ? ", clone" : "",
                   (unsigned long)video->activeSize.cx, (unsigned long)video->activeSize.cy,
                   display_hz(video->vSyncFreq));
            const DISPLAYCONFIG_SOURCE_MODE *source = display_source(&active, path);
            if (source)
                printf("  desktop %lux%lu; scaling %u\n", (unsigned long)source->width,
                       (unsigned long)source->height, (unsigned)path->targetInfo.scaling);
        }
        if (display_is_external(path)) {
            for (unsigned j = 0; j < PRESET_COUNT; ++j) {
                DisplayConfig request = {0};
                const DisplayPreset *mode = &display_presets[j];
                LONG result = display_prepare(&active, display_key(path), j, &request);
                printf("  %lux%lu @ %lu Hz read-only validation: Windows %ld\n",
                       (unsigned long)mode->width, (unsigned long)mode->height,
                       (unsigned long)mode->hz, result);
                display_free(&request);
            }
        }
    }
    printf("Live baseline read-only validation: Windows %ld\n", display_validate(&active));
    display_free(&active);
    return 0;
}

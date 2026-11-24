#include "edid.h"
#include "resource.h"
#include <dbt.h>
#include <wchar.h>

enum { TRIAL_TIMER = 1, TRIAL_MS = 15000, SETTLE_MS = 2800 };

typedef struct {
    HWND dialog;
    DisplayConfig current;
    DisplayConfig before;
    DisplayConfig request;
    DisplayKey target;
    unsigned preset;
    ULONGLONG trial_start;
    bool trial;
    bool verified;
    bool validated[PRESET_COUNT];
    HFONT preset_bold;
} App;

static void status(App *app, const wchar_t *message)
{
    SetDlgItemTextW(app->dialog, IDC_STATUS, message);
}

static void error_status(App *app, const wchar_t *message, LONG code)
{
    wchar_t text[196];
    swprintf(text, ARRAYSIZE(text), L"%ls (Windows %ld).", message, code);
    status(app, text);
}

static HWND monitor_list(const App *app)
{
    return GetDlgItem(app->dialog, IDC_MONITOR);
}

static const DISPLAYCONFIG_PATH_INFO *selected_path(const App *app)
{
    HWND list = monitor_list(app);
    LRESULT item = SendMessageW(list, CB_GETCURSEL, 0, 0);
    if (item == CB_ERR)
        return NULL;
    LRESULT index = SendMessageW(list, CB_GETITEMDATA, (WPARAM)item, 0);
    if (index < 0 || (UINT_PTR)index >= app->current.path_count)
        return NULL;
    return &app->current.paths[index];
}

static void refresh_controls(App *app)
{
    bool has_output = selected_path(app) != NULL && !app->trial;
    EnableWindow(monitor_list(app), !app->trial);
    EnableWindow(GetDlgItem(app->dialog, IDC_REFRESH), !app->trial);
    EnableWindow(GetDlgItem(app->dialog, IDC_PRESET), has_output);
    EnableWindow(GetDlgItem(app->dialog, IDC_SCAN), has_output);
    EnableWindow(GetDlgItem(app->dialog, IDC_VALIDATE), has_output);
    EnableWindow(GetDlgItem(app->dialog, IDC_TRY), has_output);
    EnableWindow(GetDlgItem(app->dialog, IDC_RESTORE), app->trial);
    EnableWindow(GetDlgItem(app->dialog, IDC_KEEP), app->trial && app->verified);
}

static void show_path(App *app, const DisplayConfig *config, const DISPLAYCONFIG_PATH_INFO *path)
{
    wchar_t desktop[96] = L"—";
    wchar_t signal[96] = L"—";
    if (path) {
        const DISPLAYCONFIG_SOURCE_MODE *source = display_source(config, path);
        const DISPLAYCONFIG_VIDEO_SIGNAL_INFO *video = display_signal(config, path);
        if (source)
            swprintf(desktop, ARRAYSIZE(desktop), L"%lu × %lu · %ls",
                     (unsigned long)source->width, (unsigned long)source->height,
                     display_is_clone(config, path) ? L"Duplica" : L"Indipendente");
        if (video)
            swprintf(signal, ARRAYSIZE(signal), L"%lu × %lu · %.2f Hz",
                     (unsigned long)video->activeSize.cx, (unsigned long)video->activeSize.cy,
                     display_hz(video->vSyncFreq));
    }
    SetDlgItemTextW(app->dialog, IDC_DESKTOP, desktop);
    SetDlgItemTextW(app->dialog, IDC_SIGNAL, signal);
}

static void clear_validation(App *app)
{
    memset(app->validated, 0, sizeof(app->validated));
    InvalidateRect(GetDlgItem(app->dialog, IDC_PRESET), NULL, TRUE);
}

static void reload(App *app, bool report)
{
    clear_validation(app);
    const DISPLAYCONFIG_PATH_INFO *old = selected_path(app);
    bool had_selection = old != NULL;
    DisplayKey previous = old ? display_key(old) : (DisplayKey){0};
    LONG code = display_read(&app->current);
    HWND list = monitor_list(app);
    SendMessageW(list, CB_RESETCONTENT, 0, 0);
    if (code != ERROR_SUCCESS) {
        display_free(&app->current);
        show_path(app, &app->current, NULL);
        refresh_controls(app);
        error_status(app, L"Lettura degli schermi non riuscita", code);
        return;
    }
    LRESULT last_selection = CB_ERR;
    LRESULT dahua = CB_ERR;
    for (UINT32 i = 0; i < app->current.path_count; ++i) {
        const DISPLAYCONFIG_PATH_INFO *path = &app->current.paths[i];
        if (!display_is_external(path))
            continue;
        DisplayKey key = display_key(path);
        DISPLAYCONFIG_TARGET_DEVICE_NAME name = {0};
        name.header = (DISPLAYCONFIG_DEVICE_INFO_HEADER){
            DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(name), key.adapter, key.id
        };
        DisplayConfigGetDeviceInfo(&name.header);
        const wchar_t *connector = display_connector(path->targetInfo.outputTechnology);
        wchar_t label[115];
        if (name.monitorFriendlyDeviceName[0])
            swprintf(label, ARRAYSIZE(label), L"%ls · %ls", name.monitorFriendlyDeviceName, connector);
        else
            swprintf(label, ARRAYSIZE(label), L"Monitor %lu · %ls", (unsigned long)i + 1, connector);
        LRESULT item = SendMessageW(list, CB_ADDSTRING, 0, (LPARAM)label);
        if (item < 0)
            continue;
        SendMessageW(list, CB_SETITEMDATA, (WPARAM)item, i);
        if (had_selection && display_same_key(previous, key))
            last_selection = item;
        if (dahua == CB_ERR && FindStringOrdinal(FIND_FROMSTART, label, -1, L"Dahua", -1, TRUE) >= 0)
            dahua = item;
    }
    LRESULT selection = last_selection != CB_ERR ? last_selection : (dahua != CB_ERR ? dahua : 0);
    SendMessageW(list, CB_SETCURSEL, (WPARAM)selection, 0);
    const DISPLAYCONFIG_PATH_INFO *path = selected_path(app);
    show_path(app, &app->current, path);
    refresh_controls(app);
    if (!path)
        status(app, L"Collega e attiva uno schermo esterno, poi premi Aggiorna.");
    else if (report)
        status(app, L"Verifica controlla la modalità senza cambiare lo schermo.");
}

static void draw_mode(const DRAWITEMSTRUCT *row, bool combo, HFONT bold)
{
    BOOL selected = (row->itemState & ODS_SELECTED) != 0;
    FillRect(row->hDC, &row->rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    if (row->itemID != (UINT)-1) {
        wchar_t text[256];
        LRESULT length = SendMessageW(row->hwndItem, combo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN, row->itemID, 0);
        if (length >= 0 && (size_t)length < ARRAYSIZE(text) &&
            SendMessageW(row->hwndItem, combo ? CB_GETLBTEXT : LB_GETTEXT, row->itemID, (LPARAM)text) != LB_ERR) {
            HFONT font = bold;
            if (!font) font = (HFONT)SendMessageW(row->hwndItem, WM_GETFONT, 0, 0);
            int saved = SaveDC(row->hDC);
            if (saved) {
                if (font) SelectObject(row->hDC, font);
                SetTextColor(row->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
                SetBkMode(row->hDC, TRANSPARENT);
                RECT area = row->rcItem;
                area.left += 3;
                DrawTextW(row->hDC, text, -1, &area, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
                RestoreDC(row->hDC, saved);
            }
        }
    }
    if (row->itemState & ODS_FOCUS) DrawFocusRect(row->hDC, &row->rcItem);
}

static INT_PTR CALLBACK scan_dialog(HWND dialog, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_DRAWITEM && wp == IDC_RESULTS) {
        const DRAWITEMSTRUCT *row = (const DRAWITEMSTRUCT *)lp;
        draw_mode(row, false, row->itemData ? (HFONT)GetWindowLongPtrW(dialog, DWLP_USER) : NULL);
        return TRUE;
    }
    if (message == WM_DESTROY) {
        DeleteObject((HFONT)GetWindowLongPtrW(dialog, DWLP_USER));
        SetWindowLongPtrW(dialog, DWLP_USER, 0);
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL))) {
        EndDialog(dialog, 0);
        return TRUE;
    }
    if (message != WM_INITDIALOG) return FALSE;
    App *app = (App *)lp;
    DisplayKey key = display_key(selected_path(app));
    DisplayConfig config = {0};
    HWND list = GetDlgItem(dialog, IDC_RESULTS);
    LOGFONTW style;
    HFONT normal = (HFONT)SendMessageW(list, WM_GETFONT, 0, 0);
    if (GetObjectW(normal, sizeof(style), &style) == sizeof(style)) {
        style.lfWeight = FW_BOLD;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)CreateFontIndirectW(&style));
        SendMessageW(list, LB_SETITEMHEIGHT, 0, abs(style.lfHeight) + 5);
    }
    const DISPLAYCONFIG_PATH_INFO *path = display_read(&config) == ERROR_SUCCESS ? display_find(&config, key) : NULL;
    if (!path) {
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Schermo non disponibile.");
    } else {
        wchar_t line[200];
        BYTE edid[128*32];
        DISPLAYCONFIG_TARGET_DEVICE_NAME target={0};
        target.header=(DISPLAYCONFIG_DEVICE_INFO_HEADER){DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(target),key.adapter,key.id};
        unsigned blocks=DisplayConfigGetDeviceInfo(&target.header)==ERROR_SUCCESS ?
            read_edid(target.monitorDevicePath,edid,32) : 0;
        const DISPLAYCONFIG_SOURCE_MODE *desktop=display_source(&config,path);
        const DISPLAYCONFIG_VIDEO_SIGNAL_INFO *signal=display_signal(&config,path);
        if(desktop && signal) {
            swprintf(line,ARRAYSIZE(line),L"Desktop %lu × %lu → segnale %lu × %lu · %.2f Hz",desktop->width,desktop->height,signal->activeSize.cx,signal->activeSize.cy,display_hz(signal->vSyncFreq));
            SendMessageW(list,LB_ADDSTRING,0,(LPARAM)line);
        }
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Segnali esterni · verifica del driver");
        const LRESULT first_signal = SendMessageW(list, LB_GETCOUNT, 0, 0);
        unsigned confirmed = 0;
        for (unsigned i = 0; i < PRESET_COUNT; ++i) {
            DisplayConfig request = {0};
            bool active = display_matches(display_signal(&config, path), &display_presets[i]);
            LONG code = active ? ERROR_SUCCESS : display_prepare(&config, key, i, &request);
            app->validated[i] = code == ERROR_SUCCESS;
            if (code == ERROR_SUCCESS)
                swprintf(line, ARRAYSIZE(line), L"%ls | EDID: %ls | %ls", display_presets[i].label, edid_status(edid,blocks,&display_presets[i]), active ? L"attivo" : L"validato");
            else
                swprintf(line, ARRAYSIZE(line), L"%ls | EDID: %ls | non validato (%ld)", display_presets[i].label, edid_status(edid,blocks,&display_presets[i]), code);
            if (code == ERROR_SUCCESS) {
                LRESULT index = active ? first_signal : first_signal + confirmed;
                LRESULT row = SendMessageW(list, LB_INSERTSTRING, (WPARAM)index, (LPARAM)line);
                if (row >= 0) {
                    SendMessageW(list, LB_SETITEMDATA, (WPARAM)row, 1);
                    ++confirmed;
                }
            } else {
                SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)line);
            }
            display_free(&request);
        }
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Modalità desktop della sorgente (condivisa in Duplica)");
        DISPLAYCONFIG_SOURCE_DEVICE_NAME name = {0};
        name.header = (DISPLAYCONFIG_DEVICE_INFO_HEADER){DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(name), path->sourceInfo.adapterId, path->sourceInfo.id};
        int first = (int)SendMessageW(list, LB_GETCOUNT, 0, 0);
        if (DisplayConfigGetDeviceInfo(&name.header) == ERROR_SUCCESS) {
            for (DWORD i = 0; ; ++i) {
                DEVMODEW mode = {0};
                mode.dmSize = sizeof(mode);
                if (!EnumDisplaySettingsExW(name.viewGdiDeviceName, i, &mode, 0)) break;
                swprintf(line, ARRAYSIZE(line), L"%lu × %lu · %lu Hz%ls", mode.dmPelsWidth, mode.dmPelsHeight, mode.dmDisplayFrequency, (mode.dmDisplayFlags & DM_INTERLACED) ? L" (interlacciato)" : L"");
                if (SendMessageW(list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)line) == LB_ERR)
                    SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)line);
            }
        }
        if (SendMessageW(list, LB_GETCOUNT, 0, 0) == first)
            SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Nessuna modalità enumerata.");
    }
    display_free(&config);
    return TRUE;
}

static void finish(App *app)
{
    KillTimer(app->dialog, TRIAL_TIMER);
    app->trial = false;
    app->verified = false;
    display_free(&app->request);
    display_free(&app->before);
    reload(app, false);
}

static void restore(App *app, const wchar_t *message)
{
    DisplayConfig actual = {0};
    LONG code = display_read(&actual);
    bool same_targets = code == ERROR_SUCCESS && display_same_targets(&app->before, &actual);
    if (same_targets && display_equivalent(&app->before, &actual)) {
        code = ERROR_SUCCESS;
    } else if (code != ERROR_SUCCESS || same_targets) {
        code = display_apply(&app->before, false);
        if (code == ERROR_SUCCESS) {
            code = display_read(&actual);
            if (code == ERROR_SUCCESS && !display_equivalent(&app->before, &actual))
                code = ERROR_BAD_CONFIGURATION;
        }
    } else {
        code = ERROR_DEVICE_NOT_CONNECTED;
    }
    display_free(&actual);
    bool fallback = false;
    if (code != ERROR_SUCCESS) {
        /* Let Windows choose among the configurations saved for the monitors now present. */
        LONG saved = SetDisplayConfig(0, NULL, 0, NULL, SDC_APPLY | SDC_USE_DATABASE_CURRENT);
        fallback = saved == ERROR_SUCCESS;
        if (fallback)
            code = ERROR_SUCCESS;
    }
    finish(app);
    if (fallback)
        status(app, L"Configurazione cambiata durante la prova. Windows ha ricaricato il profilo salvato.");
    else if (code != ERROR_SUCCESS)
        error_status(app, L"Ripristino non riuscito. Usa Win+P per riattivare gli schermi", code);
    else
        status(app, message);
}

static void begin(App *app, bool test_only)
{
    const DISPLAYCONFIG_PATH_INFO *selected = selected_path(app);
    LRESULT preset = SendDlgItemMessageW(app->dialog, IDC_PRESET, CB_GETCURSEL, 0, 0);
    if (!selected || app->trial || preset < 0 || preset >= PRESET_COUNT)
        return;
    DisplayKey key = display_key(selected);
    LONG code = display_read(&app->before);
    if (code != ERROR_SUCCESS) {
        error_status(app, L"Lettura della configurazione non riuscita", code);
        return;
    }
    const DISPLAYCONFIG_PATH_INFO *current = display_find(&app->before, key);
    const DisplayPreset *mode = &display_presets[preset];
    if (!current || !display_is_external(current)) {
        finish(app);
        status(app, L"Lo schermo selezionato non è più attivo. Controlla il collegamento.");
        return;
    }
    if (display_matches(display_signal(&app->before, current), mode)) {
        finish(app);
        status(app, L"Questo segnale è già attivo sullo schermo selezionato.");
        return;
    }
    code = display_prepare(&app->before, key, (unsigned)preset, &app->request);
    if (code != ERROR_SUCCESS) {
        finish(app);
        if (code == ERROR_ACCESS_DENIED)
            status(app, L"Questa sessione non può gestire gli schermi. Apri l'app dal desktop del notebook.");
        else
            error_status(app, L"Il driver non ha accettato il segnale con questa configurazione", code);
        return;
    }
    if (test_only) {
        finish(app);
        status(app, L"Modalità convalidata. Usa Prova per controllare il segnale effettivo.");
        return;
    }
    code = display_validate(&app->before);
    if (code != ERROR_SUCCESS || !SetTimer(app->dialog, TRIAL_TIMER, 250, NULL)) {
        finish(app);
        status(app, L"Impossibile preparare il ripristino automatico. Prova annullata.");
        return;
    }
    app->target = key;
    app->preset = (unsigned)preset;
    app->trial = true;
    app->trial_start = GetTickCount64();
    refresh_controls(app);
    status(app, L"Applicazione e verifica del segnale in corso…");
    code = display_apply(&app->request, false);
    app->trial_start = GetTickCount64();
    if (code != ERROR_SUCCESS) {
        wchar_t message[132];
        swprintf(message, ARRAYSIZE(message), L"Il driver ha rifiutato la prova (Windows %ld).", code);
        restore(app, message);
    }
}

static void tick(App *app)
{
    if (!app->trial)
        return;
    ULONGLONG elapsed = GetTickCount64() - app->trial_start;
    if (elapsed >= TRIAL_MS) {
        restore(app, L"Tempo scaduto. Configurazione precedente ripristinata.");
        return;
    }
    if (elapsed < 450)
        return;
    DisplayConfig actual = {0};
    LONG code = display_read(&actual);
    const DISPLAYCONFIG_PATH_INFO *target = display_find(&actual, app->target);
    bool verified = code == ERROR_SUCCESS &&
                    display_verified(&app->before, &actual, app->target, &display_presets[app->preset]);
    if (code == ERROR_SUCCESS && target)
        show_path(app, &actual, target);
    display_free(&actual);
    if (!verified && elapsed >= SETTLE_MS) {
        restore(app, L"Il segnale richiesto non è stato verificato. Configurazione precedente ripristinata.");
        return;
    }
    if (app->verified != verified) {
        app->verified = verified;
        refresh_controls(app);
    }
    if (verified) {
        wchar_t message[104];
        unsigned long seconds = (unsigned long)((TRIAL_MS - elapsed + 999) / 1000);
        swprintf(message, ARRAYSIZE(message), L"Segnale verificato. Conferma entro %lu s per mantenerlo.", seconds);
        status(app, message);
    }
}

static void keep(App *app)
{
    if (!app->trial || !app->verified)
        return;
    DisplayConfig actual = {0};
    LONG code = display_read(&actual);
    bool verified = code == ERROR_SUCCESS && GetTickCount64() - app->trial_start < TRIAL_MS &&
                    display_verified(&app->before, &actual, app->target, &display_presets[app->preset]);
    if (verified)
        code = display_apply(&actual, true);
    display_free(&actual);
    if (!verified || code != ERROR_SUCCESS) {
        restore(app, L"Impossibile salvare il segnale. Configurazione precedente ripristinata.");
        return;
    }
    finish(app);
    status(app, L"Segnale confermato e salvato nelle impostazioni di Windows.");
}

static INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    App *app = (App *)GetWindowLongPtrW(dialog, DWLP_USER);
    switch (message) {
    case WM_INITDIALOG: {
        app = (App *)lparam;
        app->dialog = dialog;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)app);
        HINSTANCE module = GetModuleHandleW(NULL);
        HICON large = LoadIconW(module, MAKEINTRESOURCEW(IDI_APP));
        HICON small = (HICON)LoadImageW(module, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                          LR_SHARED);
        SendMessageW(dialog, WM_SETICON, ICON_BIG, (LPARAM)large);
        SendMessageW(dialog, WM_SETICON, ICON_SMALL, (LPARAM)small);
        LOGFONTW style;
        HFONT normal = (HFONT)SendDlgItemMessageW(dialog, IDC_PRESET, WM_GETFONT, 0, 0);
        if (GetObjectW(normal, sizeof(style), &style) == sizeof(style)) {
            style.lfWeight = FW_BOLD;
            app->preset_bold = CreateFontIndirectW(&style);
            SendDlgItemMessageW(dialog, IDC_PRESET, CB_SETITEMHEIGHT, 0, abs(style.lfHeight)+5);
        }
        for (unsigned i = 0; i < PRESET_COUNT; ++i)
            SendDlgItemMessageW(dialog, IDC_PRESET, CB_ADDSTRING, 0, (LPARAM)display_presets[i].label);
        SendDlgItemMessageW(dialog, IDC_PRESET, CB_SETCURSEL, PRESET_QHD, 0);
        reload(app, true);
        return TRUE;
    }
    case WM_DRAWITEM:
        if (wparam == IDC_PRESET && app) {
            const DRAWITEMSTRUCT *row = (const DRAWITEMSTRUCT *)lparam;
            HFONT bold = row->itemID < PRESET_COUNT && app->validated[row->itemID] ? app->preset_bold : NULL;
            draw_mode(row, true, bold);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_MONITOR:
            if (HIWORD(wparam) != CBN_SELCHANGE || app->trial)
                return FALSE;
            clear_validation(app);
            show_path(app, &app->current, selected_path(app));
            status(app, L"Seleziona il nuovo segnale, poi premi Verifica o Prova.");
            return TRUE;
        case IDC_REFRESH: if (!app->trial) reload(app, true); return TRUE;
        case IDC_SCAN: {
            const DISPLAYCONFIG_PATH_INFO *path = selected_path(app);
            if (path && !app->trial) {
                clear_validation(app);
                DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_SCAN), dialog, scan_dialog, (LPARAM)app);
                InvalidateRect(GetDlgItem(dialog, IDC_PRESET), NULL, TRUE);
            }
            return TRUE;
        }
        case IDC_VALIDATE: begin(app, true); return TRUE;
        case IDC_TRY: begin(app, false); return TRUE;
        case IDC_RESTORE:
            if (app->trial)
                restore(app, L"Prova annullata. Configurazione precedente ripristinata.");
            return TRUE;
        case IDC_KEEP: keep(app); return TRUE;
        case IDCANCEL:
            if (app->trial)
                restore(app, L"Prova annullata. Configurazione precedente ripristinata.");
            else
                EndDialog(dialog, 0);
            return TRUE;
        }
        break;
    case WM_TIMER:
        if (wparam == TRIAL_TIMER) {
            tick(app);
            return TRUE;
        }
        break;
    case WM_DEVICECHANGE:
    case WM_DISPLAYCHANGE:
        if (app && !app->trial &&
            (message == WM_DISPLAYCHANGE || wparam == DBT_DEVNODES_CHANGED))
            reload(app, false);
        break;
    case WM_CLOSE:
        if (app && app->trial)
            restore(app, L"Prova annullata. Configurazione precedente ripristinata.");
        EndDialog(dialog, 0);
        return TRUE;
    }
    return FALSE;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show)
{
    (void)previous;
    (void)command;
    (void)show;
    HANDLE single = CreateMutexW(NULL, FALSE, L"Local\\SegnaleSchermo.Session");
    if (!single)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(MAKEINTATOM(32770), APP_TITLE);
        if (!existing) existing = FindWindowW(MAKEINTATOM(32770), L"Display Signal Inspector");
        if (!existing) existing = FindWindowW(MAKEINTATOM(32770), L"Segnale Schermo");
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(single);
        return 0;
    }
    App app = {0};
    INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), NULL, dialog_proc, (LPARAM)&app);
    DeleteObject(app.preset_bold);
    display_free(&app.current);
    display_free(&app.before);
    display_free(&app.request);
    CloseHandle(single);
    return result < 0 ? 1 : 0;
}

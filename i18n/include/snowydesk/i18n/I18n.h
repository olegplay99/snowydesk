#pragma once

// All user-visible text goes through this module. No UI string may be
// hard-coded anywhere else: the settings window switches language at runtime
// and calls Tr() again, without restarting the process.

#include "snowydesk/common/Types.h"

namespace snowydesk::i18n {

enum class Str {
    AppTitle,
    AppSubtitle,
    MainDescription,
    BtnClearTaskbar,
    BtnSettings,
    VersionLabel,
    LicenseLink,
    ServiceOnline,
    ServiceOffline,
    SettingsTitle,
    LblLanguage,
    LangRussian,
    LangEnglish,
    LblDensity,
    LblSnowSticks,
    LblGarland,
    LblAutostart,
    LblAutostartService,
    LblTopmost,
    LblTopmostHint,
    BtnApply,
    BtnSave,
    BtnCancel,
    MenuSettings,
    MenuClearSnow,
    MenuAbout,
    MenuExit,
    AboutTitle,
    AboutText,
    TrayHint,
    StatusSaved,
    StatusSaveFailed,
    DensityUnit,
    ClearDone,
};

void SetLanguage(Language language);
Language CurrentLanguage();

// UTF-16 string for the current language. Never null.
const wchar_t* Tr(Str id);

// Borderless-fullscreen caveat shown next to the "above everything" checkbox.
const wchar_t* TopmostCaveat();

}  // namespace snowydesk::i18n

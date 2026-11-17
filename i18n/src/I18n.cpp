#include "snowydesk/i18n/I18n.h"

#include <array>

namespace snowydesk::i18n {
namespace {

using Table = std::array<const wchar_t*, static_cast<std::size_t>(Str::ClearDone) + 1>;

constexpr Table kRussian = {
    L"SnowyDesk",                                                     // AppTitle
    L"Снег на рабочем столе",                                         // AppSubtitle
    L"Прозрачный оверлей рисует снег поверх рабочего стола. "
    L"Клики проходят сквозь оверлей, работа продолжается как обычно.", // MainDescription
    L"Убрать снег с панели задач",                                    // BtnClearTaskbar
    L"Настройки",                                                     // BtnSettings
    L"Версия",                                                        // VersionLabel
    L"Лицензия MIT",                                                  // LicenseLink
    L"Служба SnowyDesk: подключена",                                  // ServiceOnline
    L"Служба SnowyDesk: недоступна (настройки не сохраняются)",       // ServiceOffline
    L"Настройки SnowyDesk",                                           // SettingsTitle
    L"Язык",                                                          // LblLanguage
    L"Русский",                                                       // LangRussian
    L"English",                                                       // LangEnglish
    L"Интенсивность снега",                                           // LblDensity
    L"Снег остаётся на окнах и панели задач",                          // LblSnowSticks
    L"Гирлянда",                                                      // LblGarland
    L"Автозапуск при входе в систему",                                // LblAutostart
    L"Запускать через службу (а не через ключ Run)",                  // LblAutostartService
    L"Поверх ВСЕГО, включая игры",                                    // LblTopmost
    L"Поддерживается только безрамочный/оконный полноэкранный режим",  // LblTopmostHint
    L"Применить",                                                     // BtnApply
    L"Сохранить",                                                     // BtnSave
    L"Отмена",                                                        // BtnCancel
    L"Настройки",                                                     // MenuSettings
    L"Убрать снег",                                                   // MenuClearSnow
    L"О программе",                                                   // MenuAbout
    L"Выход",                                                         // MenuExit
    L"О программе SnowyDesk",                                         // AboutTitle
    L"SnowyDesk 1.0.0\n\nСнег на рабочем столе: прозрачный оверлей, "
    L"служба хранения состояния и гирлянда.\n\nЛицензия MIT, "
    L"Copyright (c) 2026 SnowyDesk contributors.",                     // AboutText
    L"SnowyDesk — двойной щелчок, чтобы открыть меню",                 // TrayHint
    L"Настройки сохранены",                                           // StatusSaved
    L"Не удалось сохранить настройки",                                // StatusSaveFailed
    L"частиц",                                                        // DensityUnit
    L"Снег с панели задач убран",                                     // ClearDone
};

constexpr Table kEnglish = {
    L"SnowyDesk",                                                     // AppTitle
    L"Snow on your desktop",                                          // AppSubtitle
    L"The transparent overlay paints snow above your desktop. "
    L"Clicks pass straight through, so you can keep working.",         // MainDescription
    L"Clear snow from the taskbar",                                   // BtnClearTaskbar
    L"Settings",                                                      // BtnSettings
    L"Version",                                                       // VersionLabel
    L"MIT License",                                                   // LicenseLink
    L"SnowyDesk service: connected",                                   // ServiceOnline
    L"SnowyDesk service: unavailable (settings are not saved)",        // ServiceOffline
    L"SnowyDesk settings",                                            // SettingsTitle
    L"Language",                                                      // LblLanguage
    L"Русский",                                                       // LangRussian
    L"English",                                                       // LangEnglish
    L"Snow intensity",                                                // LblDensity
    L"Snow stays on windows and the taskbar",                          // LblSnowSticks
    L"Garland",                                                       // LblGarland
    L"Start with Windows",                                            // LblAutostart
    L"Start via the service (instead of the Run key)",                 // LblAutostartService
    L"Above EVERYTHING, including games",                             // LblTopmost
    L"Only borderless/windowed fullscreen is supported",               // LblTopmostHint
    L"Apply",                                                         // BtnApply
    L"Save",                                                          // BtnSave
    L"Cancel",                                                        // BtnCancel
    L"Settings",                                                      // MenuSettings
    L"Clear snow",                                                    // MenuClearSnow
    L"About",                                                         // MenuAbout
    L"Exit",                                                          // MenuExit
    L"About SnowyDesk",                                               // AboutTitle
    L"SnowyDesk 1.0.0\n\nSnow on your desktop: a transparent overlay, "
    L"a service that owns the state and a garland.\n\nMIT License, "
    L"Copyright (c) 2026 SnowyDesk contributors.",                     // AboutText
    L"SnowyDesk - double click to open the menu",                      // TrayHint
    L"Settings saved",                                                // StatusSaved
    L"Could not save settings",                                       // StatusSaveFailed
    L"particles",                                                     // DensityUnit
    L"Taskbar snow cleared",                                          // ClearDone
};

Language g_language = Language::Russian;

const Table& ActiveTable() {
    return g_language == Language::English ? kEnglish : kRussian;
}

}  // namespace

void SetLanguage(Language language) {
    g_language = language == Language::English ? Language::English : Language::Russian;
}

Language CurrentLanguage() {
    return g_language;
}

const wchar_t* Tr(Str id) {
    const auto index = static_cast<std::size_t>(id);
    const Table& table = ActiveTable();
    if (index >= table.size()) {
        return L"";
    }
    return table[index];
}

const wchar_t* TopmostCaveat() {
    return Tr(Str::LblTopmostHint);
}

}  // namespace snowydesk::i18n

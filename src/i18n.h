// SPDX-License-Identifier: GPL-3.0-or-later
// Translation helpers.
//
// The catalogue lives in the applet package:
//
//     package/contents/locale/<lang>/LC_MESSAGES/plasma_applet_org.moondrop.control.mo
//
// Plasma registers that directory for the applet's translation domain when the
// applet is loaded, so i18nd() below resolves inside the widget.  The command
// line tool registers the directory itself (see cli/main.cpp) and otherwise
// falls back to the untranslated source strings.
#pragma once

#include <KLocalizedString>
#include <QString>

namespace Moondrop {

// Plasma derives the domain from the applet id: "plasma_applet_" + id
inline constexpr char TranslationDomain[] = "plasma_applet_org.moondrop.control";

// Translate a user visible string from the applet's own catalogue.
inline QString moondropTr(const char *text)
{
    return i18nd(TranslationDomain, text);
}

} // namespace Moondrop

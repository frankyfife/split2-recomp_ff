// The launcher's two languages: the English text is the key, German comes
// from the table in tr.cpp. German is the default when the system is German
// (or the settings file says launcher_language = de).
#pragma once

#include <QString>

void setUiGerman(bool german);
bool uiGerman();
QString T(const char *english);

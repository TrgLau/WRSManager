#pragma once

#include <wx/colour.h>
#include <wx/string.h>
#include <wx/textctrl.h>

enum class LogLevel {
    Plain,
    Info,
    Debug,
    Warning,
    Error,
    Success,
    Note
};

struct LoggerTheme {
    wxColour background = wxColour(0, 0, 0);
    wxColour plain = wxColour(255, 255, 255);
    wxColour info = wxColour(0, 255, 0);
    wxColour debug = wxColour(150, 150, 150);
    wxColour warning = wxColour(255, 200, 0);
    wxColour error = wxColour(255, 90, 90);
    wxColour success = wxColour(0, 255, 0);
    wxColour note = wxColour(120, 190, 255);
};

class TerminalLogger {
public:
    explicit TerminalLogger(wxTextCtrl* output);

    void SetTheme(const LoggerTheme& theme);
    void SetLevelColor(LogLevel level, const wxColour& color);
    void Log(LogLevel level, const wxString& text);

private:
    wxColour ColorFor(LogLevel level) const;

    wxTextCtrl* m_output = nullptr;
    LoggerTheme m_theme;
};


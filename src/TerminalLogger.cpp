#include "TerminalLogger.h"

TerminalLogger::TerminalLogger(wxTextCtrl* output) : m_output(output) {
    if (!m_output) {
        return;
    }

    m_output->SetEditable(false);
    m_output->SetBackgroundColour(m_theme.background);
    m_output->SetForegroundColour(m_theme.plain);
    m_output->SetDefaultStyle(wxTextAttr(m_theme.plain, m_theme.background));
    m_output->Refresh();
}

void TerminalLogger::SetTheme(const LoggerTheme& theme) {
    m_theme = theme;
    if (!m_output) {
        return;
    }

    m_output->SetBackgroundColour(m_theme.background);
    m_output->SetForegroundColour(m_theme.plain);
    m_output->SetDefaultStyle(wxTextAttr(m_theme.plain, m_theme.background));
    m_output->Refresh();
}

void TerminalLogger::SetLevelColor(LogLevel level, const wxColour& color) {
    switch (level) {
        case LogLevel::Plain:
            m_theme.plain = color;
            break;
        case LogLevel::Info:
            m_theme.info = color;
            break;
        case LogLevel::Debug:
            m_theme.debug = color;
            break;
        case LogLevel::Warning:
            m_theme.warning = color;
            break;
        case LogLevel::Error:
            m_theme.error = color;
            break;
        case LogLevel::Success:
            m_theme.success = color;
            break;
        case LogLevel::Note:
            m_theme.note = color;
            break;
    }
}

void TerminalLogger::Log(LogLevel level, const wxString& text) {
    if (!m_output) {
        return;
    }

    const wxColour textColor = ColorFor(level);
    m_output->SetInsertionPointEnd();
    const long startPos = m_output->GetLastPosition();
    wxTextAttr lineStyle(textColor, m_theme.background);
    m_output->SetDefaultStyle(lineStyle);
    m_output->AppendText(text);
    const long endPos = m_output->GetLastPosition();
    m_output->SetStyle(startPos, endPos, lineStyle);
    m_output->SetDefaultStyle(wxTextAttr(m_theme.plain, m_theme.background));
    m_output->ShowPosition(m_output->GetLastPosition());
}

wxColour TerminalLogger::ColorFor(LogLevel level) const {
    switch (level) {
        case LogLevel::Plain:
            return m_theme.plain;
        case LogLevel::Info:
            return m_theme.info;
        case LogLevel::Debug:
            return m_theme.debug;
        case LogLevel::Warning:
            return m_theme.warning;
        case LogLevel::Error:
            return m_theme.error;
        case LogLevel::Success:
            return m_theme.success;
        case LogLevel::Note:
            return m_theme.note;
    }
    return m_theme.plain;
}


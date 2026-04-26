#include "MainFrame.h"

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#endif

#include <wx/aboutdlg.h>
#include <wx/dirdlg.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>

#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <functional>
#include <thread>
#include <vector>

namespace {
std::string TrimLeft(std::string s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    return s.substr(i);
}

bool StartsWithCaseInsensitive(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[i])));
        if (a != b) return false;
    }
    return true;
}

bool ContainsCaseInsensitiveAscii(const std::string& haystack, const std::string& needle) {
    if (needle.empty() || haystack.size() < needle.size()) {
        return false;
    }
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool matches = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            const char a =
                static_cast<char>(std::tolower(static_cast<unsigned char>(haystack[i + j])));
            const char b =
                static_cast<char>(std::tolower(static_cast<unsigned char>(needle[j])));
            if (a != b) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

bool ContainsBootstrapMarker(const std::string& haystack) {
    static const std::string kMarker =
        "R5LogWater: Water landscape global memory statistics";
    static const std::string kMarkerWithColon = kMarker + ":";
    if (ContainsCaseInsensitiveAscii(haystack, kMarker) ||
        ContainsCaseInsensitiveAscii(haystack, kMarkerWithColon)) {
        return true;
    }

    auto toUtf16LeAscii = [](const std::string& s) {
        std::string utf16;
        utf16.reserve(s.size() * 2);
        for (char c : s) {
            utf16.push_back(c);
            utf16.push_back('\0');
        }
        return utf16;
    };
    const std::string markerUtf16 = toUtf16LeAscii(kMarker);
    const std::string markerUtf16WithColon = toUtf16LeAscii(kMarkerWithColon);
    return ContainsCaseInsensitiveAscii(haystack, markerUtf16) ||
           ContainsCaseInsensitiveAscii(haystack, markerUtf16WithColon);
}

std::string TransformStartLineToCall(const std::string& line) {
    std::string trimmed = TrimLeft(line);
    bool hadAtPrefix = false;
    if (!trimmed.empty() && trimmed.front() == '@') {
        hadAtPrefix = true;
        trimmed = TrimLeft(trimmed.substr(1));
    }

    if (!StartsWithCaseInsensitive(trimmed, "start ")) {
        return line;
    }

    std::string rest = TrimLeft(trimmed.substr(5));
    size_t i = 0;

    auto skipSpaces = [&]() {
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) {
            ++i;
        }
    };
    auto parseToken = [&]() -> std::string {
        skipSpaces();
        if (i >= rest.size()) return "";
        if (rest[i] == '"') {
            const size_t start = i++;
            while (i < rest.size() && rest[i] != '"') ++i;
            if (i < rest.size()) ++i;
            return rest.substr(start, i - start);
        }
        const size_t start = i;
        while (i < rest.size() && rest[i] != ' ' && rest[i] != '\t') ++i;
        return rest.substr(start, i - start);
    };

    std::string tok = parseToken();
    while (!tok.empty() && tok[0] == '/') {
        tok = parseToken();
    }

    if (!tok.empty() && tok.size() >= 2 && tok.front() == '"' && tok.back() == '"') {
        tok = parseToken();
    }

    if (tok.empty()) {
        return line;
    }

    const size_t cmdStart = rest.find(tok);
    if (cmdStart == std::string::npos) {
        return line;
    }
    std::string rewritten = std::string("call ") + rest.substr(cmdStart);
    if (hadAtPrefix) {
        rewritten = "@" + rewritten;
    }
    return rewritten;
}

std::string CreatePatchedBatchForCapture(const std::string& batPath) {
    std::ifstream in(batPath);
    if (!in.is_open()) {
        return batPath;
    }

    const std::filesystem::path srcPath(batPath);
    const std::filesystem::path patchedPath =
        srcPath.parent_path() / "__wrs_startserver_capture.bat";
    std::ofstream out(patchedPath.string(), std::ios::trunc);
    if (!out.is_open()) {
        return batPath;
    }

    std::string line;
    while (std::getline(in, line)) {
        const std::string transformed = TransformStartLineToCall(line);
        out << transformed << "\n";
    }
    return patchedPath.string();
}
}

enum : int {
    ID_Quit = wxID_EXIT,
    ID_About = wxID_ABOUT,
};

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(ID_Quit, MainFrame::OnExit)
    EVT_MENU(ID_About, MainFrame::OnAbout)
wxEND_EVENT_TABLE()

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, wxT("WRSManager"),
              wxDefaultPosition, wxSize(832, 780)),
      m_backupTimer(this) {
    Bind(wxEVT_TIMER, &MainFrame::OnBackupTimer, this, m_backupTimer.GetId());
    auto* menuFile = new wxMenu();
    menuFile->Append(ID_Quit, wxT("&Quit\tAlt-F4"));

    auto* menuHelp = new wxMenu();
    menuHelp->Append(ID_About, wxT("&About"));

    auto* menuBar = new wxMenuBar();
    menuBar->Append(menuFile, wxT("&File"));
    menuBar->Append(menuHelp, wxT("&Help"));
    SetMenuBar(menuBar);

    m_notebook = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);
    m_notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &MainFrame::OnNotebookPageChanged, this);
    m_notebook->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnNotebookLeftClick, this);

    m_plusPage = new wxPanel(m_notebook);
    m_notebook->AddPage(m_plusPage, wxT("+"), true);

    LoadAppState();
    EnsureBaseInstallDir();

    auto* rootSizer = new wxBoxSizer(wxVERTICAL);
    rootSizer->Add(m_notebook, 1, wxEXPAND);
    SetSizer(rootSizer);

    CreateStatusBar();
    SetStatusText(wxT("WRSManager"));
}

bool MainFrame::EnsureBaseInstallDir() {
    if (!m_lastInstallDir.IsEmpty()) {
        return true;
    }

    wxDirDialog dlg(this, wxT("Select base installation folder"),
                    wxEmptyString, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) {
        return false;
    }
    m_lastInstallDir = dlg.GetPath();
    SaveAppState();
    return true;
}

bool MainFrame::CreateTab(const wxString& tabName) {
    if (m_lastInstallDir.IsEmpty()) {
        return false;
    }

    const wxString tabDirWx = m_lastInstallDir + wxFILE_SEP_PATH + tabName;
    std::error_code ec;
    std::filesystem::create_directories(tabDirWx.ToStdString(), ec);
    if (ec) {
        return false;
    }

    const int plusIndex = static_cast<int>(m_notebook->GetPageCount()) - 1;
    auto* page = new wxPanel(m_notebook);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* configBox = new wxStaticBoxSizer(wxVERTICAL, page, wxT("WorldDescription.json"));
    auto* formColumns = new wxBoxSizer(wxHORIZONTAL);
    auto* formLeft = new wxFlexGridSizer(2, 7, 8);
    auto* formRight = new wxFlexGridSizer(2, 7, 8);
    formLeft->AddGrowableCol(1, 1);
    formRight->AddGrowableCol(1, 1);

    m_worldIslandIdCtrl = new wxTextCtrl(page, wxID_ANY);
    m_worldNameCtrl = new wxTextCtrl(page, wxID_ANY);
    m_worldPresetChoice = new wxChoice(page, wxID_ANY);
    m_worldPresetChoice->Append(wxT("Small"));
    m_worldPresetChoice->Append(wxT("Medium"));
    m_worldPresetChoice->Append(wxT("Large"));
    m_worldPresetChoice->SetStringSelection(wxT("Medium"));
    m_sharedQuestsChk = new wxCheckBox(page, wxID_ANY, wxT("Enabled"));
    m_easyExploreChk = new wxCheckBox(page, wxID_ANY, wxT("Enabled"));
    m_mobHealthMultiplierCtrl = new wxTextCtrl(page, wxID_ANY, wxT("1"));
    m_mobDamageMultiplierCtrl = new wxTextCtrl(page, wxID_ANY, wxT("1"));
    m_shipsHealthMultiplierCtrl = new wxTextCtrl(page, wxID_ANY, wxT("1"));
    m_shipsDamageMultiplierCtrl = new wxTextCtrl(page, wxID_ANY, wxT("1"));
    m_boardingDifficultyMultiplierCtrl = new wxTextCtrl(page, wxID_ANY, wxT("1"));
    m_coopStatsCorrectionCtrl = new wxTextCtrl(page, wxID_ANY, wxT("1"));
    m_coopShipStatsCorrectionCtrl = new wxTextCtrl(page, wxID_ANY, wxT("0"));
    m_combatDifficultyChoice = new wxChoice(page, wxID_ANY);
    m_combatDifficultyChoice->Append(wxT("Easy"));
    m_combatDifficultyChoice->Append(wxT("Normal"));
    m_combatDifficultyChoice->Append(wxT("Hard"));
    m_combatDifficultyChoice->SetStringSelection(wxT("Normal"));
    m_inviteCodeCtrl = new wxTextCtrl(page, wxID_ANY);
    m_passwordProtectedChk = new wxCheckBox(page, wxID_ANY, wxT("Enabled"));
    m_passwordCtrl = new wxTextCtrl(page, wxID_ANY);
    m_serverNameCtrl = new wxTextCtrl(page, wxID_ANY);
    m_maxPlayerCountCtrl = new wxSpinCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                          wxDefaultSize, wxSP_ARROW_KEYS, 1, 128, 1);
    m_userSelectedRegionCtrl = new wxTextCtrl(page, wxID_ANY);
    m_useDirectConnectionChk = new wxCheckBox(page, wxID_ANY, wxT("Enabled"));
    m_directConnectionServerAddressCtrl = new wxTextCtrl(page, wxID_ANY);
    m_directConnectionServerPortCtrl = new wxSpinCtrl(page, wxID_ANY, wxEmptyString,
                                                      wxDefaultPosition, wxDefaultSize,
                                                      wxSP_ARROW_KEYS, -1, 65535, -1);
    m_directConnectionProxyAddressCtrl = new wxTextCtrl(page, wxID_ANY, wxT("0.0.0.0"));

    formLeft->Add(new wxStaticText(page, wxID_ANY, wxT("WorldName")), 0, wxALIGN_CENTER_VERTICAL);
    formLeft->Add(m_worldNameCtrl, 1, wxEXPAND);
    formLeft->Add(new wxStaticText(page, wxID_ANY, wxT("WorldPresetType")), 0, wxALIGN_CENTER_VERTICAL);
    formLeft->Add(m_worldPresetChoice, 1, wxEXPAND);
    formLeft->Add(new wxStaticText(page, wxID_ANY, wxT("Coop.SharedQuests")), 0, wxALIGN_CENTER_VERTICAL);
    formLeft->Add(m_sharedQuestsChk, 1, wxEXPAND);
    formLeft->Add(new wxStaticText(page, wxID_ANY, wxT("EasyExplore")), 0, wxALIGN_CENTER_VERTICAL);
    formLeft->Add(m_easyExploreChk, 1, wxEXPAND);
    formLeft->Add(new wxStaticText(page, wxID_ANY, wxT("MobHealthMultiplier")), 0, wxALIGN_CENTER_VERTICAL);
    formLeft->Add(m_mobHealthMultiplierCtrl, 1, wxEXPAND);
    formLeft->Add(new wxStaticText(page, wxID_ANY, wxT("MobDamageMultiplier")), 0, wxALIGN_CENTER_VERTICAL);
    formLeft->Add(m_mobDamageMultiplierCtrl, 1, wxEXPAND);
    formLeft->Add(new wxStaticText(page, wxID_ANY, wxT("ShipsHealthMultiplier")), 0, wxALIGN_CENTER_VERTICAL);
    formLeft->Add(m_shipsHealthMultiplierCtrl, 1, wxEXPAND);
    formLeft->Add(new wxStaticText(page, wxID_ANY, wxT("ShipsDamageMultiplier")), 0, wxALIGN_CENTER_VERTICAL);
    formLeft->Add(m_shipsDamageMultiplierCtrl, 1, wxEXPAND);
    formLeft->Add(new wxStaticText(page, wxID_ANY, wxT("BoardingDifficultyMultiplier")), 0, wxALIGN_CENTER_VERTICAL);
    formLeft->Add(m_boardingDifficultyMultiplierCtrl, 1, wxEXPAND);
    formLeft->Add(new wxStaticText(page, wxID_ANY, wxT("Coop.StatsCorrectionModifier")), 0, wxALIGN_CENTER_VERTICAL);
    formLeft->Add(m_coopStatsCorrectionCtrl, 1, wxEXPAND);
    formLeft->Add(new wxStaticText(page, wxID_ANY, wxT("Coop.ShipStatsCorrectionModifier")), 0, wxALIGN_CENTER_VERTICAL);
    formLeft->Add(m_coopShipStatsCorrectionCtrl, 1, wxEXPAND);
    formLeft->Add(new wxStaticText(page, wxID_ANY, wxT("CombatDifficulty")), 0, wxALIGN_CENTER_VERTICAL);
    formLeft->Add(m_combatDifficultyChoice, 1, wxEXPAND);

    formRight->Add(new wxStaticText(page, wxID_ANY, wxT("InviteCode")), 0, wxALIGN_CENTER_VERTICAL);
    formRight->Add(m_inviteCodeCtrl, 1, wxEXPAND);
    formRight->Add(new wxStaticText(page, wxID_ANY, wxT("IsPasswordProtected")), 0, wxALIGN_CENTER_VERTICAL);
    formRight->Add(m_passwordProtectedChk, 1, wxEXPAND);
    formRight->Add(new wxStaticText(page, wxID_ANY, wxT("Password")), 0, wxALIGN_CENTER_VERTICAL);
    formRight->Add(m_passwordCtrl, 1, wxEXPAND);
    formRight->Add(new wxStaticText(page, wxID_ANY, wxT("ServerName")), 0, wxALIGN_CENTER_VERTICAL);
    formRight->Add(m_serverNameCtrl, 1, wxEXPAND);
    formRight->Add(new wxStaticText(page, wxID_ANY, wxT("MaxPlayerCount")), 0, wxALIGN_CENTER_VERTICAL);
    formRight->Add(m_maxPlayerCountCtrl, 1, wxEXPAND);
    formRight->Add(new wxStaticText(page, wxID_ANY, wxT("UserSelectedRegion")), 0, wxALIGN_CENTER_VERTICAL);
    formRight->Add(m_userSelectedRegionCtrl, 1, wxEXPAND);
    formRight->Add(new wxStaticText(page, wxID_ANY, wxT("UseDirectConnection")), 0, wxALIGN_CENTER_VERTICAL);
    formRight->Add(m_useDirectConnectionChk, 1, wxEXPAND);
    formRight->Add(new wxStaticText(page, wxID_ANY, wxT("DirectConnectionServerAddress")), 0, wxALIGN_CENTER_VERTICAL);
    formRight->Add(m_directConnectionServerAddressCtrl, 1, wxEXPAND);
    formRight->Add(new wxStaticText(page, wxID_ANY, wxT("DirectConnectionServerPort")), 0, wxALIGN_CENTER_VERTICAL);
    formRight->Add(m_directConnectionServerPortCtrl, 1, wxEXPAND);
    formRight->Add(new wxStaticText(page, wxID_ANY, wxT("DirectConnectionProxyAddress")), 0, wxALIGN_CENTER_VERTICAL);
    formRight->Add(m_directConnectionProxyAddressCtrl, 1, wxEXPAND);

    formColumns->Add(formLeft, 1, wxRIGHT, 16);
    formColumns->Add(formRight, 1, wxLEFT, 16);
    configBox->Add(formColumns, 1, wxEXPAND | wxALL, 8);

    auto* output = new wxTextCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                  wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxBORDER_THEME);
    output->SetFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));

    auto* validateJsonBtn = new wxButton(page, wxID_ANY, wxT("Valider"));
    validateJsonBtn->Bind(wxEVT_BUTTON, &MainFrame::OnValidateJsonClicked, this);
    configBox->Add(validateJsonBtn, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* installBtn = new wxButton(page, wxID_ANY, wxT("1. Install SteamCMD"));
    auto* serverBtn = new wxButton(page, wxID_ANY, wxT("2. Install Server"));
    auto* startBtn = new wxButton(page, wxID_ANY, wxT("3. StartServer"));
    auto* stopBtn = new wxButton(page, wxID_ANY, wxT("4. StopServer"));
    auto* autoBackupChk = new wxCheckBox(page, wxID_ANY, wxT("AutoBackup"));
    auto* backupIntervalChoice = new wxChoice(page, wxID_ANY);
    auto* maxBackupsCtrl = new wxSpinCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                          wxSize(70, -1), wxSP_ARROW_KEYS, 1, 999, 10);
    backupIntervalChoice->Append(wxT("5 min"));
    backupIntervalChoice->Append(wxT("10 min"));
    backupIntervalChoice->Append(wxT("15 min"));
    backupIntervalChoice->Append(wxT("30 min"));
    backupIntervalChoice->Append(wxT("60 min"));
    backupIntervalChoice->SetSelection(2);
    installBtn->Bind(wxEVT_BUTTON, &MainFrame::OnInstallClicked, this);
    serverBtn->Bind(wxEVT_BUTTON, &MainFrame::OnInstallServerClicked, this);
    startBtn->Bind(wxEVT_BUTTON, &MainFrame::OnStartServerClicked, this);
    stopBtn->Bind(wxEVT_BUTTON, &MainFrame::OnStopServerClicked, this);
    autoBackupChk->Bind(wxEVT_CHECKBOX, &MainFrame::OnAutoBackupChanged, this);
    backupIntervalChoice->Bind(wxEVT_CHOICE, &MainFrame::OnBackupIntervalChanged, this);
    buttonSizer->Add(installBtn, 0, wxRIGHT, 8);
    buttonSizer->Add(serverBtn, 0, wxRIGHT, 8);
    buttonSizer->Add(startBtn, 0, wxRIGHT, 8);
    buttonSizer->Add(stopBtn, 0, wxRIGHT, 8);
    buttonSizer->AddStretchSpacer(1);
    buttonSizer->Add(autoBackupChk, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    buttonSizer->Add(backupIntervalChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    buttonSizer->Add(new wxStaticText(page, wxID_ANY, wxT("MaxBackups")), 0,
                     wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    buttonSizer->Add(maxBackupsCtrl, 0, wxALIGN_CENTER_VERTICAL);

    sizer->Add(configBox, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    sizer->Add(output, 1, wxEXPAND | wxALL, 8);
    sizer->Add(buttonSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    page->SetSizer(sizer);

    m_notebook->InsertPage(plusIndex, page, tabName, true);

    m_terminalOutput = output;
    m_installButton = installBtn;
    m_installServerButton = serverBtn;
    m_startServerButton = startBtn;
    m_stopServerButton = stopBtn;
    m_validateJsonButton = validateJsonBtn;
    m_autoBackupChk = autoBackupChk;
    m_backupIntervalChoice = backupIntervalChoice;
    m_maxBackupsCtrl = maxBackupsCtrl;
    m_logger = std::make_unique<TerminalLogger>(m_terminalOutput);
    m_logger->SetLevelColor(LogLevel::Plain, wxColour(255, 255, 255));
    m_logger->SetLevelColor(LogLevel::Info, wxColour(255, 255, 255));
    m_logger->SetLevelColor(LogLevel::Debug, wxColour(180, 180, 180));
    m_logger->SetLevelColor(LogLevel::Warning, wxColour(255, 200, 0));
    m_logger->SetLevelColor(LogLevel::Error, wxColour(255, 90, 90));
    m_logger->SetLevelColor(LogLevel::Success, wxColour(0, 255, 0));
    m_logger->SetLevelColor(LogLevel::Note, wxColour(120, 190, 255));
    m_logger->Log(LogLevel::Info, wxString::Format(wxT("[INFO] Tab created: %s\n"), tabName));
    m_logger->Log(LogLevel::Note, wxString::Format(wxT("[NOTE] Install folder: %s\n"), tabDirWx));
    SetJsonControlsEnabled(false);
    LoadServerDescriptionToControls();
    SetBusyButtons(false);
    UpdateBackupScheduler();
    return true;
}

void MainFrame::OnNotebookPageChanged(wxBookCtrlEvent& event) {
    event.Skip();
}

void MainFrame::OnNotebookLeftClick(wxMouseEvent& event) {
    long flags = 0;
    const int tabIdx = m_notebook->HitTest(event.GetPosition(), &flags);
    if (tabIdx == wxNOT_FOUND) {
        event.Skip();
        return;
    }
    const int last = static_cast<int>(m_notebook->GetPageCount()) - 1;
    if (tabIdx == last && m_notebook->GetPageText(tabIdx) == wxT("+")) {
        wxTheApp->CallAfter([this]() { AppendNewTabAfterPlus(); });
    }
    event.Skip();
}


void MainFrame::AppendNewTabAfterPlus() {
    if (m_lastInstallDir.IsEmpty()) {
        if (m_logger) {
            m_logger->Log(LogLevel::Error,
                          wxT("[ERROR] Base install folder is not set. Restart app and choose a folder.\n"));
        }
        return;
    }

    wxTextEntryDialog dlg(this, wxT("Server name:"), wxT("New server"),
                          wxT("Server 1"));
    if (dlg.ShowModal() != wxID_OK) {
        return;
    }
    const wxString tabName = dlg.GetValue().Trim().Trim(false);
    if (tabName.IsEmpty()) {
        return;
    }

    if (!CreateTab(tabName)) {
        return;
    }
    ++m_nextTabNum;
}

void MainFrame::OnExit(wxCommandEvent&) {
    Close(true);
}

void MainFrame::OnAbout(wxCommandEvent&) {
    wxAboutDialogInfo info;
    info.SetName(wxT("WRSManager"));
    info.SetVersion(wxT("0.1"));
    info.SetDescription(wxT("WRSManager application (WxWidgets)."));
    wxAboutBox(info, this);
}

void MainFrame::OnInstallClicked(wxCommandEvent&) {
    const int sel = m_notebook->GetSelection();
    if (sel == wxNOT_FOUND || m_notebook->GetPage(sel) == m_plusPage) {
        return;
    }
    const wxString tabName = m_notebook->GetPageText(sel);
    const wxString installDirWx = m_lastInstallDir + wxFILE_SEP_PATH + tabName;
#ifdef _WIN32
    SetBusyButtons(true);
    m_logger->Log(LogLevel::Info, wxT("[INFO] SteamCMD installation requested.\n"));

    std::thread([this, installDirWx]() {
        const std::string installDir = installDirWx.ToStdString();
        const std::string steamRoot = installDir + "\\steamcmd";
        const std::string zipPath = steamRoot + "\\steamcmd.zip";
        const std::string steamcmdExe = steamRoot + "\\steamcmd.exe";
        const std::string steamcmdUrl =
            "https://steamcdn-a.akamaihd.net/client/installer/steamcmd.zip";

        auto toWx = [](const std::string& s) { return wxString::FromUTF8(s.c_str()); };

        auto quotePs = [](std::string s) {
            size_t pos = 0;
            while ((pos = s.find('\'', pos)) != std::string::npos) {
                s.replace(pos, 1, "''");
                pos += 2;
            }
            return s;
        };

        auto runProcessAndCapture =
            [](std::string commandLine, const char* cwd, DWORD* outExitCode,
               const std::function<void(const std::string&)>& onChunk) -> bool {
            SECURITY_ATTRIBUTES sa = {};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;
            HANDLE readPipe = nullptr;
            HANDLE writePipe = nullptr;
            if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
                return false;
            }
            SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = writePipe;
            si.hStdError = writePipe;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            PROCESS_INFORMATION pi = {};
            std::vector<char> buf(commandLine.begin(), commandLine.end());
            buf.push_back('\0');

            if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                nullptr, cwd, &si, &pi)) {
                CloseHandle(writePipe);
                CloseHandle(readPipe);
                return false;
            }

            CloseHandle(writePipe);

            char outBuf[4096];
            DWORD bytesRead = 0;
            while (ReadFile(readPipe, outBuf, sizeof(outBuf) - 1, &bytesRead, nullptr) &&
                   bytesRead > 0) {
                outBuf[bytesRead] = '\0';
                if (onChunk) {
                    onChunk(std::string(outBuf, bytesRead));
                }
            }

            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode = 1;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(readPipe);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (outExitCode) {
                *outExitCode = exitCode;
            }
            return true;
        };

        std::error_code fsErr;
        const bool alreadyExists = std::filesystem::exists(steamRoot, fsErr);
        if (fsErr) {
            this->CallAfter([this, msg = toWx(fsErr.message())]() {
                m_logger->Log(LogLevel::Error,
                              wxString::Format(wxT("[ERROR] Unable to check steamcmd folder: %s\n"), msg));
                SetBusyButtons(false);
            });
            return;
        }
        if (alreadyExists) {
            const auto removed = std::filesystem::remove_all(steamRoot, fsErr);
            if (fsErr) {
                this->CallAfter([this, msg = toWx(fsErr.message())]() {
                    m_logger->Log(LogLevel::Error,
                                  wxString::Format(wxT("[ERROR] Unable to remove steamcmd folder: %s\n"), msg));
                    m_installButton->Enable(true);
                    m_installServerButton->Enable(m_steamcmdInstalled);
                });
                return;
            }
            this->CallAfter([this, removed]() {
                m_logger->Log(LogLevel::Warning,
                              wxString::Format(wxT("[INFO] Existing steamcmd folder removed (%llu entries).\n"),
                                               static_cast<unsigned long long>(removed)));
            });
        }
        std::filesystem::create_directories(steamRoot, fsErr);
        if (fsErr) {
            this->CallAfter([this, msg = toWx(fsErr.message())]() {
                m_logger->Log(LogLevel::Error,
                              wxString::Format(wxT("[ERROR] Unable to create steamcmd folder: %s\n"), msg));
                m_installButton->Enable(true);
                m_installServerButton->Enable(m_steamcmdInstalled);
            });
            return;
        }

        this->CallAfter([this, installDirWx, steamRoot = toWx(steamRoot), zipPath = toWx(zipPath)]() {
            m_logger->Log(LogLevel::Note, wxString::Format(wxT("[NOTE] Target folder: %s\n"), installDirWx));
            m_logger->Log(LogLevel::Note, wxString::Format(wxT("[NOTE] steamcmd subfolder: %s\n"), steamRoot));
            m_logger->Log(LogLevel::Note, wxString::Format(wxT("[NOTE] Zip target path: %s\n"), zipPath));
            m_logger->Log(LogLevel::Info, wxT("[INFO] Downloading steamcmd.zip...\n"));
        });

        HINTERNET hInternet = InternetOpenA("WRSManager", INTERNET_OPEN_TYPE_PRECONFIG,
                                            nullptr, nullptr, 0);
        if (!hInternet) {
            const DWORD err = GetLastError();
            this->CallAfter([this, err]() {
                m_logger->Log(LogLevel::Error,
                              wxString::Format(wxT("[ERROR] InternetOpenA failed (code %lu).\n"),
                                               static_cast<unsigned long>(err)));
                m_installButton->Enable(true);
                m_installServerButton->Enable(m_steamcmdInstalled);
            });
            return;
        }

        HINTERNET hUrl =
            InternetOpenUrlA(hInternet, steamcmdUrl.c_str(), nullptr, 0,
                             INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (!hUrl) {
            const DWORD err = GetLastError();
            InternetCloseHandle(hInternet);
            this->CallAfter([this, err]() {
                m_logger->Log(LogLevel::Error,
                              wxString::Format(wxT("[ERROR] InternetOpenUrlA failed (code %lu).\n"),
                                               static_cast<unsigned long>(err)));
                m_installButton->Enable(true);
                m_installServerButton->Enable(m_steamcmdInstalled);
            });
            return;
        }

        HANDLE hOutFile =
            CreateFileA(zipPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hOutFile == INVALID_HANDLE_VALUE) {
            const DWORD err = GetLastError();
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            this->CallAfter([this, err]() {
                m_logger->Log(LogLevel::Error,
                              wxString::Format(wxT("[ERROR] Unable to create zip file (code %lu).\n"),
                                               static_cast<unsigned long>(err)));
                m_installButton->Enable(true);
                m_installServerButton->Enable(m_steamcmdInstalled);
            });
            return;
        }

        DWORD contentLength = 0;
        DWORD contentLengthSize = sizeof(contentLength);
        const bool hasContentLength =
            HttpQueryInfoA(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
                           &contentLength, &contentLengthSize, nullptr) == TRUE &&
            contentLength > 0;
        this->CallAfter([this, hasContentLength, contentLength]() {
            if (hasContentLength) {
                m_logger->Log(LogLevel::Note,
                              wxString::Format(wxT("[DOWNLOAD] Reported size: %lu bytes\n"),
                                               static_cast<unsigned long>(contentLength)));
            } else {
                m_logger->Log(LogLevel::Note, wxT("[DOWNLOAD] Unknown size (no Content-Length)\n"));
            }
        });

        std::vector<char> dlBuf(64 * 1024);
        DWORD bytesRead = 0;
        DWORD bytesWritten = 0;
        unsigned long long totalRead = 0;
        unsigned long long lastLoggedBytes = 0;
        int lastPercent = -1;
        const auto downloadStart = std::chrono::steady_clock::now();
        while (InternetReadFile(hUrl, dlBuf.data(), static_cast<DWORD>(dlBuf.size()), &bytesRead) &&
               bytesRead > 0) {
            if (!WriteFile(hOutFile, dlBuf.data(), bytesRead, &bytesWritten, nullptr) ||
                bytesWritten != bytesRead) {
                const DWORD err = GetLastError();
                CloseHandle(hOutFile);
                InternetCloseHandle(hUrl);
                InternetCloseHandle(hInternet);
                this->CallAfter([this, err]() {
                    m_logger->Log(LogLevel::Error,
                                  wxString::Format(wxT("[ERROR] Zip write failed (code %lu).\n"),
                                                   static_cast<unsigned long>(err)));
                    m_installButton->Enable(true);
                    m_installServerButton->Enable(m_steamcmdInstalled);
                });
                return;
            }

            totalRead += bytesRead;
            if (hasContentLength) {
                const int percent = static_cast<int>((totalRead * 100ULL) / contentLength);
                if (percent >= lastPercent + 1 || percent == 100) {
                    lastPercent = percent;
                    this->CallAfter([this, percent, totalRead, contentLength]() {
                        m_logger->Log(LogLevel::Note,
                                      wxString::Format(wxT("[DOWNLOAD] %d%% (%llu/%lu)\n"), percent,
                                                       static_cast<unsigned long long>(totalRead),
                                                       static_cast<unsigned long>(contentLength)));
                    });
                }
            } else if (totalRead - lastLoggedBytes >= (1024ULL * 1024ULL)) {
                lastLoggedBytes = totalRead;
                this->CallAfter([this, totalRead]() {
                    m_logger->Log(LogLevel::Note,
                                  wxString::Format(wxT("[DOWNLOAD] %llu bytes received\n"),
                                                   static_cast<unsigned long long>(totalRead)));
                });
            }
        }
        CloseHandle(hOutFile);
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);

        const auto downloadEnd = std::chrono::steady_clock::now();
        const auto downloadMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(downloadEnd - downloadStart)
                .count();
        this->CallAfter([this, totalRead, downloadMs]() {
            m_logger->Log(LogLevel::Note,
                          wxString::Format(wxT("[DOWNLOAD] 100%% (%llu bytes in %lld ms)\n"),
                                           static_cast<unsigned long long>(totalRead),
                                           static_cast<long long>(downloadMs)));
        });

        this->CallAfter([this]() {
            m_logger->Log(LogLevel::Info, wxT("[INFO] Extracting steamcmd.zip...\n"));
            m_logger->Log(LogLevel::Note, wxT("[INFO] Unzip progress format: [UNZIP] current/total file\n"));
        });

        const std::string psCmd =
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"$ErrorActionPreference='Stop';"
            "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
            "$zip='" + quotePs(zipPath) + "';"
            "$dest='" + quotePs(steamRoot) + "';"
            "$archive=[System.IO.Compression.ZipFile]::OpenRead($zip);"
            "try {"
            "$entries=@($archive.Entries | Where-Object { $_.FullName -and $_.FullName -ne '' });"
            "$total=$entries.Count;"
            "$i=0;"
            "foreach($e in $entries){"
            "$target=Join-Path $dest $e.FullName;"
            "if($e.FullName.EndsWith('/')){"
            "New-Item -ItemType Directory -Force -Path $target | Out-Null"
            "} else {"
            "$dir=Split-Path -Parent $target;"
            "if($dir){ New-Item -ItemType Directory -Force -Path $dir | Out-Null };"
            "[System.IO.Compression.ZipFileExtensions]::ExtractToFile($e,$target,$true)"
            "};"
            "$i++;"
            "Write-Output ('UNZIP_PROGRESS:{0}:{1}:{2}' -f $i,$total,$e.FullName)"
            "}"
            "} finally { $archive.Dispose() }\"";
        DWORD unzipExitCode = 1;
        std::string unzipBuffer;
        auto onUnzipChunk = [this, &unzipBuffer](const std::string& chunk) {
            unzipBuffer += chunk;
            size_t nlPos = 0;
            while ((nlPos = unzipBuffer.find('\n')) != std::string::npos) {
                std::string line = unzipBuffer.substr(0, nlPos);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                unzipBuffer.erase(0, nlPos + 1);
                if (line.empty()) {
                    continue;
                }

                if (line.rfind("UNZIP_PROGRESS:", 0) == 0) {
                    const size_t p1 = line.find(':', 15);
                    const size_t p2 =
                        (p1 == std::string::npos) ? std::string::npos : line.find(':', p1 + 1);
                    if (p1 != std::string::npos && p2 != std::string::npos) {
                        const std::string curS = line.substr(15, p1 - 15);
                        const std::string totalS = line.substr(p1 + 1, p2 - p1 - 1);
                        const std::string fileS = line.substr(p2 + 1);
                        const long cur = std::strtol(curS.c_str(), nullptr, 10);
                        const long total = std::strtol(totalS.c_str(), nullptr, 10);
                        this->CallAfter([this, cur, total, fileS]() {
                            const long percent = (total > 0) ? (cur * 100 / total) : 0;
                            m_logger->Log(LogLevel::Note,
                                          wxString::Format(wxT("[UNZIP] %ld/%ld (%ld%%) %s\n"), cur, total,
                                                           percent,
                                                           wxString::FromUTF8(fileS.c_str())));
                        });
                        continue;
                    }
                }

                this->CallAfter([this, line]() {
                    m_logger->Log(LogLevel::Debug, wxString::FromUTF8(line.c_str()) + wxT("\n"));
                });
            }
        };
        if (!runProcessAndCapture(psCmd, nullptr, &unzipExitCode, onUnzipChunk) ||
            unzipExitCode != 0) {
            this->CallAfter([this, unzipExitCode]() {
                m_logger->Log(LogLevel::Error,
                              wxString::Format(
                                  wxT("[ERROR] Extraction failed (code %lu). Check PowerShell/Expand-Archive.\n"),
                                  static_cast<unsigned long>(unzipExitCode)));
                m_installButton->Enable(true);
                m_installServerButton->Enable(m_steamcmdInstalled);
            });
            return;
        }
        this->CallAfter([this]() {
            m_logger->Log(LogLevel::Info, wxT("[INFO] Extraction finished.\n"));
        });

        const DWORD attrs = GetFileAttributesA(steamcmdExe.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            this->CallAfter([this, steamcmdExe, toWx]() {
                m_logger->Log(LogLevel::Error,
                              wxString::Format(wxT("[ERROR] steamcmd.exe not found after extraction: %s\n"),
                                               toWx(steamcmdExe)));
                m_installButton->Enable(true);
                m_installServerButton->Enable(m_steamcmdInstalled);
            });
            return;
        }

        auto runSteamcmdQuit = [&steamcmdExe, &installDir]() -> bool {
            const std::string cmd = "\"" + steamcmdExe + "\" +quit";
            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi = {};
            std::vector<char> cmdBuffer(cmd.begin(), cmd.end());
            cmdBuffer.push_back('\0');

            if (!CreateProcessA(nullptr, cmdBuffer.data(), nullptr, nullptr, FALSE,
                                CREATE_NO_WINDOW, nullptr, installDir.c_str(), &si, &pi)) {
                return false;
            }

            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode = 1;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return exitCode == 0;
        };

        (void)runSteamcmdQuit();
        (void)runSteamcmdQuit();

        this->CallAfter([this, steamcmdExe, toWx]() {
            m_logger->Log(LogLevel::Success,
                          wxString::Format(wxT("[SUCCESS] SteamCMD installed: %s\n"), toWx(steamcmdExe)));
            m_logger->Log(LogLevel::Info, wxT("[INFO] Action complete: install SteamCMD.\n"));
            m_steamcmdInstalled = true;
            SaveAppState();
            m_installButton->Enable(true);
            m_installServerButton->Enable(m_steamcmdInstalled);
        });
    }).detach();
#else
    m_logger->Log(LogLevel::Error, wxT("[ERROR] This action is only available on Windows.\n"));
#endif
}

void MainFrame::OnInstallServerClicked(wxCommandEvent&) {
    if (m_lastInstallDir.IsEmpty()) {
        m_logger->Log(LogLevel::Error,
                      wxT("[ERROR] No saved install folder. Run '1. Install SteamCMD' first.\n"));
        return;
    }

#ifdef _WIN32
    const int sel = m_notebook->GetSelection();
    if (sel == wxNOT_FOUND || m_notebook->GetPage(sel) == m_plusPage) {
        return;
    }
    const wxString tabName = m_notebook->GetPageText(sel);
    const wxString installDirWx = m_lastInstallDir + wxFILE_SEP_PATH + tabName;
    SaveServerDescriptionFromControls();
    SetBusyButtons(true);
    m_logger->Log(LogLevel::Info, wxT("[INFO] Server installation requested.\n"));

    std::thread([this, installDirWx]() {
        const std::string installDir = installDirWx.ToStdString();
        const std::string steamcmdExe = installDir + "\\steamcmd\\steamcmd.exe";
        const std::string workingDir = installDir + "\\steamcmd";

        auto toWx = [](const std::string& s) { return wxString::FromUTF8(s.c_str()); };

        const DWORD attrs = GetFileAttributesA(steamcmdExe.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            this->CallAfter([this, steamcmdExe, toWx]() {
                m_logger->Log(LogLevel::Error,
                              wxString::Format(
                                  wxT("[ERROR] steamcmd.exe not found: %s (run button 1 first)\n"),
                                  toWx(steamcmdExe)));
                SetBusyButtons(false);
            });
            return;
        }

        auto runSteamCommand = [this, &workingDir](const std::string& command,
                                                   const wxString& phase,
                                                   bool verboseOutput,
                                                   bool logCommand) -> DWORD {
            this->CallAfter([this, phase, command, logCommand]() {
                if (!phase.IsEmpty()) {
                    m_logger->Log(LogLevel::Info, wxString::Format(wxT("[INFO] %s\n"), phase));
                }
                if (logCommand) {
                    m_logger->Log(LogLevel::Note,
                                  wxString::Format(wxT("[NOTE] Command: %s\n"),
                                                   wxString::FromUTF8(command.c_str())));
                }
            });

            SECURITY_ATTRIBUTES sa = {};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;
            HANDLE readPipe = nullptr;
            HANDLE writePipe = nullptr;
            if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
                const DWORD err = GetLastError();
                this->CallAfter([this, err]() {
                    m_logger->Log(LogLevel::Error,
                                  wxString::Format(wxT("[ERROR] CreatePipe failed (code %lu).\n"),
                                                   static_cast<unsigned long>(err)));
                });
                return static_cast<DWORD>(-1);
            }
            SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = writePipe;
            si.hStdError = writePipe;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

            PROCESS_INFORMATION pi = {};
            std::vector<char> cmdBuffer(command.begin(), command.end());
            cmdBuffer.push_back('\0');
            if (!CreateProcessA(nullptr, cmdBuffer.data(), nullptr, nullptr, TRUE,
                                CREATE_NO_WINDOW, nullptr, workingDir.c_str(), &si, &pi)) {
                const DWORD err = GetLastError();
                CloseHandle(writePipe);
                CloseHandle(readPipe);
                this->CallAfter([this, err]() {
                    m_logger->Log(LogLevel::Error,
                                  wxString::Format(wxT("[ERROR] CreateProcessA failed (code %lu).\n"),
                                                   static_cast<unsigned long>(err)));
                });
                return static_cast<DWORD>(-1);
            }

            this->CallAfter([this, pid = static_cast<unsigned long>(pi.dwProcessId)]() {
                m_logger->Log(LogLevel::Info,
                              wxString::Format(wxT("[INFO] steamcmd started (PID %lu).\n"), pid));
            });

            CloseHandle(writePipe);
            char buffer[4096];
            DWORD bytesRead = 0;
            DWORD idleAfterExitMs = 0;
            while (true) {
                DWORD available = 0;
                if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) {
                    break;
                }

                if (available > 0) {
                    if (!ReadFile(readPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) ||
                        bytesRead == 0) {
                        break;
                    }
                    buffer[bytesRead] = '\0';
                    idleAfterExitMs = 0;

                    if (!verboseOutput) {
                        continue;
                    }
                    wxString line = wxString::FromUTF8(buffer);
                    if (line.empty()) {
                        line = wxString::From8BitData(buffer);
                    }
                    wxString lower = line.Lower();
                    LogLevel level = LogLevel::Debug;
                    if (lower.Contains("error") || lower.Contains("failed")) {
                        level = LogLevel::Error;
                    } else if (lower.Contains("success") || lower.Contains("complete") ||
                               lower.Contains("installed") || lower.Contains("ok")) {
                        level = LogLevel::Success;
                    } else if (lower.Contains("warning")) {
                        level = LogLevel::Warning;
                    } else {
                        level = LogLevel::Info;
                    }
                    this->CallAfter([this, level, line]() { m_logger->Log(level, line); });
                    continue;
                }

                const DWORD waitRes = WaitForSingleObject(pi.hProcess, 50);
                if (waitRes == WAIT_OBJECT_0) {
                    idleAfterExitMs += 50;
                    if (idleAfterExitMs >= 1000) {
                        break;
                    }
                }
            }

            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode = 1;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(readPipe);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return exitCode;
        };

        this->CallAfter([this, installDirWx]() {
            m_logger->Log(LogLevel::Note,
                          wxString::Format(wxT("[NOTE] Server target folder: %s\n"), installDirWx));
        });

        const std::string bootstrapCommand = "\"" + steamcmdExe + "\" +quit";
        (void)runSteamCommand(bootstrapCommand, wxEmptyString, false, false);

        const std::string installCommand =
            "\"" + steamcmdExe + "\" +force_install_dir \"" + installDir + "\" +login anonymous " +
            "+app_update 4129620 validate +quit";
        (void)runSteamCommand(installCommand, wxEmptyString, false, false);
        const DWORD installExit = runSteamCommand(
            installCommand, wxT("Installing server via SteamCMD..."), true, true);

        auto bootstrapServerConfigSilently = [this, &installDir]() -> bool {
            const std::string batPath =
                installDir +
                "\\steamcmd\\steamapps\\common\\Windrose Dedicated Server\\StartServerForeground.bat";
            const std::string patchedBat = CreatePatchedBatchForCapture(batPath);
            const std::string batDir =
                installDir + "\\steamcmd\\steamapps\\common\\Windrose Dedicated Server";
            const std::string logPath =
                installDir +
                "\\steamcmd\\steamapps\\common\\Windrose Dedicated Server\\R5\\Saved\\Logs\\R5.log";
            const std::string cmdLine =
                "cmd.exe /d /q /c \"pushd \"" + batDir + "\" && call \"" + patchedBat + "\"\"";

            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {};
            std::vector<char> buf(cmdLine.begin(), cmdLine.end());
            buf.push_back('\0');

            if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE,
                                CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW,
                                nullptr, batDir.c_str(), &si, &pi)) {
                return false;
            }

            std::uintmax_t startSize = 0;
            std::error_code fsErr;
            if (std::filesystem::exists(logPath, fsErr)) {
                startSize = std::filesystem::file_size(logPath, fsErr);
                if (fsErr) {
                    startSize = 0;
                }
            }
            bool markerSeen = false;
            bool markerSeenByFinalTail = false;
            std::string rollingLogBytes;
            const DWORD bootstrapTimeoutMs = 180000;
            const auto t0 = std::chrono::steady_clock::now();

            while (true) {
                if (std::filesystem::exists(logPath, fsErr) && !fsErr) {
                    const auto curSize = std::filesystem::file_size(logPath, fsErr);
                    if (!fsErr && curSize > startSize) {
                        std::ifstream logIn(logPath, std::ios::binary);
                        if (logIn.is_open()) {
                            logIn.seekg(static_cast<std::streamoff>(startSize), std::ios::beg);
                            std::string tail((std::istreambuf_iterator<char>(logIn)),
                                             std::istreambuf_iterator<char>());
                            rollingLogBytes.append(tail);
                            if (rollingLogBytes.size() > 1024 * 1024) {
                                rollingLogBytes.erase(0, rollingLogBytes.size() - 256 * 1024);
                            }
                            if (ContainsBootstrapMarker(rollingLogBytes)) {
                                markerSeen = true;
                                this->CallAfter([this]() {
                                    m_logger->Log(
                                        LogLevel::Note,
                                        wxT("[NOTE] Bootstrap marker detected (live), stopping bootstrap...\n"));
                                });
                                break;
                            }
                        }
                        startSize = curSize;
                    }
                }

                const DWORD waitRes = WaitForSingleObject(pi.hProcess, 50);
                if (waitRes == WAIT_OBJECT_0) {
                    break;
                }
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
                if (elapsed > bootstrapTimeoutMs) {
                    break;
                }
            }

            auto gracefulStopWithCtrlC = [&pi]() {
                bool stopped = false;
                FreeConsole();
                if (AttachConsole(pi.dwProcessId)) {
                    SetConsoleCtrlHandler(nullptr, TRUE);
                    if (!GenerateConsoleCtrlEvent(CTRL_C_EVENT, pi.dwProcessId)) {
                        (void)GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pi.dwProcessId);
                    }
                    const DWORD waitRes = WaitForSingleObject(pi.hProcess, 20000);
                    SetConsoleCtrlHandler(nullptr, FALSE);
                    FreeConsole();
                    stopped = (waitRes == WAIT_OBJECT_0);
                } else {
                    (void)GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pi.dwProcessId);
                    const DWORD waitRes = WaitForSingleObject(pi.hProcess, 20000);
                    stopped = (waitRes == WAIT_OBJECT_0);
                }

                if (!stopped) {
                    const std::string killTreeCmd =
                        "cmd.exe /d /q /c \"taskkill /PID " + std::to_string(pi.dwProcessId) +
                        " /T /F >nul 2>nul\"";
                    (void)std::system(killTreeCmd.c_str());
                    const DWORD waitRes = WaitForSingleObject(pi.hProcess, 5000);
                    stopped = (waitRes == WAIT_OBJECT_0);
                }

                if (!stopped) {
                    (void)TerminateProcess(pi.hProcess, 1);
                    const DWORD waitRes = WaitForSingleObject(pi.hProcess, 5000);
                    stopped = (waitRes == WAIT_OBJECT_0);
                }
                return stopped;
            };
            if (markerSeen) {
                const std::string killTreeCmd =
                    "cmd.exe /d /q /c \"taskkill /PID " + std::to_string(pi.dwProcessId) +
                    " /T /F >nul 2>nul\"";
                (void)std::system(killTreeCmd.c_str());
                const DWORD waitRes = WaitForSingleObject(pi.hProcess, 5000);
                if (waitRes != WAIT_OBJECT_0) {
                    (void)TerminateProcess(pi.hProcess, 1);
                    (void)WaitForSingleObject(pi.hProcess, 2000);
                }
            } else {
                (void)gracefulStopWithCtrlC();
            }

            if (!markerSeen) {
                std::error_code tailErr;
                if (std::filesystem::exists(logPath, tailErr) && !tailErr) {
                    const auto endSize = std::filesystem::file_size(logPath, tailErr);
                    if (!tailErr && endSize > 0) {
                        const std::uintmax_t tailBytes = 2ULL * 1024ULL * 1024ULL;
                        const std::uintmax_t readFrom =
                            (endSize > tailBytes) ? (endSize - tailBytes) : 0;
                        std::ifstream finalLog(logPath, std::ios::binary);
                        if (finalLog.is_open()) {
                            finalLog.seekg(static_cast<std::streamoff>(readFrom), std::ios::beg);
                            std::string finalTail((std::istreambuf_iterator<char>(finalLog)),
                                                  std::istreambuf_iterator<char>());
                            markerSeen = ContainsBootstrapMarker(finalTail);
                            markerSeenByFinalTail = markerSeen;
                        }
                    }
                }
            }

            this->CallAfter([this, markerSeen, markerSeenByFinalTail]() {
                if (markerSeen) {
                    if (markerSeenByFinalTail) {
                        m_logger->Log(LogLevel::Note,
                                      wxT("[NOTE] Bootstrap marker detected in final log-tail check.\n"));
                    } else {
                        m_logger->Log(LogLevel::Note,
                                      wxT("[NOTE] Bootstrap marker detected during live log polling.\n"));
                    }
                } else {
                    m_logger->Log(LogLevel::Warning,
                                  wxT("[WARN] Bootstrap marker not detected before shutdown/timeout.\n"));
                }
            });

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return markerSeen;
        };
        bool configBootstrapOk = false;
        if (installExit == 0) {
            configBootstrapOk = bootstrapServerConfigSilently();
        }

        this->CallAfter([this, installExit, configBootstrapOk]() {
            if (installExit == 0) {
                m_logger->Log(LogLevel::Success, wxT("[SUCCESS] Server installation completed.\n"));
                if (!configBootstrapOk) {
                    m_logger->Log(
                        LogLevel::Warning,
                        wxT("[WARN] Config bootstrap marker was not reached, continuing anyway.\n"));
                }
                m_serverInstalled = true;
                SaveAppState();
                LoadServerDescriptionToControls();
                SetJsonControlsEnabled(true);
            } else {
                m_logger->Log(
                    LogLevel::Error,
                    wxString::Format(wxT("[ERROR] steamcmd exited with code %lu.\n"),
                                     static_cast<unsigned long>(installExit)));
                SetJsonControlsEnabled(false);
            }
            SetBusyButtons(false);
        });
    }).detach();
#else
    m_logger->Log(LogLevel::Error, wxT("[ERROR] This action is only available on Windows.\n"));
#endif
}

void MainFrame::OnStartServerClicked(wxCommandEvent&) {
#ifdef _WIN32
    const int sel = m_notebook->GetSelection();
    if (sel == wxNOT_FOUND || m_notebook->GetPage(sel) == m_plusPage) {
        return;
    }
    const wxString tabName = m_notebook->GetPageText(sel);
    const std::string installDir = (m_lastInstallDir + wxFILE_SEP_PATH + tabName).ToStdString();
    const std::string batPath =
        installDir +
        "\\steamcmd\\steamapps\\common\\Windrose Dedicated Server\\StartServerForeground.bat";
    const std::string patchedBat = CreatePatchedBatchForCapture(batPath);
    const std::string batDir =
        installDir + "\\steamcmd\\steamapps\\common\\Windrose Dedicated Server";

    const DWORD attrs = GetFileAttributesA(batPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        m_logger->Log(LogLevel::Error,
                      wxString::Format(wxT("[ERROR] StartServerForeground.bat not found: %s\n"),
                                       wxString::FromUTF8(batPath.c_str())));
        return;
    }

    const std::string cmdLine =
        "cmd.exe /d /q /c \"pushd \"" + batDir + "\" && call \"" + patchedBat + "\"\"";
    m_logger->Log(LogLevel::Info, wxT("[INFO] Starting server and redirecting output...\n"));
    SetBusyButtons(true);

    std::thread([this, cmdLine, batDir]() {
        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        std::vector<char> buf(cmdLine.begin(), cmdLine.end());
        buf.push_back('\0');

        if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                            nullptr, batDir.c_str(), &si, &pi)) {
            const DWORD err = GetLastError();
            this->CallAfter([this, err]() {
                m_logger->Log(LogLevel::Error,
                              wxString::Format(wxT("[ERROR] Failed to launch StartServer (code %lu).\n"),
                                               static_cast<unsigned long>(err)));
            });
            return;
        }

        this->CallAfter([this, pid = static_cast<unsigned long>(pi.dwProcessId)]() {
            m_startServerRunning = true;
            m_startServerPid = pid;
            SetBusyButtons(false);
            m_logger->Log(LogLevel::Info,
                          wxString::Format(wxT("[INFO] StartServer launched (PID %lu).\n"), pid));
        });

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        this->CallAfter([this, exitCode]() {
            m_startServerRunning = false;
            m_startServerPid = 0;
            SetBusyButtons(false);
            m_logger->Log(LogLevel::Info,
                          wxString::Format(wxT("[INFO] StartServer process exited (code %lu).\n"),
                                           static_cast<unsigned long>(exitCode)));
        });
    }).detach();
#else
    m_logger->Log(LogLevel::Error, wxT("[ERROR] This action is only available on Windows.\n"));
#endif
}

void MainFrame::OnStopServerClicked(wxCommandEvent&) {
#ifdef _WIN32
    if (!m_startServerRunning || m_startServerPid == 0) {
        return;
    }

    const unsigned long pid = m_startServerPid;
    m_logger->Log(LogLevel::Warning,
                  wxString::Format(wxT("[WARN] Stop requested for StartServer PID %lu.\n"), pid));

    std::thread([this, pid]() {
        const std::string killCmd =
            "cmd.exe /d /q /c \"taskkill /PID " + std::to_string(pid) + " /T /F\"";
        const int rc = std::system(killCmd.c_str());
        this->CallAfter([this, pid, rc]() {
            if (rc == 0) {
                m_logger->Log(LogLevel::Info,
                              wxString::Format(wxT("[INFO] Stop command sent for PID %lu.\n"), pid));
            } else {
                m_logger->Log(LogLevel::Error,
                              wxString::Format(wxT("[ERROR] Stop command failed for PID %lu (rc=%d).\n"),
                                               pid, rc));
            }
        });
    }).detach();
#else
    m_logger->Log(LogLevel::Error, wxT("[ERROR] This action is only available on Windows.\n"));
#endif
}

void MainFrame::OnAutoBackupChanged(wxCommandEvent&) {
    UpdateBackupScheduler();
}

void MainFrame::OnBackupIntervalChanged(wxCommandEvent&) {
    UpdateBackupScheduler();
}

void MainFrame::OnBackupTimer(wxTimerEvent&) {
    RunBackupNowAsync();
}

int MainFrame::GetSelectedBackupMinutes() const {
    if (!m_backupIntervalChoice) {
        return 15;
    }
    switch (m_backupIntervalChoice->GetSelection()) {
        case 0: return 5;
        case 1: return 10;
        case 2: return 15;
        case 3: return 30;
        case 4: return 60;
        default: return 15;
    }
}

int MainFrame::GetMaxBackupsToKeep() const {
    if (!m_maxBackupsCtrl) {
        return 10;
    }
    return m_maxBackupsCtrl->GetValue();
}

void MainFrame::UpdateBackupScheduler() {
    if (!m_autoBackupChk || !m_backupIntervalChoice) {
        return;
    }
    const bool enabled = m_autoBackupChk->IsEnabled();
    const bool checked = m_autoBackupChk->GetValue();
    m_backupIntervalChoice->Enable(enabled && checked);

    if (!enabled || !checked) {
        if (m_backupTimer.IsRunning()) {
            m_backupTimer.Stop();
        }
        return;
    }

    const int minutes = GetSelectedBackupMinutes();
    const int intervalMs = minutes * 60 * 1000;
    m_backupTimer.Start(intervalMs);
    m_logger->Log(LogLevel::Note,
                  wxString::Format(wxT("[NOTE] AutoBackup enabled (%d min).\n"), minutes));
}

void MainFrame::RunBackupNowAsync() {
    if (m_backupInProgress) {
        return;
    }
    const std::string installDir = CurrentTabInstallDir();
    if (installDir.empty()) {
        return;
    }

    const std::string sourceDir =
        installDir + "\\steamcmd\\steamapps\\common\\Windrose Dedicated Server\\R5\\Saved";
    const std::string backupDir = installDir + "\\BackupServer";
    const int maxBackups = GetMaxBackupsToKeep();
    std::error_code fsErr;
    if (!std::filesystem::exists(sourceDir, fsErr) || fsErr) {
        return;
    }
    std::filesystem::create_directories(backupDir, fsErr);
    if (fsErr) {
        return;
    }

    m_backupInProgress = true;
    auto now = wxDateTime::Now();
    const std::string zipName =
        "Saved_" + now.Format("%Y%m%d_%H%M%S").ToStdString() + ".zip";
    const std::string zipPath = backupDir + "\\" + zipName;

    auto quotePs = [](std::string s) {
        size_t pos = 0;
        while ((pos = s.find('\'', pos)) != std::string::npos) {
            s.replace(pos, 1, "''");
            pos += 2;
        }
        return s;
    };

    m_logger->Log(LogLevel::Info,
                  wxString::Format(wxT("[INFO] Backup started: %s\n"),
                                   wxString::FromUTF8(zipName.c_str())));

    std::thread([this, sourceDir, zipPath, backupDir, maxBackups, quotePs]() {
        const std::string psCmd =
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"$ErrorActionPreference='Stop';"
            "Compress-Archive -Path '" + quotePs(sourceDir + "\\*") +
            "' -DestinationPath '" + quotePs(zipPath) + "' -Force\"";
        const int rc = std::system(psCmd.c_str());
        if (rc == 0) {
            std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> zips;
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(backupDir, ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (ext != ".zip") {
                    continue;
                }
                zips.emplace_back(entry.last_write_time(ec), entry.path());
                if (ec) {
                    ec.clear();
                }
            }

            std::sort(zips.begin(), zips.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });

            while (static_cast<int>(zips.size()) > maxBackups && !zips.empty()) {
                std::error_code rmErr;
                std::filesystem::remove(zips.front().second, rmErr);
                zips.erase(zips.begin());
            }
        }

        this->CallAfter([this, rc, zipPath, maxBackups]() {
            m_backupInProgress = false;
            if (rc == 0) {
                m_logger->Log(LogLevel::Success,
                              wxString::Format(wxT("[SUCCESS] Backup created: %s\n"),
                                               wxString::FromUTF8(zipPath.c_str())));
                m_logger->Log(LogLevel::Note,
                              wxString::Format(wxT("[NOTE] Backup retention: keep last %d zip(s).\n"),
                                               maxBackups));
            } else {
                m_logger->Log(LogLevel::Error,
                              wxString::Format(wxT("[ERROR] Backup failed (rc=%d).\n"), rc));
            }
        });
    }).detach();
}

void MainFrame::OnValidateJsonClicked(wxCommandEvent&) {
    if (!m_worldIslandIdCtrl || !m_worldNameCtrl || !m_worldPresetChoice ||
        !m_sharedQuestsChk || !m_easyExploreChk || !m_mobHealthMultiplierCtrl ||
        !m_mobDamageMultiplierCtrl || !m_shipsHealthMultiplierCtrl || !m_shipsDamageMultiplierCtrl ||
        !m_boardingDifficultyMultiplierCtrl || !m_coopStatsCorrectionCtrl ||
        !m_coopShipStatsCorrectionCtrl || !m_combatDifficultyChoice) {
        return;
    }

    if (m_passwordProtectedChk && m_passwordProtectedChk->GetValue() &&
        m_passwordCtrl && m_passwordCtrl->GetValue().Trim().IsEmpty()) {
        wxMessageBox(wxT("Le champ Password est obligatoire si IsPasswordProtected est coché."),
                     wxT("Validation JSON"), wxOK | wxICON_WARNING, this);
        m_passwordCtrl->SetFocus();
        return;
    }

    const std::string worldPath = CurrentServerDescriptionPath();
    const std::string legacyPath = CurrentLegacyServerDescriptionPath();
    if (worldPath.empty() && legacyPath.empty()) {
        wxMessageBox(wxT("Impossible de trouver les JSON de configuration."),
                     wxT("Validation JSON"), wxOK | wxICON_ERROR, this);
        return;
    }

    SaveServerDescriptionFromControls();
    m_logger->Log(LogLevel::Success, wxT("[SUCCESS] WorldDescription + ServerDescription updated.\n"));
}

std::string MainFrame::CurrentTabInstallDir() const {
    const int sel = m_notebook ? m_notebook->GetSelection() : wxNOT_FOUND;
    if (sel == wxNOT_FOUND || !m_notebook || m_notebook->GetPage(sel) == m_plusPage) {
        return "";
    }
    const wxString tabName = m_notebook->GetPageText(sel);
    return (m_lastInstallDir + wxFILE_SEP_PATH + tabName).ToStdString();
}

std::string MainFrame::CurrentServerDescriptionPath() const {
    const std::string dir = CurrentTabInstallDir();
    if (dir.empty()) {
        return "";
    }
    const std::filesystem::path worldsRoot =
        std::filesystem::path(dir) / "steamcmd" / "steamapps" / "common" /
        "Windrose Dedicated Server" / "R5" / "Saved" / "SaveProfiles" / "Default" /
        "RocksDB" / "0.10.0" / "Worlds";

    std::error_code ec;
    if (!std::filesystem::exists(worldsRoot, ec) || ec) {
        return "";
    }

    std::filesystem::path worldDir;
    for (const auto& entry : std::filesystem::directory_iterator(worldsRoot, ec)) {
        if (ec) break;
        if (entry.is_directory()) {
            worldDir = entry.path();
            break;
        }
    }
    if (worldDir.empty()) {
        return "";
    }

    for (const auto& entry : std::filesystem::directory_iterator(worldDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (ext == ".json") {
            return entry.path().string();
        }
    }
    return (worldDir / "WorldDescription.json").string();
}

std::string MainFrame::CurrentLegacyServerDescriptionPath() const {
    const std::string dir = CurrentTabInstallDir();
    if (dir.empty()) {
        return "";
    }
    return dir +
           "\\steamcmd\\steamapps\\common\\Windrose Dedicated Server\\R5\\ServerDescription.json";
}

void MainFrame::LoadServerDescriptionToControls() {
    if (!m_worldIslandIdCtrl || !m_worldNameCtrl || !m_worldPresetChoice ||
        !m_sharedQuestsChk || !m_easyExploreChk || !m_mobHealthMultiplierCtrl ||
        !m_mobDamageMultiplierCtrl || !m_shipsHealthMultiplierCtrl || !m_shipsDamageMultiplierCtrl ||
        !m_boardingDifficultyMultiplierCtrl || !m_coopStatsCorrectionCtrl ||
        !m_coopShipStatsCorrectionCtrl || !m_combatDifficultyChoice) {
        return;
    }

    m_worldIslandIdCtrl->SetValue(wxEmptyString);
    m_worldNameCtrl->SetValue(wxEmptyString);
    m_worldPresetChoice->SetStringSelection(wxT("Medium"));
    m_sharedQuestsChk->SetValue(true);
    m_easyExploreChk->SetValue(false);
    m_mobHealthMultiplierCtrl->SetValue(wxT("1"));
    m_mobDamageMultiplierCtrl->SetValue(wxT("1"));
    m_shipsHealthMultiplierCtrl->SetValue(wxT("1"));
    m_shipsDamageMultiplierCtrl->SetValue(wxT("1"));
    m_boardingDifficultyMultiplierCtrl->SetValue(wxT("1"));
    m_coopStatsCorrectionCtrl->SetValue(wxT("1"));
    m_coopShipStatsCorrectionCtrl->SetValue(wxT("0"));
    m_combatDifficultyChoice->SetStringSelection(wxT("Normal"));
    m_inviteCodeCtrl->SetValue(wxEmptyString);
    m_passwordProtectedChk->SetValue(false);
    m_passwordCtrl->SetValue(wxEmptyString);
    m_serverNameCtrl->SetValue(wxEmptyString);
    m_maxPlayerCountCtrl->SetValue(1);
    m_userSelectedRegionCtrl->SetValue(wxEmptyString);
    m_useDirectConnectionChk->SetValue(false);
    m_directConnectionServerAddressCtrl->SetValue(wxEmptyString);
    m_directConnectionServerPortCtrl->SetValue(-1);
    m_directConnectionProxyAddressCtrl->SetValue(wxT("0.0.0.0"));

    const std::string path = CurrentServerDescriptionPath();
    if (path.empty()) {
        return;
    }
    std::ifstream in(path);
    if (!in.is_open()) {
        return;
    }
    std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    auto readString = [&json](const std::string& key, const std::string& fallback) {
        const std::string token = "\"" + key + "\"";
        size_t k = json.find(token);
        if (k == std::string::npos) return fallback;
        size_t q1 = json.find('"', json.find(':', k) + 1);
        if (q1 == std::string::npos) return fallback;
        size_t q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos) return fallback;
        return json.substr(q1 + 1, q2 - q1 - 1);
    };
    auto readBool = [&json](const std::string& keyToken, bool fallback) {
        const std::string token = "\"" + keyToken + "\"";
        size_t k = json.find(token);
        if (k == std::string::npos) return fallback;
        size_t c = json.find(':', k);
        if (c == std::string::npos) return fallback;
        const std::string tail = json.substr(c + 1, 8);
        if (tail.find("true") != std::string::npos) return true;
        if (tail.find("false") != std::string::npos) return false;
        return fallback;
    };
    auto readDouble = [&json](const std::string& keyToken, const std::string& fallback) {
        try {
            const std::regex rx("\"" + keyToken + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
            std::smatch m;
            if (std::regex_search(json, m, rx) && m.size() > 1) {
                return m[1].str();
            }
        } catch (...) {
        }
        return fallback;
    };
    auto readTagDifficulty = [&json]() -> std::string {
        const std::regex rx("\"WDS\\.Parameter\\.CombatDifficulty\\.([A-Za-z]+)\"");
        std::smatch m;
        if (std::regex_search(json, m, rx) && m.size() > 1) {
            return m[1].str();
        }
        return "Normal";
    };

    m_worldIslandIdCtrl->SetValue(wxString::FromUTF8(readString("islandId", "").c_str()));
    m_worldNameCtrl->SetValue(wxString::FromUTF8(readString("WorldName", "").c_str()));
    m_worldPresetChoice->SetStringSelection(
        wxString::FromUTF8(readString("WorldPresetType", "Medium").c_str()));
    m_sharedQuestsChk->SetValue(readBool("{\\\"TagName\\\": \\\"WDS.Parameter.Coop.SharedQuests\\\"}", true));
    m_easyExploreChk->SetValue(readBool("{\\\"TagName\\\": \\\"WDS.Parameter.EasyExplore\\\"}", false));
    m_mobHealthMultiplierCtrl->SetValue(
        wxString::FromUTF8(readDouble("{\\\"TagName\\\": \\\"WDS.Parameter.MobHealthMultiplier\\\"}", "1").c_str()));
    m_mobDamageMultiplierCtrl->SetValue(
        wxString::FromUTF8(readDouble("{\\\"TagName\\\": \\\"WDS.Parameter.MobDamageMultiplier\\\"}", "1").c_str()));
    m_shipsHealthMultiplierCtrl->SetValue(
        wxString::FromUTF8(readDouble("{\\\"TagName\\\": \\\"WDS.Parameter.ShipsHealthMultiplier\\\"}", "1").c_str()));
    m_shipsDamageMultiplierCtrl->SetValue(
        wxString::FromUTF8(readDouble("{\\\"TagName\\\": \\\"WDS.Parameter.ShipsDamageMultiplier\\\"}", "1").c_str()));
    m_boardingDifficultyMultiplierCtrl->SetValue(wxString::FromUTF8(
        readDouble("{\\\"TagName\\\": \\\"WDS.Parameter.BoardingDifficultyMultiplier\\\"}", "1").c_str()));
    m_coopStatsCorrectionCtrl->SetValue(wxString::FromUTF8(
        readDouble("{\\\"TagName\\\": \\\"WDS.Parameter.Coop.StatsCorrectionModifier\\\"}", "1").c_str()));
    m_coopShipStatsCorrectionCtrl->SetValue(wxString::FromUTF8(
        readDouble("{\\\"TagName\\\": \\\"WDS.Parameter.Coop.ShipStatsCorrectionModifier\\\"}", "0").c_str()));
    m_combatDifficultyChoice->SetStringSelection(
        wxString::FromUTF8(readTagDifficulty().c_str()));

    const std::string legacyPath = CurrentLegacyServerDescriptionPath();
    if (!legacyPath.empty()) {
        std::ifstream legacyIn(legacyPath);
        if (legacyIn.is_open()) {
            std::string legacyJson((std::istreambuf_iterator<char>(legacyIn)),
                                   std::istreambuf_iterator<char>());
            auto readLegacyString = [&legacyJson](const std::string& key, const std::string& fallback) {
                const std::string token = "\"" + key + "\"";
                size_t k = legacyJson.find(token);
                if (k == std::string::npos) return fallback;
                size_t q1 = legacyJson.find('"', legacyJson.find(':', k) + 1);
                if (q1 == std::string::npos) return fallback;
                size_t q2 = legacyJson.find('"', q1 + 1);
                if (q2 == std::string::npos) return fallback;
                return legacyJson.substr(q1 + 1, q2 - q1 - 1);
            };
            auto readLegacyBool = [&legacyJson](const std::string& key, bool fallback) {
                const std::string token = "\"" + key + "\"";
                size_t k = legacyJson.find(token);
                if (k == std::string::npos) return fallback;
                size_t c = legacyJson.find(':', k);
                if (c == std::string::npos) return fallback;
                const std::string tail = legacyJson.substr(c + 1, 8);
                if (tail.find("true") != std::string::npos) return true;
                if (tail.find("false") != std::string::npos) return false;
                return fallback;
            };
            auto readLegacyInt = [&legacyJson](const std::string& key, int fallback) {
                try {
                    const std::regex rx("\"" + key + "\"\\s*:\\s*(-?\\d+)");
                    std::smatch m;
                    if (std::regex_search(legacyJson, m, rx) && m.size() > 1) {
                        return std::stoi(m[1].str());
                    }
                } catch (...) {
                }
                return fallback;
            };

            m_inviteCodeCtrl->SetValue(wxString::FromUTF8(readLegacyString("InviteCode", "").c_str()));
            m_passwordProtectedChk->SetValue(readLegacyBool("IsPasswordProtected", false));
            m_passwordCtrl->SetValue(wxString::FromUTF8(readLegacyString("Password", "").c_str()));
            m_serverNameCtrl->SetValue(wxString::FromUTF8(readLegacyString("ServerName", "").c_str()));
            m_maxPlayerCountCtrl->SetValue(readLegacyInt("MaxPlayerCount", 1));
            m_userSelectedRegionCtrl->SetValue(wxString::FromUTF8(readLegacyString("UserSelectedRegion", "").c_str()));
            m_useDirectConnectionChk->SetValue(readLegacyBool("UseDirectConnection", false));
            m_directConnectionServerAddressCtrl->SetValue(
                wxString::FromUTF8(readLegacyString("DirectConnectionServerAddress", "").c_str()));
            m_directConnectionServerPortCtrl->SetValue(readLegacyInt("DirectConnectionServerPort", -1));
            m_directConnectionProxyAddressCtrl->SetValue(
                wxString::FromUTF8(readLegacyString("DirectConnectionProxyAddress", "0.0.0.0").c_str()));
        }
    }
}

void MainFrame::SaveServerDescriptionFromControls() {
    if (!m_worldIslandIdCtrl || !m_worldNameCtrl || !m_worldPresetChoice ||
        !m_sharedQuestsChk || !m_easyExploreChk || !m_mobHealthMultiplierCtrl ||
        !m_mobDamageMultiplierCtrl || !m_shipsHealthMultiplierCtrl || !m_shipsDamageMultiplierCtrl ||
        !m_boardingDifficultyMultiplierCtrl || !m_coopStatsCorrectionCtrl ||
        !m_coopShipStatsCorrectionCtrl || !m_combatDifficultyChoice) {
        return;
    }
    const std::string path = CurrentServerDescriptionPath();
    const std::string legacyPath = CurrentLegacyServerDescriptionPath();
    if (path.empty() && legacyPath.empty()) {
        return;
    }

    std::error_code ec;
    if (!path.empty()) {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    }
    if (!legacyPath.empty()) {
        std::filesystem::create_directories(std::filesystem::path(legacyPath).parent_path(), ec);
    }

    auto esc = [](std::string s) {
        size_t p = 0;
        while ((p = s.find('\\', p)) != std::string::npos) { s.insert(p, "\\"); p += 2; }
        p = 0;
        while ((p = s.find('"', p)) != std::string::npos) { s.insert(p, "\\"); p += 2; }
        return s;
    };
    auto readCreationTimeRaw = [](const std::string& filePath) -> std::string {
        std::ifstream in(filePath);
        if (!in.is_open()) {
            return "0";
        }
        const std::string json((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        try {
            const std::regex rx("\"CreationTime\"\\s*:\\s*([-+]?\\d+(?:\\.\\d+)?(?:[eE][-+]?\\d+)?)");
            std::smatch m;
            if (std::regex_search(json, m, rx) && m.size() > 1) {
                return m[1].str();
            }
        } catch (...) {
        }
        return "0";
    };

    std::ofstream out(path, std::ios::trunc);
    auto numericOrDefault = [](const wxTextCtrl* ctrl, const char* fallback) {
        if (!ctrl) return std::string(fallback);
        std::string s = ctrl->GetValue().ToStdString();
        try {
            size_t idx = 0;
            (void)std::stod(s, &idx);
            if (idx == s.size()) return s;
        } catch (...) {
        }
        return std::string(fallback);
    };
    const std::string creationTimeRaw = readCreationTimeRaw(path);
    const std::string combatDiff = m_combatDifficultyChoice->GetStringSelection().ToStdString();
    if (out.is_open()) {
        out << "{\n"
            << "\t\"Version\": 1,\n"
            << "\t\"WorldDescription\":\n"
            << "\t{\n"
            << "\t\t\"islandId\": \"" << esc(m_worldIslandIdCtrl->GetValue().ToStdString()) << "\",\n"
            << "\t\t\"WorldName\": \"" << esc(m_worldNameCtrl->GetValue().ToStdString()) << "\",\n"
            << "\t\t\"CreationTime\": " << creationTimeRaw << ",\n"
            << "\t\t\"WorldPresetType\": \"" << esc(m_worldPresetChoice->GetStringSelection().ToStdString()) << "\",\n"
            << "\t\t\"WorldSettings\":\n"
            << "\t\t{\n"
            << "\t\t\t\"BoolParameters\":\n"
            << "\t\t\t{\n"
            << "\t\t\t\t\"{\\\"TagName\\\": \\\"WDS.Parameter.Coop.SharedQuests\\\"}\": "
            << (m_sharedQuestsChk->GetValue() ? "true" : "false") << ",\n"
            << "\t\t\t\t\"{\\\"TagName\\\": \\\"WDS.Parameter.EasyExplore\\\"}\": "
            << (m_easyExploreChk->GetValue() ? "true" : "false") << "\n"
            << "\t\t\t},\n"
            << "\t\t\t\"FloatParameters\":\n"
            << "\t\t\t{\n"
            << "\t\t\t\t\"{\\\"TagName\\\": \\\"WDS.Parameter.MobHealthMultiplier\\\"}\": "
            << numericOrDefault(m_mobHealthMultiplierCtrl, "1") << ",\n"
            << "\t\t\t\t\"{\\\"TagName\\\": \\\"WDS.Parameter.MobDamageMultiplier\\\"}\": "
            << numericOrDefault(m_mobDamageMultiplierCtrl, "1") << ",\n"
            << "\t\t\t\t\"{\\\"TagName\\\": \\\"WDS.Parameter.ShipsHealthMultiplier\\\"}\": "
            << numericOrDefault(m_shipsHealthMultiplierCtrl, "1") << ",\n"
            << "\t\t\t\t\"{\\\"TagName\\\": \\\"WDS.Parameter.ShipsDamageMultiplier\\\"}\": "
            << numericOrDefault(m_shipsDamageMultiplierCtrl, "1") << ",\n"
            << "\t\t\t\t\"{\\\"TagName\\\": \\\"WDS.Parameter.BoardingDifficultyMultiplier\\\"}\": "
            << numericOrDefault(m_boardingDifficultyMultiplierCtrl, "1") << ",\n"
            << "\t\t\t\t\"{\\\"TagName\\\": \\\"WDS.Parameter.Coop.StatsCorrectionModifier\\\"}\": "
            << numericOrDefault(m_coopStatsCorrectionCtrl, "1") << ",\n"
            << "\t\t\t\t\"{\\\"TagName\\\": \\\"WDS.Parameter.Coop.ShipStatsCorrectionModifier\\\"}\": "
            << numericOrDefault(m_coopShipStatsCorrectionCtrl, "0") << "\n"
            << "\t\t\t},\n"
            << "\t\t\t\"TagParameters\":\n"
            << "\t\t\t{\n"
            << "\t\t\t\t\"{\\\"TagName\\\": \\\"WDS.Parameter.CombatDifficulty\\\"}\":\n"
            << "\t\t\t\t{\n"
            << "\t\t\t\t\t\"TagName\": \"WDS.Parameter.CombatDifficulty." << esc(combatDiff) << "\"\n"
            << "\t\t\t\t}\n"
            << "\t\t\t}\n"
            << "\t\t}\n"
            << "\t}\n"
            << "}\n";
    }

    if (!legacyPath.empty()) {
        std::ofstream legacyOut(legacyPath, std::ios::trunc);
        if (legacyOut.is_open()) {
            legacyOut << "{\n"
                      << "\t\"Version\": 1,\n"
                      << "\t\"DeploymentId\": \"0.10.0.3.104-256f9653\",\n"
                      << "\t\"ServerDescription_Persistent\":\n"
                      << "\t{\n"
                      << "\t\t\"PersistentServerId\": \"DE2B06D14F61D7A7E907B695253B5954\",\n"
                      << "\t\t\"InviteCode\": \"" << esc(m_inviteCodeCtrl->GetValue().ToStdString()) << "\",\n"
                      << "\t\t\"IsPasswordProtected\": " << (m_passwordProtectedChk->GetValue() ? "true" : "false") << ",\n"
                      << "\t\t\"Password\": \"" << esc(m_passwordCtrl->GetValue().ToStdString()) << "\",\n"
                      << "\t\t\"ServerName\": \"" << esc(m_serverNameCtrl->GetValue().ToStdString()) << "\",\n"
                      << "\t\t\"WorldIslandId\": \"" << esc(m_worldIslandIdCtrl->GetValue().ToStdString()) << "\",\n"
                      << "\t\t\"MaxPlayerCount\": " << m_maxPlayerCountCtrl->GetValue() << ",\n"
                      << "\t\t\"UserSelectedRegion\": \"" << esc(m_userSelectedRegionCtrl->GetValue().ToStdString()) << "\",\n"
                      << "\t\t\"P2pProxyAddress\": \"127.0.0.1\",\n"
                      << "\t\t\"UseDirectConnection\": " << (m_useDirectConnectionChk->GetValue() ? "true" : "false") << ",\n"
                      << "\t\t\"DirectConnectionServerAddress\": \""
                      << esc(m_directConnectionServerAddressCtrl->GetValue().ToStdString()) << "\",\n"
                      << "\t\t\"DirectConnectionServerPort\": " << m_directConnectionServerPortCtrl->GetValue() << ",\n"
                      << "\t\t\"DirectConnectionProxyAddress\": \""
                      << esc(m_directConnectionProxyAddressCtrl->GetValue().ToStdString()) << "\"\n"
                      << "\t}\n"
                      << "}\n";
        }
    }
}

void MainFrame::SetJsonControlsEnabled(bool enabled) {
    if (m_inviteCodeCtrl) m_inviteCodeCtrl->Enable(enabled);
    if (m_passwordProtectedChk) m_passwordProtectedChk->Enable(enabled);
    if (m_passwordCtrl) m_passwordCtrl->Enable(enabled);
    if (m_serverNameCtrl) m_serverNameCtrl->Enable(enabled);
    if (m_maxPlayerCountCtrl) m_maxPlayerCountCtrl->Enable(enabled);
    if (m_userSelectedRegionCtrl) m_userSelectedRegionCtrl->Enable(enabled);
    if (m_useDirectConnectionChk) m_useDirectConnectionChk->Enable(enabled);
    if (m_directConnectionServerAddressCtrl) m_directConnectionServerAddressCtrl->Enable(enabled);
    if (m_directConnectionServerPortCtrl) m_directConnectionServerPortCtrl->Enable(enabled);
    if (m_directConnectionProxyAddressCtrl) m_directConnectionProxyAddressCtrl->Enable(enabled);
    if (m_worldIslandIdCtrl) m_worldIslandIdCtrl->Enable(enabled);
    if (m_worldNameCtrl) m_worldNameCtrl->Enable(enabled);
    if (m_worldPresetChoice) m_worldPresetChoice->Enable(enabled);
    if (m_sharedQuestsChk) m_sharedQuestsChk->Enable(enabled);
    if (m_easyExploreChk) m_easyExploreChk->Enable(enabled);
    if (m_mobHealthMultiplierCtrl) m_mobHealthMultiplierCtrl->Enable(enabled);
    if (m_mobDamageMultiplierCtrl) m_mobDamageMultiplierCtrl->Enable(enabled);
    if (m_shipsHealthMultiplierCtrl) m_shipsHealthMultiplierCtrl->Enable(enabled);
    if (m_shipsDamageMultiplierCtrl) m_shipsDamageMultiplierCtrl->Enable(enabled);
    if (m_boardingDifficultyMultiplierCtrl) m_boardingDifficultyMultiplierCtrl->Enable(enabled);
    if (m_coopStatsCorrectionCtrl) m_coopStatsCorrectionCtrl->Enable(enabled);
    if (m_coopShipStatsCorrectionCtrl) m_coopShipStatsCorrectionCtrl->Enable(enabled);
    if (m_combatDifficultyChoice) m_combatDifficultyChoice->Enable(enabled);
    if (m_validateJsonButton) m_validateJsonButton->Enable(enabled);
    if (m_autoBackupChk) m_autoBackupChk->Enable(enabled);
    UpdateBackupScheduler();
}

std::string MainFrame::GetStateFilePath() const {
    namespace fs = std::filesystem;
#ifdef _WIN32
    char exePath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return "wrsmanager_state.ini";
    }
    return (fs::path(exePath).parent_path() / "wrsmanager_state.ini").string();
#else
    return (fs::current_path() / "wrsmanager_state.ini").string();
#endif
}

void MainFrame::LoadAppState() {
    const std::string statePath = GetStateFilePath();
    std::ifstream in(statePath);
    if (!in.is_open()) {
        if (m_logger) {
            m_logger->Log(LogLevel::Debug, wxT("[DEBUG] No saved state found yet.\n"));
        }
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "last_install_dir") {
            m_lastInstallDir = wxString::FromUTF8(value.c_str());
        } else if (key == "steamcmd_installed") {
            m_steamcmdInstalled = (value == "1");
        } else if (key == "server_installed") {
            m_serverInstalled = (value == "1");
        }
    }

    if (m_logger) {
        m_logger->Log(LogLevel::Debug,
                      wxString::Format(wxT("[DEBUG] Loaded state from: %s\n"),
                                       wxString::FromUTF8(statePath.c_str())));
        if (!m_lastInstallDir.IsEmpty()) {
            m_logger->Log(LogLevel::Note,
                          wxString::Format(wxT("[NOTE] Last selected folder: %s\n"), m_lastInstallDir));
        }
        m_logger->Log(LogLevel::Info,
                      wxString::Format(wxT("[INFO] Saved flags: steamcmd=%s, server=%s\n"),
                                       m_steamcmdInstalled ? wxT("true") : wxT("false"),
                                       m_serverInstalled ? wxT("true") : wxT("false")));
    }
}

void MainFrame::SaveAppState() const {
    const std::string statePath = GetStateFilePath();
    std::ofstream out(statePath, std::ios::trunc);
    if (!out.is_open()) {
        return;
    }

    out << "last_install_dir=" << m_lastInstallDir.ToStdString() << "\n";
    out << "steamcmd_installed=" << (m_steamcmdInstalled ? "1" : "0") << "\n";
    out << "server_installed=" << (m_serverInstalled ? "1" : "0") << "\n";
}

void MainFrame::SetBusyButtons(bool busy) {
    if (!m_installButton || !m_installServerButton || !m_startServerButton || !m_stopServerButton) {
        return;
    }
    if (busy) {
        m_installButton->Enable(false);
        m_installServerButton->Enable(false);
        m_startServerButton->Enable(false);
        m_stopServerButton->Enable(false);
        SetJsonControlsEnabled(false);
        return;
    }

    if (m_startServerRunning) {
        m_installButton->Enable(false);
        m_installServerButton->Enable(false);
        m_startServerButton->Enable(false);
        m_stopServerButton->Enable(true);
        SetJsonControlsEnabled(false);
        return;
    }

    m_installButton->Enable(true);
    m_installServerButton->Enable(m_steamcmdInstalled);
    m_startServerButton->Enable(m_serverInstalled && !m_startServerRunning);
    m_stopServerButton->Enable(m_startServerRunning);
    SetJsonControlsEnabled(m_serverInstalled);
}

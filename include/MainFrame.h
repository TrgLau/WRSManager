#pragma once

#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <wx/choice.h>
#include <wx/timer.h>
#include <memory>
#include <string>

#include "TerminalLogger.h"

class MainFrame : public wxFrame {
public:
    MainFrame();

private:
    void OnExit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnInstallClicked(wxCommandEvent& event);
    void OnInstallServerClicked(wxCommandEvent& event);
    void OnStartServerClicked(wxCommandEvent& event);
    void OnStopServerClicked(wxCommandEvent& event);
    void OnValidateJsonClicked(wxCommandEvent& event);
    void OnAutoBackupChanged(wxCommandEvent& event);
    void OnBackupIntervalChanged(wxCommandEvent& event);
    void OnBackupTimer(wxTimerEvent& event);
    void OnNotebookLeftClick(wxMouseEvent& event);
    void OnNotebookPageChanged(wxBookCtrlEvent& event);
    void AppendNewTabAfterPlus();
    std::string GetStateFilePath() const;
    void LoadAppState();
    void SaveAppState() const;
    void SetBusyButtons(bool busy);
    bool EnsureBaseInstallDir();
    bool CreateTab(const wxString& tabName);
    std::string CurrentTabInstallDir() const;
    std::string CurrentServerDescriptionPath() const;
    std::string CurrentLegacyServerDescriptionPath() const;
    void LoadServerDescriptionToControls();
    void SaveServerDescriptionFromControls();
    void SetJsonControlsEnabled(bool enabled);
    void UpdateBackupScheduler();
    void RunBackupNowAsync();
    int GetSelectedBackupMinutes() const;
    int GetMaxBackupsToKeep() const;

    wxNotebook* m_notebook = nullptr;
    wxPanel* m_plusPage = nullptr;
    wxTextCtrl* m_terminalOutput = nullptr;
    wxButton* m_installButton = nullptr;
    wxButton* m_installServerButton = nullptr;
    wxButton* m_startServerButton = nullptr;
    wxButton* m_stopServerButton = nullptr;
    wxButton* m_validateJsonButton = nullptr;
    wxCheckBox* m_autoBackupChk = nullptr;
    wxChoice* m_backupIntervalChoice = nullptr;
    wxSpinCtrl* m_maxBackupsCtrl = nullptr;
    wxTextCtrl* m_inviteCodeCtrl = nullptr;
    wxCheckBox* m_passwordProtectedChk = nullptr;
    wxTextCtrl* m_passwordCtrl = nullptr;
    wxTextCtrl* m_serverNameCtrl = nullptr;
    wxSpinCtrl* m_maxPlayerCountCtrl = nullptr;
    wxTextCtrl* m_userSelectedRegionCtrl = nullptr;
    wxCheckBox* m_useDirectConnectionChk = nullptr;
    wxTextCtrl* m_directConnectionServerAddressCtrl = nullptr;
    wxSpinCtrl* m_directConnectionServerPortCtrl = nullptr;
    wxTextCtrl* m_directConnectionProxyAddressCtrl = nullptr;
    wxTextCtrl* m_worldIslandIdCtrl = nullptr;
    wxTextCtrl* m_worldNameCtrl = nullptr;
    wxChoice* m_worldPresetChoice = nullptr;
    wxCheckBox* m_sharedQuestsChk = nullptr;
    wxCheckBox* m_easyExploreChk = nullptr;
    wxTextCtrl* m_mobHealthMultiplierCtrl = nullptr;
    wxTextCtrl* m_mobDamageMultiplierCtrl = nullptr;
    wxTextCtrl* m_shipsHealthMultiplierCtrl = nullptr;
    wxTextCtrl* m_shipsDamageMultiplierCtrl = nullptr;
    wxTextCtrl* m_boardingDifficultyMultiplierCtrl = nullptr;
    wxTextCtrl* m_coopStatsCorrectionCtrl = nullptr;
    wxTextCtrl* m_coopShipStatsCorrectionCtrl = nullptr;
    wxChoice* m_combatDifficultyChoice = nullptr;
    std::unique_ptr<TerminalLogger> m_logger;
    wxString m_lastInstallDir;
    int m_nextTabNum = 2;
    bool m_steamcmdInstalled = false;
    bool m_serverInstalled = false;
    bool m_startServerRunning = false;
    unsigned long m_startServerPid = 0;
    bool m_backupInProgress = false;
    wxTimer m_backupTimer;

    wxDECLARE_EVENT_TABLE();
};

#pragma once

#include "ServerConfig.h"
#include <shellapi.h>

class CMainWnd : public WindowImplBase
{
public:
	CMainWnd();
	~CMainWnd();

	CDuiString GetSkinFile() override;
	CDuiString GetSkinFolder();
	LPCTSTR GetWindowClassName() const override;
	void InitWindow() override;
	void OnFinalMessage(HWND hWnd) override;
	LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) override;
	LRESULT OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) override;
	LRESULT HandleCustomMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) override;

	DUI_DECLARE_MESSAGE_MAP()
	void OnClick(TNotifyUI& msg) override;
	void OnSelectChanged(TNotifyUI& msg);
	void OnMenu(TNotifyUI& msg);
	void OnCategoryFold(TNotifyUI& msg);

private:
	void SelectPage(int index);
	void SetStatusMessage(LPCTSTR text, bool writeLog = true);
	void OnDropFiles(HDROP hDrop);
	bool AddServerFolder(const std::wstring& folder);
	void LoadSavedServerFolders();
	void SetNavVisible(bool show);
	AppSettings CurrentSettings();
	void RebuildServerGroups();
	void RefreshRunningServers();
	bool FindServerBySender(CControlUI* sender, const ServerConfig*& server) const;
	bool FindServerByName(const CDuiString& name, const ServerConfig*& server, size_t* groupIndex = NULL) const;
	void ShowServerMenu(CControlUI* serverButton, POINT ptScreen);
	void RemoveServerFolder(size_t group);
	void ShowTrayMenu(POINT ptScreen);
	bool OnServerMenuCommand(WPARAM wParam);
	void OpenChosenFeature(bool actor, size_t group, const ServerConfig& server);
	void OnStartServer(const ServerConfig& server);
	void OnStopServer(const ServerConfig& server);
	void OnHotUpdateServer(const ServerConfig& server);
	void OnClearServer(const ServerConfig& server);

	struct ChosenServer
	{
		bool has;
		std::wstring folder;
		ServerConfig config;

		ChosenServer() : has(false) {}
	};

	bool PickChosenServer(LPCTSTR feature, ChosenServer& chosen, LPCTSTR labelName);
	void SetActorSqlPath(const std::wstring& path);
	void SetActorImportBusy(bool busy);
	void OnImportActor();
	void SetSrvSqlPath(const std::wstring& path);
	void SetSrvImportBusy(bool busy);
	void OnImportServer();
	void UpdateChosenLabel(LPCTSTR labelName, const ChosenServer& chosen);
	bool RestoreChosenServer(const ServerPick& pick, ChosenServer& chosen, LPCTSTR labelName);
	ServerPick ToPick(const ChosenServer& chosen) const;

	CTabLayoutUI* m_pPages;
	CLabelUI* m_pStatusMessage;
	CContainerUI* m_pServerGroups;
	CVirtualListUI* m_pLog;
	IVirtualListCallback* m_pLogCallback;
	std::vector<std::wstring> m_logLines;
	std::vector<ServerFolder> m_serverFolders;
	std::vector<std::wstring> m_collapsedFolders;
	CMenuWnd* m_pServerMenu;
	CMenuWnd* m_pTrayMenu;
	CDuiString m_menuServerName;
	ChosenServer m_actorServer;
	ChosenServer m_srvServer;
	std::wstring m_actorSqlPath;
	bool m_actorImporting;
	std::wstring m_srvSqlPath;
	bool m_srvImporting;
};

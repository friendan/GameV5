#include "StdAfx.h"
#include "duilib.h"
#include "ServerAction.h"

#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>

namespace {

	struct NavPage
	{
		LPCTSTR name;
		int index;
		LPCTSTR status;
	};

	const UINT_PTR kServerStateTimer = 1;

	bool EndsWithSql(const std::wstring& path);
	bool IsProtectedServerDatabase(const std::wstring& name);

	const NavPage kNavPages[] = {
		{ _T("nav_start"),      0, _T("当前：服务器") },
		{ _T("nav_export"),     1, _T("当前：导号") },
		{ _T("nav_export_srv"), 2, _T("当前：导服") },
		{ _T("nav_log"),        3, _T("当前：日志") },
	};

	struct LogListCallback : IVirtualListCallback
	{
		const std::vector<std::wstring>* lines;
		mutable CDuiString text;

		explicit LogListCallback(const std::vector<std::wstring>* source) : lines(source) {}

		LPCTSTR GetItemText(CControlUI*, int iIndex) override
		{
			if( lines == NULL || iIndex < 0 || iIndex >= (int)lines->size() )
				return _T("");
			text = (*lines)[(size_t)iIndex].c_str();
			return text.GetData();
		}
	};

} // namespace

DUI_BEGIN_MESSAGE_MAP(CMainWnd, WindowImplBase)
	DUI_ON_MSGTYPE(DUI_MSGTYPE_CLICK, CMainWnd::OnClick)
	DUI_ON_MSGTYPE(DUI_MSGTYPE_SELECTCHANGED, CMainWnd::OnSelectChanged)
	DUI_ON_MSGTYPE(DUI_MSGTYPE_MENU, CMainWnd::OnMenu)
	DUI_ON_MSGTYPE(DUI_MSGTYPE_ITEMEXPAND, CMainWnd::OnCategoryFold)
	DUI_ON_MSGTYPE(DUI_MSGTYPE_ITEMCOLLAPSE, CMainWnd::OnCategoryFold)
DUI_END_MESSAGE_MAP()

CMainWnd::CMainWnd()
	: m_pPages(NULL)
	, m_pStatusMessage(NULL)
	, m_pServerGroups(NULL)
	, m_pLog(NULL)
	, m_pLogCallback(new LogListCallback(&m_logLines))
	, m_pServerMenu(NULL)
	, m_pTrayMenu(NULL)
	, m_actorImporting(false)
	, m_srvImporting(false)
{
}

CMainWnd::~CMainWnd()
{
	delete m_pServerMenu;
	m_pServerMenu = NULL;
	delete m_pTrayMenu;
	m_pTrayMenu = NULL;
	delete m_pLogCallback;
	m_pLogCallback = NULL;
}

CDuiString CMainWnd::GetSkinFile()
{
	return _T("main.html");
}

CDuiString CMainWnd::GetSkinFolder()
{
	return _T("");
}

LPCTSTR CMainWnd::GetWindowClassName() const
{
	return _T("GameV5MainWnd");
}

void CMainWnd::InitWindow()
{
	SetIcon(IDI_GAMEV5);
	if( GetTrayIcon().Create(m_hWnd, 1, (UINT)IDI_GAMEV5, _T("GameV5"), 0, NULL) )
		GetTrayIcon().SetNotifyVersion(NOTIFYICON_VERSION_4);
	m_pPages = static_cast<CTabLayoutUI*>(m_pm.FindControl(_T("pages")));
	m_pStatusMessage = static_cast<CLabelUI*>(m_pm.FindControl(_T("status_message")));
	m_pServerGroups = static_cast<CContainerUI*>(m_pm.FindControl(_T("server_groups")));
	CControlUI* pLog = m_pm.FindControl(_T("log_view"));
	if( pLog != NULL )
		m_pLog = static_cast<CVirtualListUI*>(pLog->GetInterface(DUI_CTR_VIRTUALLIST));
	if( m_pLog != NULL )
		m_pLog->SetCallback(m_pLogCallback);
	::DragAcceptFiles(m_hWnd, TRUE);
	::ChangeWindowMessageFilter(WM_DROPFILES, MSGFLT_ADD);
	::ChangeWindowMessageFilter(WM_COPYDATA, MSGFLT_ADD);
	::ChangeWindowMessageFilter(0x0049, MSGFLT_ADD);
	SelectPage(0);
	LoadSavedServerFolders();
}

void CMainWnd::OnFinalMessage(HWND hWnd)
{
	WindowImplBase::OnFinalMessage(hWnd);
}

LRESULT CMainWnd::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	GetTrayIcon().DeleteTrayIcon();
	const LRESULT result = WindowImplBase::OnDestroy(uMsg, wParam, lParam, bHandled);
	::PostQuitMessage(0);
	return result;
}

LRESULT CMainWnd::OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	::ScreenToClient(m_hWnd, &pt);
	for( CControlUI* p = m_pm.FindControl(pt); p != NULL; p = p->GetParent() ) {
		if( p->GetName() == _T("btn_nav_toggle") ) {
			bHandled = TRUE;
			return HTCLIENT;
		}
	}
	return WindowImplBase::OnNcHitTest(uMsg, wParam, lParam, bHandled);
}

LRESULT CMainWnd::HandleCustomMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	if( uMsg == WM_RBUTTONDOWN )
		m_pm.HideControlToolTip();
	if( uMsg == WM_DROPFILES ) {
		OnDropFiles((HDROP)wParam);
		bHandled = TRUE;
		return 0;
	}
	if( uMsg == WM_MENUCLICK && OnServerMenuCommand(wParam) ) {
		bHandled = TRUE;
		return 0;
	}
	if( uMsg == CTrayIcon::GetTaskbarCreatedMsg() ) {
		GetTrayIcon().Recreate();
		bHandled = TRUE;
		return 0;
	}
	if( uMsg == UIMSG_TRAYICON ) {
		const UINT ev = CTrayIcon::DecodeNotifyMsg(wParam, lParam, GetTrayIcon().GetNotifyVersion());
		if( ev == WM_LBUTTONUP || ev == WM_LBUTTONDBLCLK ) {
			CTrayIcon::ShowWindowOnTaskbar(m_hWnd, true);
		}
		else if( ev == WM_RBUTTONUP ) {
			ShowTrayMenu(CTrayIcon::DecodeNotifyPos(wParam, lParam, GetTrayIcon().GetNotifyVersion()));
		}
		bHandled = TRUE;
		return 0;
	}
	if( uMsg == WM_SERVER_GM_RESULT ) {
		std::wstring* text = (std::wstring*)wParam;
		if( text != NULL ) {
			SetStatusMessage(text->c_str());
			delete text;
		}
		if( lParam == 1 )
			SetActorImportBusy(false);
		if( lParam == 2 )
			SetSrvImportBusy(false);
		RefreshRunningServers();
		bHandled = TRUE;
		return 0;
	}
	if( uMsg == WM_TIMER && wParam == kServerStateTimer ) {
		RefreshRunningServers();
		bHandled = TRUE;
		return 0;
	}
	return WindowImplBase::HandleCustomMessage(uMsg, wParam, lParam, bHandled);
}

void CMainWnd::OnClick(TNotifyUI& msg)
{
	if( msg.pSender == NULL ) {
		WindowImplBase::OnClick(msg);
		return;
	}
	const ServerConfig* server = NULL;
	if( FindServerBySender(msg.pSender, server) && server != NULL ) {
		TCHAR status[256];
		_sntprintf_s(status, _TRUNCATE, _T("服务器 %d  登录 %d  GM %d"),
			server->hostId,
			server->loginPort,
			server->mcsPort);
		SetStatusMessage(status);
		return;
	}
	if( msg.pSender->GetName() == _T("btn_pick_export_server") ) {
		PickChosenServer(_T("导号"), m_actorServer, _T("export_server_id"));
		return;
	}
	if( msg.pSender->GetName() == _T("btn_pick_export_srv") ) {
		PickChosenServer(_T("导服"), m_srvServer, _T("export_srv_id"));
		return;
	}
	if( msg.pSender->GetName() == _T("btn_pick_actor_sql") ) {
		wchar_t file[MAX_PATH] = {};
		OPENFILENAMEW dialog = {};
		dialog.lStructSize = sizeof(dialog);
		dialog.hwndOwner = m_hWnd;
		dialog.lpstrFilter = L"SQL 文件\0*.sql\0";
		dialog.lpstrFile = file;
		dialog.nMaxFile = MAX_PATH;
		dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
		dialog.lpstrTitle = L"选择玩家 SQL 文件";
		if( ::GetOpenFileNameW(&dialog) )
			SetActorSqlPath(file);
		return;
	}
	if( msg.pSender->GetName() == _T("btn_import_actor") ) {
		OnImportActor();
		return;
	}
	if( msg.pSender->GetName() == _T("btn_pick_srv_sql") ) {
		wchar_t file[MAX_PATH] = {};
		OPENFILENAMEW dialog = {};
		dialog.lStructSize = sizeof(dialog);
		dialog.hwndOwner = m_hWnd;
		dialog.lpstrFilter = L"数据库备份\0*.sql\0";
		dialog.lpstrFile = file;
		dialog.nMaxFile = MAX_PATH;
		dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
		dialog.lpstrTitle = L"选择数据库备份";
		if( ::GetOpenFileNameW(&dialog) )
			SetSrvSqlPath(file);
		return;
	}
	if( msg.pSender->GetName() == _T("btn_import_server") ) {
		OnImportServer();
		return;
	}
	if( msg.pSender->GetName() == _T("btn_clear_log") ) {
		m_logLines.clear();
		if( m_pLog != NULL ) {
			m_pLog->SetItemCount(0);
			m_pLog->Invalidate();
		}
		if( m_pStatusMessage != NULL ) {
			m_pStatusMessage->SetText(_T("日志已清空"));
			m_pStatusMessage->SetToolTip(_T(""));
		}
		return;
	}
	if( msg.pSender->GetName() == _T("btn_nav_toggle") ) {
		CControlUI* pNav = m_pm.FindControl(_T("nav_panel"));
		if( pNav != NULL ) {
			const bool show = !pNav->IsVisible();
			SetNavVisible(show);
			if( !SaveAppSettings(CurrentSettings()) )
				SetStatusMessage(_T("导航状态未能保存"));
		}
		return;
	}
	WindowImplBase::OnClick(msg);
}

void CMainWnd::OnSelectChanged(TNotifyUI& msg)
{
	if( msg.pSender == NULL ) return;
	COptionUI* option = static_cast<COptionUI*>(msg.pSender->GetInterface(DUI_CTR_OPTION));
	if( option != NULL && !option->IsSelected() )
		return;
	CDuiString name = msg.pSender->GetName();
	for( int i = 0; i < (int)(sizeof(kNavPages) / sizeof(kNavPages[0])); ++i ) {
		if( name.CompareNoCase(kNavPages[i].name) != 0 )
			continue;
		SelectPage(kNavPages[i].index);
		return;
	}
}

void CMainWnd::SelectPage(int index)
{
	if( m_pPages != NULL )
		m_pPages->SelectItem(index);
	if( index == 0 )
		::SetTimer(m_hWnd, kServerStateTimer, 2000, NULL);
	else
		::KillTimer(m_hWnd, kServerStateTimer);
	RefreshRunningServers();

	LPCTSTR status = _T("就绪");
	CDuiString exportStatus;
	for( int i = 0; i < (int)(sizeof(kNavPages) / sizeof(kNavPages[0])); ++i ) {
		if( kNavPages[i].index == index ) {
			status = kNavPages[i].status;
			break;
		}
	}
	const ChosenServer* chosen = NULL;
	LPCTSTR feature = NULL;
	LPCTSTR labelName = NULL;
	if( index == 1 ) {
		chosen = &m_actorServer;
		feature = _T("当前：导号");
		labelName = _T("export_server_id");
	}
	else if( index == 2 ) {
		chosen = &m_srvServer;
		feature = _T("当前：导服");
		labelName = _T("export_srv_id");
	}
	if( chosen != NULL && chosen->has ) {
		UpdateChosenLabel(labelName, *chosen);
		exportStatus.Format(_T("%s  服务器 id：%d  分类：%s"), feature, chosen->config.hostId, chosen->folder.c_str());
		status = exportStatus.GetData();
	}
	SetStatusMessage(status, false);
}

void CMainWnd::UpdateChosenLabel(LPCTSTR labelName, const ChosenServer& chosen)
{
	CLabelUI* label = static_cast<CLabelUI*>(m_pm.FindControl(labelName));
	if( label == NULL )
		return;
	if( !chosen.has ) {
		label->SetText(_T("未选择服务器"));
		label->SetToolTip(_T(""));
		return;
	}
	CDuiString text;
	text.Format(_T("服务器 id：%d    分类：%s"), chosen.config.hostId, chosen.folder.c_str());
	label->SetText(text.GetData());
	label->SetToolTip(_T(""));
}

void CMainWnd::SetStatusMessage(LPCTSTR text, bool writeLog)
{
	if( m_pStatusMessage == NULL || text == NULL )
		return;
	SYSTEMTIME now = {};
	::GetLocalTime(&now);
	CDuiString line;
	line.Format(_T("%04u-%02u-%02u %02u:%02u:%02u.%03u  %s"), now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text);
	m_pStatusMessage->SetText(line.GetData());
	m_pStatusMessage->SetToolTip(_T(""));
	if( !writeLog || m_pLog == NULL )
		return;
	m_logLines.push_back(line.GetData());
	const int kMaxLogLines = 10000;
	if( (int)m_logLines.size() > kMaxLogLines ) {
		const size_t drop = m_logLines.size() - (size_t)kMaxLogLines;
		m_logLines.erase(m_logLines.begin(), m_logLines.begin() + drop);
	}
	m_pLog->SetItemCount((int)m_logLines.size());
	m_pLog->Invalidate();
	if( !m_logLines.empty() )
		m_pLog->EnsureVisible((int)m_logLines.size() - 1);
}

namespace {

	std::wstring FormatServerTip(const ServerConfig& server)
	{
		std::wstring tip = _T("文件：") + server.fileName;
		TCHAR line[128];
		_sntprintf_s(line, _TRUNCATE, _T("\n服务器id：%d"), server.hostId);
		tip += line;
		if( server.loginPort >= 0 ) {
			_sntprintf_s(line, _TRUNCATE, _T("\n登录端口：%d"), server.loginPort);
			tip += line;
		}
		if( server.mcsPort >= 0 ) {
			_sntprintf_s(line, _TRUNCATE, _T("\nGM端口：%d"), server.mcsPort);
			tip += line;
		}
		if( !server.dbHost.empty() ) {
			tip += _T("\n数据库：") + server.dbHost;
			if( server.dbPort >= 0 ) {
				_sntprintf_s(line, _TRUNCATE, _T(":%d"), server.dbPort);
				tip += line;
			}
			if( !server.dbName.empty() )
				tip += _T(" / ") + server.dbName;
			if( !server.dbUser.empty() )
				tip += _T("\n用户：") + server.dbUser;
		}
		return tip;
	}

	int CountServers(const std::vector<ServerFolder>& folders)
	{
		int count = 0;
		for( size_t i = 0; i < folders.size(); ++i )
			count += (int)folders[i].servers.size();
		return count;
	}

	void StyleCategoryItem(CAccordionItemUI* item)
	{
		item->SetAttribute(_T("header-height"), _T("32"));
		item->SetAttribute(_T("header-color"), _T("var(--color-text)"));
		item->SetAttribute(_T("header-background-color"), _T("var(--color-bg-hover)"));
		item->SetAttribute(_T("header-background-color-hover"), _T("var(--color-bg-hover-medium)"));
		item->SetAttribute(_T("header-background-color-active"), _T("var(--color-bg-hover)"));
		item->SetAttribute(_T("header-background-color-active-hover"), _T("var(--color-bg-hover-medium)"));
		item->SetAttribute(_T("background-color"), _T("#00000000"));
		item->SetAttribute(_T("border-color"), _T("var(--color-border)"));
		item->SetAttribute(_T("gap"), _T("8"));
	}

	CFlowLayoutUI* AddCategoryServers(CAccordionItemUI* item, const ServerFolder& folder, size_t group, LPCTSTR nameFormat, bool contextMenu)
	{
		if( folder.servers.empty() ) {
			CLabelUI* empty = new CLabelUI;
			empty->SetText(_T("没有识别到服务器配置"));
			empty->SetFixedHeight(22);
			empty->SetAttribute(_T("theme"), _T("secondary"));
			item->Add(empty);
			return NULL;
		}
		CFlowLayoutUI* flow = new CFlowLayoutUI;
		flow->SetAttribute(_T("gap"), _T("8"));
		flow->SetAttribute(_T("line-spacing"), _T("8"));
		for( size_t i = 0; i < folder.servers.size(); ++i ) {
			const ServerConfig& server = folder.servers[i];
			CButtonUI* button = new CButtonUI;
			TCHAR name[64];
			_sntprintf_s(name, _TRUNCATE, nameFormat, (unsigned)group, (unsigned)i);
			button->SetName(name);
			TCHAR idText[32];
			_sntprintf_s(idText, _TRUNCATE, _T("%d"), server.hostId);
			button->SetText(idText);
			button->SetFixedWidth(96);
			button->SetFixedHeight(96);
			button->SetAttribute(_T("kind"), _T("none"));
			button->SetAttribute(_T("lucide"), _T("server"));
			button->SetAttribute(_T("icon-position"), _T("top"));
			button->SetAttribute(_T("icon-size"), _T("36"));
			button->SetAttribute(_T("border-radius"), _T("8"));
			button->SetAttribute(_T("color"), _T("var(--color-text)"));
			button->SetAttribute(_T("background-color-hover"), _T("var(--color-bg-hover-medium)"));
			button->SetToolTip(FormatServerTip(server).c_str());
			if( contextMenu )
				button->SetContextMenuUsed(true);
			flow->Add(button);
		}
		item->Add(flow);
		return flow;
	}

	class CPickServerWnd : public WindowImplBase
	{
	public:
		explicit CPickServerWnd(const std::vector<ServerFolder>* folders)
			: m_folders(folders)
			, m_group(-1)
			, m_index(-1)
		{
		}

		bool Accepted() const { return m_group >= 0 && m_index >= 0; }
		int Group() const { return m_group; }
		int Index() const { return m_index; }

		CDuiString GetSkinFile() override { return _T("pick_server.html"); }
		CDuiString GetSkinFolder() { return _T(""); }
		LPCTSTR GetWindowClassName() const override { return _T("GameV5PickServerWnd"); }

		void InitWindow() override
		{
			CContainerUI* groups = static_cast<CContainerUI*>(m_pm.FindControl(_T("pick_groups")));
			if( groups == NULL || m_folders == NULL )
				return;
			for( size_t g = 0; g < m_folders->size(); ++g ) {
				const ServerFolder& folder = (*m_folders)[g];
				if( folder.servers.empty() )
					continue;
				CAccordionItemUI* item = new CAccordionItemUI;
				StyleCategoryItem(item);
				CDuiString title;
				title.Format(_T("%d 台    %s"), (int)folder.servers.size(), folder.path.c_str());
				item->SetTitle(title.GetData());
				AddCategoryServers(item, folder, g, _T("pick_%u_%u"), false);
				item->SetActive(true, false);
				groups->Add(item);
			}
		}

		void OnClick(TNotifyUI& msg) override
		{
			if( msg.pSender == NULL ) {
				WindowImplBase::OnClick(msg);
				return;
			}
			unsigned group = 0;
			unsigned index = 0;
			if( _stscanf_s(msg.pSender->GetName().GetData(), _T("pick_%u_%u"), &group, &index) == 2 ) {
				m_group = (int)group;
				m_index = (int)index;
				Close(IDOK);
				return;
			}
			if( msg.pSender->GetName() == _T("closebtn") || msg.pSender->GetAction() == UIACTION_CLOSE ) {
				Close(IDCANCEL);
				return;
			}
			WindowImplBase::OnClick(msg);
		}

	private:
		const std::vector<ServerFolder>* m_folders;
		int m_group;
		int m_index;
	};

} // namespace

bool CMainWnd::PickChosenServer(LPCTSTR feature, ChosenServer& chosen, LPCTSTR labelName)
{
	CDuiString text;
	if( CountServers(m_serverFolders) <= 0 ) {
		text.Format(_T("%s：没有可用服务器，请先拖入配置文件夹"), feature);
		SetStatusMessage(text.GetData());
		return false;
	}
	text.Format(_T("%s：请选择服务器"), feature);
	SetStatusMessage(text.GetData());
	CPickServerWnd dialog(&m_serverFolders);
	dialog.Create(m_hWnd, _T("选择服务器"), UI_WNDSTYLE_FRAME, 0L, 0, 0, 720, 520);
	dialog.CenterWindow();
	dialog.ShowModal();
	if( !dialog.Accepted() ) {
		text.Format(_T("%s：已取消"), feature);
		SetStatusMessage(text.GetData());
		return false;
	}
	const int group = dialog.Group();
	const int index = dialog.Index();
	if( group < 0 || index < 0 || (size_t)group >= m_serverFolders.size() || (size_t)index >= m_serverFolders[(size_t)group].servers.size() ) {
		text.Format(_T("%s：已取消"), feature);
		SetStatusMessage(text.GetData());
		return false;
	}
	chosen.folder = m_serverFolders[(size_t)group].path;
	chosen.config = m_serverFolders[(size_t)group].servers[(size_t)index];
	chosen.has = true;
	UpdateChosenLabel(labelName, chosen);
	text.Format(_T("%s：已选择服务器 id：%d  分类：%s"), feature, chosen.config.hostId, chosen.folder.c_str());
	SetStatusMessage(text.GetData());
	if( !SaveAppSettings(CurrentSettings()) ) {
		text.Format(_T("%s：服务器选择未能保存"), feature);
		SetStatusMessage(text.GetData());
	}
	return true;
}

void CMainWnd::SetActorSqlPath(const std::wstring& path)
{
	m_actorSqlPath = path;
	CLabelUI* label = static_cast<CLabelUI*>(m_pm.FindControl(_T("actor_sql_file")));
	if( label == NULL )
		return;
	label->SetText(path.empty() ? _T("未选择 SQL 文件") : path.c_str());
	label->SetToolTip(_T(""));
}

void CMainWnd::OnImportActor()
{
	if( m_actorImporting ) {
		SetStatusMessage(_T("导号：正在导入"));
		return;
	}
	if( !m_actorServer.has || m_actorServer.config.dbHost.empty() || m_actorServer.config.dbName.empty() ) {
		SetStatusMessage(_T("导号：请先选择带数据库配置的服务器"));
		return;
	}
	if( m_actorSqlPath.size() < 5 || _wcsicmp(m_actorSqlPath.c_str() + m_actorSqlPath.size() - 4, L".sql") != 0 ) {
		SetStatusMessage(_T("导号：请选择 SQL 文件"));
		return;
	}
	if( ::GetFileAttributesW(m_actorSqlPath.c_str()) == INVALID_FILE_ATTRIBUTES ) {
		SetStatusMessage(_T("导号：SQL 文件不存在"));
		return;
	}
	SetActorImportBusy(true);
	CDuiString text;
	text.Format(_T("导号：开始导入 %s"), m_actorSqlPath.c_str());
	SetStatusMessage(text.GetData());
	ImportActorSql(m_hWnd, m_actorServer.config, m_actorSqlPath);
}

void CMainWnd::SetActorImportBusy(bool busy)
{
	m_actorImporting = busy;
	const LPCTSTR names[] = {
		_T("btn_pick_export_server"),
		_T("btn_pick_actor_sql"),
		_T("btn_import_actor"),
	};
	for( int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i ) {
		CControlUI* control = m_pm.FindControl(names[i]);
		if( control != NULL )
			control->SetEnabled(!busy);
	}
}

void CMainWnd::SetSrvSqlPath(const std::wstring& path)
{
	m_srvSqlPath = path;
	CLabelUI* label = static_cast<CLabelUI*>(m_pm.FindControl(_T("srv_sql_file")));
	if( label == NULL )
		return;
	label->SetText(path.empty() ? _T("未选择备份文件") : path.c_str());
	label->SetToolTip(_T(""));
}

void CMainWnd::SetSrvImportBusy(bool busy)
{
	m_srvImporting = busy;
	const LPCTSTR names[] = {
		_T("btn_pick_export_srv"),
		_T("btn_pick_srv_sql"),
		_T("btn_import_server"),
	};
	for( int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i ) {
		CControlUI* control = m_pm.FindControl(names[i]);
		if( control != NULL )
			control->SetEnabled(!busy);
	}
}

void CMainWnd::OnImportServer()
{
	if( m_srvImporting ) {
		SetStatusMessage(_T("导服：正在导入"));
		return;
	}
	if( !m_srvServer.has || m_srvServer.config.dbHost.empty() || m_srvServer.config.dbName.empty() ) {
		SetStatusMessage(_T("导服：请先选择带数据库配置的服务器"));
		return;
	}
	if( IsProtectedServerDatabase(m_srvServer.config.dbName) ) {
		SetStatusMessage(_T("导服：该数据库禁止导入"));
		return;
	}
	if( !EndsWithSql(m_srvSqlPath) ) {
		SetStatusMessage(_T("导服：请选择 .sql 备份"));
		return;
	}
	if( ::GetFileAttributesW(m_srvSqlPath.c_str()) == INVALID_FILE_ATTRIBUTES ) {
		SetStatusMessage(_T("导服：备份文件不存在"));
		return;
	}
	std::vector<SkynetProcess> processes;
	EnumerateSkynetProcesses(processes);
	if( IsSkynetServerRunning(m_srvServer.config, processes) ) {
		SetStatusMessage((L"导服 " + std::to_wstring(m_srvServer.config.hostId) + L"：未执行。服务器正在运行，请先停服").c_str());
		CMessageBox::Show(m_hWnd, _T("导服"), _T("服务器正在运行，请先停服后再导入。"),
			CModalOptions()
				.Kind(CONTROLKIND_WARNING)
				.ShowCancel(false)
				.OkText(_T("确定"))
				.Width(420)
				.Height(180));
		return;
	}
	std::wstring text = L"确定导入服务器 " + std::to_wstring(m_srvServer.config.hostId) + L" ？\n数据库：" + m_srvServer.config.dbHost;
	if( m_srvServer.config.dbPort > 0 )
		text += L":" + std::to_wstring(m_srvServer.config.dbPort);
	text += L" / " + m_srvServer.config.dbName;
	text += L"\n备份：" + m_srvSqlPath;
	text += L"\n将删除该库中备份里出现的表和存储过程，再写入备份。此操作不能撤销。";
	const int answer = CMessageBox::Show(m_hWnd, _T("导服"), text.c_str(),
		CModalOptions()
			.Kind(CONTROLKIND_DANGER)
			.OkKind(CONTROLKIND_DANGER)
			.ShowCancel(true)
			.OkText(_T("导入"))
			.CancelText(_T("取消"))
			.Width(560)
			.Height(280));
	if( answer != MESSAGEBOX_OK ) {
		SetStatusMessage((L"导服 " + std::to_wstring(m_srvServer.config.hostId) + L"：已取消").c_str());
		return;
	}
	SetSrvImportBusy(true);
	CDuiString started;
	started.Format(_T("导服：开始导入 %s"), m_srvSqlPath.c_str());
	SetStatusMessage(started.GetData());
	ImportServerDump(m_hWnd, m_srvServer.config, m_srvSqlPath);
}

namespace {

bool PointOnNamedPage(CPaintManagerUI& pm, POINT pt, LPCTSTR name)
{
	CControlUI* page = pm.FindControl(name);
	if( page == NULL || !page->IsVisible() )
		return false;
	for( CControlUI* control = pm.FindControl(pt); control != NULL; control = control->GetParent() ) {
		if( control == page )
			return true;
	}
	return false;
}

bool EndsWithText(const std::wstring& path, const wchar_t* suffix)
{
	const size_t suffixLen = wcslen(suffix);
	return path.size() >= suffixLen && _wcsicmp(path.c_str() + path.size() - suffixLen, suffix) == 0;
}

bool EndsWithSql(const std::wstring& path)
{
	return EndsWithText(path, L".sql");
}

bool IsProtectedServerDatabase(const std::wstring& name)
{
	return _wcsicmp(name.c_str(), L"actor_zqzs_trunk") == 0
		|| _wcsicmp(name.c_str(), L"actor_zqzs_release5.0") == 0;
}

} // namespace

void CMainWnd::OnDropFiles(HDROP hDrop)
{
	POINT pt = { 0, 0 };
	::DragQueryPoint(hDrop, &pt);
	const UINT count = ::DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
	std::vector<std::wstring> folders;
	std::vector<std::wstring> sqlFiles;
	bool hasFile = false;
	for( UINT i = 0; i < count; ++i ) {
		wchar_t path[MAX_PATH];
		if( ::DragQueryFileW(hDrop, i, path, MAX_PATH) == 0 )
			continue;
		const DWORD attr = ::GetFileAttributesW(path);
		if( attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0 )
			folders.push_back(path);
		else if( EndsWithSql(path) )
			sqlFiles.push_back(path);
		else
			hasFile = true;
	}
	::DragFinish(hDrop);

	const bool onServerPage = PointOnNamedPage(m_pm, pt, _T("page_start"));
	const bool onActorPage = PointOnNamedPage(m_pm, pt, _T("page_export"));
	const bool onSrvPage = PointOnNamedPage(m_pm, pt, _T("page_export_srv"));
	if( onSrvPage && m_srvImporting ) {
		SetStatusMessage(_T("导服：正在导入"));
		return;
	}
	if( onSrvPage && !sqlFiles.empty() ) {
		SetSrvSqlPath(sqlFiles[0]);
		CDuiString text;
		text.Format(_T("导服：已选择备份 %s"), sqlFiles[0].c_str());
		SetStatusMessage(text.GetData());
		return;
	}
	if( onSrvPage ) {
		SetStatusMessage(_T("请把 .sql 备份拖到导服页面"));
		return;
	}
	if( onActorPage && m_actorImporting ) {
		SetStatusMessage(_T("导号：正在导入"));
		return;
	}
	if( onActorPage && !sqlFiles.empty() ) {
		SetActorSqlPath(sqlFiles[0]);
		CDuiString text;
		text.Format(_T("导号：已选择 SQL 文件 %s"), sqlFiles[0].c_str());
		SetStatusMessage(text.GetData());
		return;
	}
	if( onActorPage ) {
		SetStatusMessage(_T("请把 SQL 文件拖到导号页面"));
		return;
	}
	if( !sqlFiles.empty() ) {
		SetStatusMessage(_T("请把 SQL 文件拖到导号页面"));
		return;
	}
	if( !onServerPage ) {
		SetStatusMessage(_T("请把文件夹拖到服务器页面"));
		return;
	}
	if( folders.empty() ) {
		SetStatusMessage(_T("请拖入配置文件夹"));
		return;
	}
	for( size_t i = 0; i < folders.size(); ++i )
		AddServerFolder(folders[i]);
	const bool savedOk = SaveAppSettings(CurrentSettings());
	TCHAR status[160];
	if( !savedOk ) {
		_sntprintf_s(status, _TRUNCATE, _T("已加载 %d 个文件夹，%d 台服务器，配置未能保存"),
			(int)m_serverFolders.size(), CountServers(m_serverFolders));
	}
	else if( hasFile ) {
		_sntprintf_s(status, _TRUNCATE, _T("已加载 %d 个文件夹，%d 台服务器（已忽略文件）"),
			(int)m_serverFolders.size(), CountServers(m_serverFolders));
	}
	else {
		_sntprintf_s(status, _TRUNCATE, _T("已加载 %d 个文件夹，%d 台服务器"),
			(int)m_serverFolders.size(), CountServers(m_serverFolders));
	}
	SetStatusMessage(status);
}

void CMainWnd::SetNavVisible(bool show)
{
	CControlUI* pNav = m_pm.FindControl(_T("nav_panel"));
	if( pNav != NULL )
		pNav->SetVisible(show);
	CControlUI* pButton = m_pm.FindControl(_T("btn_nav_toggle"));
	if( pButton != NULL )
		pButton->SetToolTip(show ? _T("隐藏功能菜单") : _T("显示功能菜单"));
}

AppSettings CMainWnd::CurrentSettings()
{
	AppSettings settings;
	CControlUI* pNav = m_pm.FindControl(_T("nav_panel"));
	settings.showNav = pNav == NULL || pNav->IsVisible();
	settings.serverFolders.reserve(m_serverFolders.size());
	for( size_t i = 0; i < m_serverFolders.size(); ++i )
		settings.serverFolders.push_back(m_serverFolders[i].path);
	settings.exportActor = ToPick(m_actorServer);
	settings.exportServer = ToPick(m_srvServer);
	for( size_t i = 0; i < m_collapsedFolders.size(); ++i ) {
		for( size_t g = 0; g < m_serverFolders.size(); ++g ) {
			if( _wcsicmp(m_collapsedFolders[i].c_str(), m_serverFolders[g].path.c_str()) == 0 ) {
				settings.collapsedFolders.push_back(m_serverFolders[g].path);
				break;
			}
		}
	}
	return settings;
}

ServerPick CMainWnd::ToPick(const ChosenServer& chosen) const
{
	ServerPick pick;
	if( !chosen.has )
		return pick;
	pick.folder = chosen.folder;
	pick.fileName = chosen.config.fileName;
	pick.hostId = chosen.config.hostId;
	pick.valid = !pick.folder.empty() && !pick.fileName.empty();
	return pick;
}

bool CMainWnd::RestoreChosenServer(const ServerPick& pick, ChosenServer& chosen, LPCTSTR labelName)
{
	size_t group = 0;
	size_t index = 0;
	if( !FindServerPick(m_serverFolders, pick, group, index) )
		return false;
	chosen.folder = m_serverFolders[group].path;
	chosen.config = m_serverFolders[group].servers[index];
	chosen.has = true;
	UpdateChosenLabel(labelName, chosen);
	return true;
}

void CMainWnd::LoadSavedServerFolders()
{
	const std::wstring path = GetAppConfigPath();
	if( ::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES )
		SaveAppSettings(AppSettings());

	AppSettings settings;
	if( !LoadAppSettings(settings) ) {
		SetStatusMessage(_T("GameV5.conf 无法读取"));
		return;
	}
	SetNavVisible(settings.showNav);
	m_collapsedFolders = settings.collapsedFolders;
	int skipped = 0;
	for( size_t i = 0; i < settings.serverFolders.size(); ++i ) {
		if( !AddServerFolder(settings.serverFolders[i]) )
			++skipped;
	}
	const bool actorRestored = !settings.exportActor.valid || RestoreChosenServer(settings.exportActor, m_actorServer, _T("export_server_id"));
	const bool srvRestored = !settings.exportServer.valid || RestoreChosenServer(settings.exportServer, m_srvServer, _T("export_srv_id"));
	if( !settings.serverFolders.empty() ) {
		CDuiString status;
		status.Format(_T("已加载 %d 个文件夹，%d 台服务器"), (int)m_serverFolders.size(), CountServers(m_serverFolders));
		SetStatusMessage(status.GetData());
	}
	if( !actorRestored )
		SetStatusMessage(_T("导号：上次选择的服务器不在当前分类中"));
	if( !srvRestored )
		SetStatusMessage(_T("导服：上次选择的服务器不在当前分类中"));
	if( skipped > 0 ) {
		CDuiString status;
		status.Format(_T("已跳过 %d 个不存在或无法读取的配置文件夹"), skipped);
		SetStatusMessage(status.GetData());
		if( !SaveAppSettings(CurrentSettings()) )
			SetStatusMessage(_T("失效的配置文件夹未能从配置中移除"));
	}
}

bool CMainWnd::AddServerFolder(const std::wstring& folder)
{
	ServerFolder loaded;
	if( !LoadServerFolder(folder, loaded) ) {
		CDuiString text;
		text.Format(_T("配置文件夹不存在或无法读取，已跳过：%s"), folder.c_str());
		SetStatusMessage(text.GetData());
		return false;
	}
	bool replaced = false;
	for( size_t i = 0; i < m_serverFolders.size(); ++i ) {
		if( _wcsicmp(m_serverFolders[i].path.c_str(), loaded.path.c_str()) == 0 ) {
			m_serverFolders[i] = loaded;
			replaced = true;
			break;
		}
	}
	if( !replaced )
		m_serverFolders.push_back(loaded);
	RebuildServerGroups();
	return true;
}

void CMainWnd::RebuildServerGroups()
{
	if( m_pServerGroups == NULL )
		return;
	m_pServerGroups->RemoveAll();
	for( size_t g = 0; g < m_serverFolders.size(); ++g ) {
		const ServerFolder& folder = m_serverFolders[g];
		CAccordionItemUI* item = new CAccordionItemUI;
		TCHAR name[64];
		_sntprintf_s(name, _TRUNCATE, _T("grp_%u"), (unsigned)g);
		item->SetName(name);
		StyleCategoryItem(item);
		CDuiString title;
		title.Format(_T("%d 台    %s"), (int)folder.servers.size(), folder.path.c_str());
		item->SetTitle(title.GetData());
		AddCategoryServers(item, folder, g, _T("srv_%u_%u"), true);
		bool collapsed = false;
		for( size_t i = 0; i < m_collapsedFolders.size(); ++i ) {
			if( _wcsicmp(m_collapsedFolders[i].c_str(), folder.path.c_str()) == 0 ) {
				collapsed = true;
				break;
			}
		}
		item->SetActive(!collapsed, false);
		item->SetContextMenuUsed(true);
		m_pServerGroups->Add(item);
	}
	RefreshRunningServers();
}

void CMainWnd::OnCategoryFold(TNotifyUI& msg)
{
	if( msg.pSender == NULL )
		return;
	unsigned group = 0;
	if( _stscanf_s(msg.pSender->GetName().GetData(), _T("grp_%u"), &group) != 1 )
		return;
	if( group >= m_serverFolders.size() )
		return;
	const std::wstring path = m_serverFolders[group].path;
	const bool collapsed = msg.sType == DUI_MSGTYPE_ITEMCOLLAPSE;
	for( size_t i = 0; i < m_collapsedFolders.size(); ) {
		if( _wcsicmp(m_collapsedFolders[i].c_str(), path.c_str()) == 0 )
			m_collapsedFolders.erase(m_collapsedFolders.begin() + i);
		else
			++i;
	}
	if( collapsed )
		m_collapsedFolders.push_back(path);
	if( !SaveAppSettings(CurrentSettings()) )
		SetStatusMessage(_T("分类折叠状态未能保存"));
}

void CMainWnd::RefreshRunningServers()
{
	if( m_pServerGroups == NULL )
		return;
	std::vector<SkynetProcess> processes;
	EnumerateSkynetProcesses(processes);
	for( size_t g = 0; g < m_serverFolders.size(); ++g ) {
		const ServerFolder& folder = m_serverFolders[g];
		for( size_t i = 0; i < folder.servers.size(); ++i ) {
			TCHAR name[64];
			_sntprintf_s(name, _TRUNCATE, _T("srv_%u_%u"), (unsigned)g, (unsigned)i);
			CControlUI* button = m_pm.FindControl(name);
			if( button == NULL )
				continue;
			const bool running = IsSkynetServerRunning(folder.servers[i], processes);
			if( running ) {
				button->SetAttribute(_T("color"), _T("#0B7A45FF"));
				button->SetAttribute(_T("color-hover"), _T("#FFFFFFFF"));
				button->SetAttribute(_T("icon-tint"), _T("#0B7A45FF"));
				button->SetAttribute(_T("icon-tint-hover"), _T("#FFFFFFFF"));
				button->SetAttribute(_T("background-color"), _T("#E7F7EEFF"));
				button->SetAttribute(_T("background-color-hover"), _T("#0B7A45FF"));
				button->SetAttribute(_T("border-width"), _T("1"));
				button->SetAttribute(_T("border-color"), _T("#0B7A45FF"));
				button->SetAttribute(_T("border-color-hover"), _T("#086338FF"));
			}
			else {
				button->SetAttribute(_T("color"), _T("var(--color-text)"));
				button->SetAttribute(_T("color-hover"), _T("var(--color-text)"));
				button->SetAttribute(_T("icon-tint"), _T("auto"));
				button->SetAttribute(_T("icon-tint-hover"), _T("auto"));
				button->SetAttribute(_T("background-color"), _T("#00000000"));
				button->SetAttribute(_T("background-color-hover"), _T("var(--color-bg-hover-medium)"));
				button->SetAttribute(_T("border-width"), _T("0"));
				button->SetAttribute(_T("border-color"), _T("#00000000"));
				button->SetAttribute(_T("border-color-hover"), _T("#00000000"));
			}
			std::wstring tip = FormatServerTip(folder.servers[i]);
			tip += running ? L"\n状态：已启动" : L"\n状态：未启动";
			button->SetToolTip(tip.c_str());
		}
	}
}

bool CMainWnd::FindServerByName(const CDuiString& name, const ServerConfig*& server, size_t* groupIndex) const
{
	server = NULL;
	unsigned group = 0;
	unsigned index = 0;
	if( _stscanf_s(name.GetData(), _T("srv_%u_%u"), &group, &index) != 2 )
		return false;
	if( group >= m_serverFolders.size() || index >= m_serverFolders[group].servers.size() )
		return false;
	server = &m_serverFolders[group].servers[index];
	if( groupIndex != NULL )
		*groupIndex = group;
	return true;
}

bool CMainWnd::FindServerBySender(CControlUI* sender, const ServerConfig*& server) const
{
	server = NULL;
	if( sender == NULL )
		return false;
	return FindServerByName(sender->GetName(), server);
}

void CMainWnd::OnMenu(TNotifyUI& msg)
{
	if( msg.pSender == NULL )
		return;
	unsigned group = 0;
	if( _stscanf_s(msg.pSender->GetName().GetData(), _T("grp_%u"), &group) == 1 ) {
		if( group >= m_serverFolders.size() )
			return;
		m_pm.HideControlToolTip();
		m_menuServerName = msg.pSender->GetName();
		if( m_pServerMenu != NULL ) {
			delete m_pServerMenu;
			m_pServerMenu = NULL;
		}
		CDuiPoint point(msg.ptScreen.x, msg.ptScreen.y);
		m_pServerMenu = new CMenuWnd();
		m_pServerMenu->Init(NULL, _T("category_menu.html"), point, &m_pm);
		return;
	}
	const ServerConfig* server = NULL;
	if( !FindServerBySender(msg.pSender, server) )
		return;
	ShowServerMenu(msg.pSender, msg.ptScreen);
}

void CMainWnd::ShowServerMenu(CControlUI* serverButton, POINT ptScreen)
{
	if( serverButton == NULL )
		return;
	m_pm.HideControlToolTip();
	m_menuServerName = serverButton->GetName();
	if( m_pServerMenu != NULL ) {
		delete m_pServerMenu;
		m_pServerMenu = NULL;
	}
	CDuiPoint point(ptScreen.x, ptScreen.y);
	m_pServerMenu = new CMenuWnd();
	m_pServerMenu->Init(NULL, _T("server_menu.html"), point, &m_pm);
}

void CMainWnd::ShowTrayMenu(POINT ptScreen)
{
	if( m_pTrayMenu != NULL ) {
		delete m_pTrayMenu;
		m_pTrayMenu = NULL;
	}
	::SetForegroundWindow(m_hWnd);
	CDuiPoint point(ptScreen.x, ptScreen.y);
	m_pTrayMenu = new CMenuWnd();
	m_pTrayMenu->Init(NULL, _T("tray_menu.html"), point, &m_pm);
}

bool CMainWnd::OnServerMenuCommand(WPARAM wParam)
{
	MenuCmd* command = (MenuCmd*)wParam;
	if( command == NULL )
		return false;
	const CDuiString name = command->szName;
	if( name == _T("tray_exit") ) {
		m_pm.DeletePtr(command);
		ForceClose(0);
		return true;
	}
	const bool ours = name == _T("menu_start") || name == _T("menu_stop") || name == _T("menu_hot") || name == _T("menu_clear")
		|| name == _T("menu_export_actor") || name == _T("menu_export_server")
		|| name == _T("menu_remove_folder") || name == _T("menu_open_folder");
	if( !ours )
		return false;
	m_pm.DeletePtr(command);

	if( name == _T("menu_remove_folder") || name == _T("menu_open_folder") ) {
		unsigned group = 0;
		if( _stscanf_s(m_menuServerName.GetData(), _T("grp_%u"), &group) != 1 || group >= m_serverFolders.size() )
			return true;
		if( name == _T("menu_remove_folder") ) {
			RemoveServerFolder(group);
			return true;
		}
		const std::wstring path = m_serverFolders[group].path;
		const DWORD attr = ::GetFileAttributesW(path.c_str());
		if( attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0 ) {
			CDuiString text;
			text.Format(_T("无法打开文件夹：%s"), path.c_str());
			SetStatusMessage(text.GetData());
			return true;
		}
		const HINSTANCE opened = ::ShellExecuteW(m_hWnd, _T("explore"), path.c_str(), NULL, NULL, SW_SHOWNORMAL);
		if( (INT_PTR)opened <= 32 ) {
			CDuiString text;
			text.Format(_T("无法打开文件夹：%s"), path.c_str());
			SetStatusMessage(text.GetData());
		}
		return true;
	}

	const ServerConfig* server = NULL;
	size_t group = 0;
	if( !FindServerByName(m_menuServerName, server, &group) || server == NULL )
		return true;
	if( name == _T("menu_start") )
		OnStartServer(*server);
	else if( name == _T("menu_stop") )
		OnStopServer(*server);
	else if( name == _T("menu_hot") )
		OnHotUpdateServer(*server);
	else if( name == _T("menu_clear") )
		OnClearServer(*server);
	else
		OpenChosenFeature(name == _T("menu_export_actor"), group, *server);
	return true;
}

void CMainWnd::OpenChosenFeature(bool actor, size_t group, const ServerConfig& server)
{
	if( group >= m_serverFolders.size() )
		return;
	if( actor && m_actorImporting ) {
		SetStatusMessage(_T("导号：正在导入"));
		return;
	}
	if( !actor && m_srvImporting ) {
		SetStatusMessage(_T("导服：正在导入"));
		return;
	}
	ChosenServer& chosen = actor ? m_actorServer : m_srvServer;
	chosen.folder = m_serverFolders[group].path;
	chosen.config = server;
	chosen.has = true;
	UpdateChosenLabel(actor ? _T("export_server_id") : _T("export_srv_id"), chosen);

	const int page = actor ? 1 : 2;
	LPCTSTR navName = actor ? _T("nav_export") : _T("nav_export_srv");
	COptionUI* option = static_cast<COptionUI*>(m_pm.FindControl(navName));
	if( option != NULL && !option->IsSelected() )
		option->Selected(true);
	else
		SelectPage(page);

	CDuiString text;
	text.Format(_T("%s：已选择服务器 id：%d  分类：%s"), actor ? _T("导号") : _T("导服"), chosen.config.hostId, chosen.folder.c_str());
	SetStatusMessage(text.GetData());
	if( !SaveAppSettings(CurrentSettings()) ) {
		text.Format(_T("%s：服务器选择未能保存"), actor ? _T("导号") : _T("导服"));
		SetStatusMessage(text.GetData());
	}
}

void CMainWnd::RemoveServerFolder(size_t group)
{
	if( group >= m_serverFolders.size() )
		return;
	const std::wstring path = m_serverFolders[group].path;
	std::wstring text = L"从列表移除这个分类？\n" + path + L"\n文件夹不会删除。";
	const int answer = CMessageBox::Show(m_hWnd, _T("移除分类"), text.c_str(),
		CModalOptions()
			.Kind(CONTROLKIND_WARNING)
			.ShowCancel(true)
			.OkText(_T("移除"))
			.CancelText(_T("取消"))
			.Width(560)
			.Height(220));
	if( answer != MESSAGEBOX_OK ) {
		SetStatusMessage(_T("已取消移除分类"));
		return;
	}
	m_serverFolders.erase(m_serverFolders.begin() + group);
	for( size_t i = 0; i < m_collapsedFolders.size(); ) {
		if( _wcsicmp(m_collapsedFolders[i].c_str(), path.c_str()) == 0 )
			m_collapsedFolders.erase(m_collapsedFolders.begin() + i);
		else
			++i;
	}
	if( m_actorServer.has && _wcsicmp(m_actorServer.folder.c_str(), path.c_str()) == 0 ) {
		m_actorServer = ChosenServer();
		UpdateChosenLabel(_T("export_server_id"), m_actorServer);
	}
	if( m_srvServer.has && _wcsicmp(m_srvServer.folder.c_str(), path.c_str()) == 0 ) {
		m_srvServer = ChosenServer();
		UpdateChosenLabel(_T("export_srv_id"), m_srvServer);
	}
	RebuildServerGroups();
	CDuiString status;
	status.Format(_T("已从列表移除分类：%s"), path.c_str());
	SetStatusMessage(status.GetData());
	if( !SaveAppSettings(CurrentSettings()) )
		SetStatusMessage(_T("移除的分类未能从配置中保存"));
}

void CMainWnd::OnStartServer(const ServerConfig& server)
{
	std::wstring display;
	if( !DescribeStart(server, display) ) {
		SetStatusMessage(display.c_str());
		return;
	}
	SetStatusMessage(display.c_str());
	std::wstring message;
	StartSkynet(server, message);
	SetStatusMessage(message.c_str());
	RefreshRunningServers();
}

void CMainWnd::OnStopServer(const ServerConfig& server)
{
	if( server.mcsPort <= 0 ) {
		SetStatusMessage(_T("停服失败：没有 GM 端口"));
		return;
	}
	CDuiString text;
	text.Format(_T("停服 %d：正在根据启动参数查找 skynet.exe"), server.hostId);
	SetStatusMessage(text.GetData());
	std::wstring detail;
	if( !FindRunningSkynet(server, detail) ) {
		text.Format(_T("停服 %d：未执行。%s"), server.hostId, detail.c_str());
		SetStatusMessage(text.GetData());
		return;
	}
	text.Format(_T("停服 %d：%s"), server.hostId, detail.c_str());
	SetStatusMessage(text.GetData());
	text.Format(_T("停服 %d：准备请求 http://127.0.0.1:%d/shutdown"), server.hostId, server.mcsPort);
	SetStatusMessage(text.GetData());
	RequestGm(m_hWnd, server.hostId, server.mcsPort, false);
}

void CMainWnd::OnHotUpdateServer(const ServerConfig& server)
{
	if( server.mcsPort <= 0 ) {
		SetStatusMessage(_T("热更失败：没有 GM 端口"));
		return;
	}
	CDuiString text;
	text.Format(_T("热更 %d：正在根据启动参数查找 skynet.exe"), server.hostId);
	SetStatusMessage(text.GetData());
	std::wstring detail;
	if( !FindRunningSkynet(server, detail) ) {
		text.Format(_T("热更 %d：未执行。%s"), server.hostId, detail.c_str());
		SetStatusMessage(text.GetData());
		return;
	}
	text.Format(_T("热更 %d：%s"), server.hostId, detail.c_str());
	SetStatusMessage(text.GetData());
	text.Format(_T("热更 %d：准备请求 http://127.0.0.1:%d/manage/update_auto.lua ，然后 collectgarbage.lua"), server.hostId, server.mcsPort);
	SetStatusMessage(text.GetData());
	RequestGm(m_hWnd, server.hostId, server.mcsPort, true);
}

void CMainWnd::OnClearServer(const ServerConfig& server)
{
	if( server.dbHost.empty() || server.dbName.empty() ) {
		SetStatusMessage(_T("清档失败：没有数据库配置"));
		return;
	}
	std::vector<SkynetProcess> processes;
	EnumerateSkynetProcesses(processes);
	if( IsSkynetServerRunning(server, processes) ) {
		const std::wstring blocked = L"清档 " + std::to_wstring(server.hostId) + L"：未执行。服务器正在运行，请先停服";
		SetStatusMessage(blocked.c_str());
		CMessageBox::Show(m_hWnd, _T("清档"), _T("服务器正在运行，请先停服后再清档。"),
			CModalOptions()
				.Kind(CONTROLKIND_WARNING)
				.ShowCancel(false)
				.OkText(_T("确定"))
				.Width(420)
				.Height(180));
		return;
	}
	std::wstring text = L"确定清档服务器 " + std::to_wstring(server.hostId) + L" ？\n数据库：" + server.dbHost;
	if( server.dbPort > 0 )
		text += L":" + std::to_wstring(server.dbPort);
	text += L" / " + server.dbName;
	text += L"\n将删除该服的角色、货币、变量、账号、模块和命令数据。此操作不能撤销。";
	const int answer = CMessageBox::Show(m_hWnd, _T("清档"), text.c_str(),
		CModalOptions()
			.Kind(CONTROLKIND_DANGER)
			.OkKind(CONTROLKIND_DANGER)
			.ShowCancel(true)
			.OkText(_T("清档"))
			.CancelText(_T("取消"))
			.Width(520)
			.Height(260));
	if( answer != MESSAGEBOX_OK ) {
		const std::wstring cancelled = L"清档 " + std::to_wstring(server.hostId) + L"：已取消";
		SetStatusMessage(cancelled.c_str());
		return;
	}
	const std::wstring started = L"清档 " + std::to_wstring(server.hostId) + L"：开始";
	SetStatusMessage(started.c_str());
	ClearServerDatabase(m_hWnd, server);
}

#include "stdafx.h"
#include "SafeHandle.h"
#include "resource.h"
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Rpcrt4.lib")
#pragma comment(lib, "dnsapi.lib")
#pragma comment(lib, "Version.lib")
#pragma comment(lib, "ntdll.lib")

static const size_t GUID_BASE64_LEN = 22;

#define RS(IDS, n) LoadStringInto(IDS, (LPWSTR)_alloca((n)*sizeof(wchar_t)), n)

struct CPeer
{
	wstring Name;
	IP4_ADDRESS Addr4; //In BE order - as it comes from DNS
	IP6_ADDRESS Addr6;
	WORD Port; //In host order - as it comes from DNS
	GUID PeerGUID;

	CPeer(const wstring& n, const IP4_ADDRESS a4, const IP6_ADDRESS &a6, const WORD p, const GUID& g)
		:Name(n), Addr4(a4), Addr6(a6), Port(p), PeerGUID(g)
	{}
};

HINSTANCE g_hInst;
UUID g_MyGUID;

static HWND s_hMainDlg = 0;
typedef vector<unique_ptr<CPeer>> CPeers;
static CPeers s_Peers;
static SafeRegKey s_hkSettings;

// Utils stuff
extern void ToBase64(const unsigned char* p, size_t l, char* s);
extern void ToBase64(const unsigned char* p, size_t l, wchar_t* s);
extern bool FromBase64(const wchar_t* s, unsigned char* p, size_t &n);
extern void DumpResult(PDNS_QUERY_RESULT Res);
extern void LoadShortString(int IDS, wstring& ws);
extern void NormalizeEndOfLines(LPWSTR p);
extern LPCWSTR LoadStringInto(UINT IDS, LPWSTR p, int n);
extern void GetMyVersion(wstring& Ver);
extern void GUIDWindowsToNet(GUID& g, unsigned char ng[]);
extern void GUIDNetToWindows(unsigned char ng[], GUID& g);
extern bool IsReallyV4(const unsigned char* Addr);

// Network stuff
extern void RequestReceive();
extern void Send(const IP4_ADDRESS Addr4, const IP6_ADDRESS& Addr6, const WORD Port, LPCWSTR Buf);
extern int NetInit(USHORT& Port);
extern void NetCleanup();
extern void MakePeerName(bool IPv6, const unsigned char* Addr, USHORT Port, wstring& Name);
extern LPCSTR MakePeerName(bool IPv6, const unsigned char* Addr, USHORT Port, LPSTR s, size_t n);

static void EnsureMyGUID()
{
	wchar_t s[50];
	DWORD Type, sz = sizeof s;
	if(RegQueryValueEx(s_hkSettings, L"MyGUID", 0, &Type, (LPBYTE)s, &sz) != ERROR_SUCCESS ||
		Type != REG_SZ)
	{
		UuidCreate(&g_MyGUID);
		StringFromGUID2(g_MyGUID, s, _countof(s));
	    RegSetValueEx(s_hkSettings, L"MyGUID", 0, REG_SZ, (LPBYTE)s, DWORD((wcslen(s) + 1) * sizeof(wchar_t)));
	}
	else
		CLSIDFromString(s, &g_MyGUID);
}

static CPeers::iterator FindPeer(const UUID& PeerGUID)
{
	return find_if(s_Peers.begin(), s_Peers.end(), [PeerGUID](const unique_ptr<CPeer>& p){return InlineIsEqualGUID(p->PeerGUID, PeerGUID); });
}

static void WINAPI OnRegComplete(DWORD, PVOID, PDNS_SERVICE_INSTANCE) { }

struct CReceivedMessage
{
	const UUID& PeerGUID;
	LPCWSTR Text;
};

static INT_PTR CALLBACK MessageDlgProc(HWND hDlg, unsigned Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
	case WM_INITDIALOG:
		{
		    const CReceivedMessage *rm = (const CReceivedMessage*)lParam;
			LPCWSTR pText = rm->Text;
			SetDlgItemText(hDlg, IDC_TEXT, pText);
			if (!wcsncmp(pText, L"https://", 8) || !wcsncmp(pText, L"http://", 7))
				ShowWindow(GetDlgItem(hDlg, IDC_NAVIGATE), SW_SHOW);
			CPeers::iterator it = FindPeer(rm->PeerGUID);
			wstring Title, ws;
			if(it != s_Peers.end())
				Title = ((*it)->Name);
			else
				LoadShortString(IDS_SOMEBODY, Title);
			LoadShortString(IDS_PEERSAYS, ws);
			Title += ws;
			SetWindowText(hDlg, Title.c_str());
		}
		break;
	case WM_COMMAND:
		if(LOWORD(wParam) == IDCANCEL)
			EndDialog(hDlg, IDCANCEL);
		else if (LOWORD(wParam) == IDC_NAVIGATE)
		{
			size_t n = (size_t)SendDlgItemMessage(hDlg, IDC_TEXT, WM_GETTEXTLENGTH, 0, 0);
			LPWSTR s = (LPWSTR)_alloca((n + 1) * sizeof(wchar_t));
			*s = wchar_t(n + 1);
			n = (size_t)SendDlgItemMessage(hDlg, IDC_TEXT, EM_GETLINE, 0, (LPARAM)s);
			s[n] = 0;
			ShellExecute(hDlg, 0, s, 0, 0, SW_SHOWNORMAL);
		}
		break;
	case WM_CLOSE:
		EndDialog(hDlg, IDCANCEL);
		break;
	}
	return 0;
}

static void StoreNameForGUID(const UUID& PeerGUID, const wstring& Name)
{
	SafeRegKey hk;
	if(RegCreateKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\Seva\\Toss\\Peers", 0, 0, 0, KEY_ALL_ACCESS, 0, &hk, 0) == ERROR_SUCCESS)
	{
		wchar_t ws[50];
		StringFromGUID2(PeerGUID, ws, _countof(ws));
		RegSetValueEx(hk, ws, 0, REG_SZ, (LPBYTE)Name.c_str(), DWORD(sizeof(wchar_t) * (Name.size() + 1)));
	}
}

static void OnBrowseComplete(PVOID, PDNS_QUERY_RESULT Res)
{
	//DumpResult(Res);

	DNS_RECORD *pr;
	UUID PeerGUID;
	bool GotPeerGUID = false;
	WORD Port = 0;
	IP4_ADDRESS Addr4 = 0;
	IP6_ADDRESS Addr6 = {0, 0};
	LPCWSTR HostName = 0, DisplayName = 0;
	for(pr = Res->pQueryRecords; pr; pr = pr->pNext)
	{
		switch(pr->wType)
		{
		case DNS_TYPE_SRV:
			if(!Port)
				Port = pr->Data.SRV.wPort;
			if(!HostName)
				HostName = pr->pName;
			break;
		case DNS_TYPE_A:
			if(!Addr4)
				Addr4 = pr->Data.A.IpAddress;
			break;
		case DNS_TYPE_AAAA:
			if(IsValidAddress(Addr6))
				Addr6 = pr->Data.AAAA.Ip6Address;
			break;
		case DNS_TYPE_TEXT:
			for(DWORD i = 0; i < pr->Data.TXT.dwStringCount; ++i)
			{
				LPCWSTR kv = pr->Data.TXT.pStringArray[i];
				if(!GotPeerGUID && !wcsncmp(kv, L"GUID=", 5))
				{
					size_t sz = sizeof PeerGUID;
					unsigned char NetGUID[16];
					if((GotPeerGUID = FromBase64(kv + 5, NetGUID, sz) && sz == sizeof NetGUID))
					    GUIDNetToWindows(NetGUID, PeerGUID);
				}
				else if(!wcsncmp(kv, L"Name=", 5) && !DisplayName)
				{
					DisplayName = kv + 5;
				}
			}
			break;
		}
	}

	if((DisplayName || HostName) && Port &&
		(Addr4 || IsValidAddress(Addr6)) &&
		GotPeerGUID && !InlineIsEqualGUID(PeerGUID, g_MyGUID))
	{
		char ip4[30], ip6[60];
		_RPT4(0, "~~~~ Found %S as %S at %s | %s\n", DisplayName, HostName,
			MakePeerName(false, (LPBYTE)&Addr4, Port, ip4, _countof(ip4)),
			MakePeerName(true, Addr6.IP6Byte, Port, ip6, _countof(ip6)));

		wstring Name;
		if(DisplayName)
			Name = DisplayName;
		else
		{
			LPCWSTR p = wcschr(HostName, L'.');
			if(p)
				Name = wstring(HostName, p - HostName);
			else
				Name = HostName;
		}

		StoreNameForGUID(PeerGUID, Name);

		CPeers::iterator it = FindPeer(PeerGUID);
		if(it != s_Peers.end())
		{
			//_RPT1(0, "Replaced %S with %d\n", Name.c_str(), Port);
			(*it)->Addr4 = Addr4;
			(*it)->Addr6 = Addr6;
			(*it)->Port = Port;

			if((*it)->Name != Name)
			{
				size_t i = it - s_Peers.begin();
				size_t Sel = (size_t)SendDlgItemMessage(s_hMainDlg, IDC_THELIST, LB_GETCURSEL, 0, 0);
				SendDlgItemMessage(s_hMainDlg, IDC_THELIST, LB_DELETESTRING, (WPARAM)i, 0);
				SendDlgItemMessage(s_hMainDlg, IDC_THELIST, LB_INSERTSTRING, (WPARAM)i, (LPARAM)Name.c_str());
				if(i == Sel)
					SendDlgItemMessage(s_hMainDlg, IDC_THELIST, LB_SETCURSEL, (WPARAM)i, 0);
			}
		}
		else
		{
			//_RPT1(0, "Added %S\n", Name.c_str());
			unique_ptr<CPeer> p(new CPeer(Name, Addr4, Addr6, Port, PeerGUID));
			s_Peers.push_back(move(p));
			SendDlgItemMessage(s_hMainDlg, IDC_THELIST, LB_ADDSTRING, 0, (LPARAM)Name.c_str());
		}
	}
	else
		;// _RPT5(0, "Not recorded %p %p %d %d %d\n", DisplayName, HostName, (int)Port, (int)Addr, (int)GotPeerGUID);
	
	DnsRecordListFree(Res->pQueryRecords, 0);
}

static bool GetNameForGUID(const UUID& PeerGUID, wstring& ws)
{
	SafeRegKey hk;
	if(RegOpenKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\Seva\\Toss\\Peers", 0, KEY_READ, &hk) == ERROR_SUCCESS)
	{
		wchar_t wsGUID[50];
		StringFromGUID2(PeerGUID, wsGUID, _countof(wsGUID));
		DWORD t, sz = 0;
		if(RegQueryValueEx(hk, wsGUID, 0, 0, 0, &sz) == ERROR_SUCCESS)
		{
			vector<BYTE> b;
			b.resize(sz);
			if(RegQueryValueEx(hk, wsGUID, 0, &t, b.data(), &sz) == ERROR_SUCCESS && t == REG_SZ)
			{
				ws = (LPCWSTR)b.data();
				return true;
			}
		}
	}
	return false;
}

void RecordPeer(const UUID& IncomingGUID, bool IPv6, const unsigned char *Addr, USHORT Port)
{
	CPeers::iterator it = FindPeer(IncomingGUID);
	if(it != s_Peers.end())
	{
		if(IPv6)
		{
			if(IsReallyV4(Addr))
				(*it)->Addr4 = *(ULONG*)(Addr+12);
			else
			    (*it)->Addr6 = *(IP6_ADDRESS*)Addr;
		}
		else
		    (*it)->Addr4 = *(ULONG*)Addr;
		(*it)->Port = Port;
	}
	else
	{
		wstring PeerName;
		if(!GetNameForGUID(IncomingGUID, PeerName))
			MakePeerName(IPv6, Addr, Port, PeerName);
		IP4_ADDRESS Addr4;
		IP6_ADDRESS Addr6;
		if(IPv6)
		{
			if(IsReallyV4(Addr))
			{
				Addr4 = *(IP4_ADDRESS*)(Addr + 12);
				ZeroMemory(Addr6.IP6Byte, sizeof Addr6);
			}
			else
			{
				Addr4 = 0;
				Addr6 = *(IP6_ADDRESS*)Addr;
			}
		}
		else
		{
			Addr4 = *(IP4_ADDRESS*)Addr;
			ZeroMemory(Addr6.IP6Byte, sizeof Addr6);
		}
		unique_ptr<CPeer> p(new CPeer(PeerName, Addr4, Addr6, Port, IncomingGUID));
		s_Peers.push_back(move(p));
		SendDlgItemMessage(s_hMainDlg, IDC_THELIST, LB_ADDSTRING, 0, (LPARAM)PeerName.c_str());
	}
}

void OnReceivedMessage(const UUID& IncomingGUID, LPCWSTR ws)
{
	CReceivedMessage rm = {IncomingGUID, ws};
	DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_MESSAGE), s_hMainDlg, MessageDlgProc, (LPARAM)&rm);
}

static void OnSend()
{
	int Sel = (int)SendDlgItemMessage(s_hMainDlg, IDC_THELIST, LB_GETCURSEL, 0, 0);
	if(Sel < 0 || Sel >= (int)s_Peers.size())
		return;

	const CPeer& Peer = *s_Peers[Sel];

	if(SendDlgItemMessage(s_hMainDlg, IDC_THETEXT, WM_GETTEXTLENGTH, 0, 0) > MAX_MESSAGE_SIZE)
		return;

	wchar_t Buf[MAX_MESSAGE_SIZE + 1];
	SendDlgItemMessage(s_hMainDlg, IDC_THETEXT, WM_GETTEXT, _countof(Buf), (LPARAM)Buf);
	NormalizeEndOfLines(Buf);

	//char ip[20];
	//_RPT2(0, "===== Send to %s:%hu\n", IPToString(Peer.Addr, ip), Peer.Port);
	Send(Peer.Addr4, Peer.Addr6, Peer.Port, Buf);
}

static INT_PTR CALLBACK MainDlgProc(HWND hDlg, unsigned Msg, WPARAM wParam, LPARAM lParam)
{
	static DNS_SERVICE_CANCEL bca = {0};

	switch(Msg)
	{
	case WM_INITDIALOG:
		s_hMainDlg = hDlg;
		SendDlgItemMessage(hDlg, IDC_THETEXT, EM_SETLIMITTEXT, 492, 0);
		RequestReceive();
		{
			wchar_t ws[MAX_COMPUTERNAME_LENGTH + 52];
			LoadString(g_hInst, IDS_ADVAS, ws, _countof(ws));
			DWORD dw = _countof(ws) - (DWORD)wcslen(ws);
			GetComputerName(ws + wcslen(ws), &dw);
			wcscat_s(ws, L".");
			SetDlgItemText(hDlg, IDC_ADVAS, ws);

			static DNS_SERVICE_BROWSE_REQUEST BrowReq = {DNS_QUERY_REQUEST_VERSION2, 0, L"_toss._udp.local", {(PDNS_SERVICE_BROWSE_CALLBACK)OnBrowseComplete}};
			DnsServiceBrowse(&BrowReq, &bca);
		}
		break;
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDC_SEND:
			OnSend();
		    break;
		case IDC_THELIST:
			if(HIWORD(wParam) == LBN_SELCHANGE || HIWORD(wParam) == LBN_SELCANCEL)
			{
				int Sel = (int)SendMessage((HWND)lParam, LB_GETCURSEL, 0, 0);
				if(Sel >= 0)
				{
					EnableWindow(GetDlgItem(hDlg, IDC_SEND), 1);
					EnableWindow(GetDlgItem(hDlg, IDC_THETEXT), 1);

					wstring ws;
					LoadShortString(IDS_SENDTO, ws);
					ws += s_Peers[Sel]->Name;
					ws += L':';
					SetDlgItemText(hDlg, IDC_SENDTO, ws.c_str());

					SetFocus(GetDlgItem(hDlg, IDC_THETEXT));
				}
			}
			break;
		case ID_EDIT_CLEAR:
			SendDlgItemMessage(hDlg, IDC_THETEXT, WM_SETTEXT, 0, 0);
			EnableWindow((HWND)lParam, 0);
			break;
		case ID_FILE_EXIT:
			DestroyWindow(hDlg);
			break;
		case ID_HELP_ABOUT:
		{
			wstring Ver;
			GetMyVersion(Ver);
			wstring ws((wostringstream() << RS(IDS_ABOUT1, 120) << Ver << RS(IDS_ABOUT2, 120)).str());
			MessageBox(hDlg, ws.c_str(), RS(IDS_ABOUTTITLE, 20), MB_ICONINFORMATION);
			break;
		}
			
		}
		break;
	case WM_INITMENUPOPUP:
		if(LOWORD(lParam) == 1) //Edit
		{
			LRESULT n = SendDlgItemMessage(hDlg, IDC_THETEXT, WM_GETTEXTLENGTH, 0, 0);
			EnableMenuItem((HMENU)wParam, ID_EDIT_CLEAR, n > 0 ? MF_ENABLED : MF_DISABLED);
		}
		break;
	case WM_CLOSE:
		DestroyWindow(hDlg);
		break;
	case WM_DESTROY:
		DnsServiceBrowseCancel(&bca);
		PostQuitMessage(0);
		break;
	}
	return 0;
}

static int RunWithWSA(LPWSTR CmdLine)
{
	if(RegCreateKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\Seva\\Toss", 0, 0, 0, KEY_ALL_ACCESS, 0, &s_hkSettings, 0) != ERROR_SUCCESS)
		return 1;

	EnsureMyGUID();
    
	int r;
	USHORT Port;
	if(r = NetInit(Port))
		return r;

	_RPT1(0, "~~~~~~~ Listening on %d\n", Port);

	//Advertise the service...
	wchar_t GUIDKey[] = L"GUID",
		GUIDValue[GUID_BASE64_LEN + 1],
		InstanceName[MAX_COMPUTERNAME_LENGTH + 20],
		HostName[MAX_COMPUTERNAME_LENGTH + 10];
	DWORD sz;
	unsigned char NetGUID[16];
	GUIDWindowsToNet(g_MyGUID, NetGUID),
		ToBase64(NetGUID, sizeof NetGUID, GUIDValue);
	PWSTR TxtKeys[] = {GUIDKey}, TxtValues[] = {GUIDValue};
	GetComputerName(InstanceName, &(sz = _countof(InstanceName)));
	wcscpy_s(HostName, InstanceName);
	wcscat_s(InstanceName, L"._toss._udp.local");
	wcscat_s(HostName, L".local");

	DNS_SERVICE_INSTANCE si = {InstanceName, HostName, 0, 0, Port, 0, 0, _countof(TxtKeys), TxtKeys, TxtValues, 0};
	DNS_SERVICE_REGISTER_REQUEST RegReq = {DNS_QUERY_REQUEST_VERSION1, 0, &si, OnRegComplete, 0, 0, false};
	DWORD dwr = DnsServiceRegister(&RegReq, 0);
	if(dwr != DNS_REQUEST_PENDING)
		return 1;

	// Continue with app init
	HACCEL hAcc = LoadAccelerators(g_hInst, MAKEINTRESOURCE(IDR_ACCEL));
	
	CreateDialog(g_hInst, MAKEINTRESOURCE(IDD_MAINDLG), 0, MainDlgProc);
	DWORD dwwr;
	MSG Msg;
	for(;;)
	{
		dwwr = MsgWaitForMultipleObjectsEx(0, 0, INFINITE, QS_ALLEVENTS, MWMO_ALERTABLE);
		if(dwwr == WAIT_OBJECT_0)
		{
			Msg.message = 0; //Just in case PeekMessage doesn't fill the struct when no messages 
			while(PeekMessage(&Msg, 0, 0, 0, PM_REMOVE))
			{
				if(Msg.message == WM_QUIT)
					break;
				if(!TranslateAccelerator(s_hMainDlg, hAcc, &Msg))
				{
					TranslateMessage(&Msg);
					DispatchMessage(&Msg);
				}
			}
			if(Msg.message == WM_QUIT)
				break;
		}
	}

	DnsServiceDeRegister(&RegReq, 0);
	NetCleanup();
	return 0;
}

int WINAPI wWinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ LPWSTR CmdLine, _In_ int)
{
	SafeWin32Handle hRunning(CreateMutex(0, 1, L"SevaTossIsRunning"));
	if(GetLastError() == ERROR_ALREADY_EXISTS)
	{
		MessageBoxW(0, RS(IDS_ALREADYRUNNING, 100), RS(IDS_APPTITLE, 10), MB_ICONEXCLAMATION | MB_OK);
		return 0;
	}

	g_hInst = hInst;
	WSADATA wd;
	if(WSAStartup(MAKEWORD(2, 2), &wd))
		return 1;
	int r = RunWithWSA(CmdLine);
	WSACleanup();
	return r;
}

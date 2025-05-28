#include "stdafx.h"

static const size_t
	DATAGRAM_SIZE = 512,
	V0_HEADER_SIZE = 20; 

union CDatagram
{
	char Data[DATAGRAM_SIZE];
	struct
	{
		unsigned Version;
		unsigned char nGUID[16]; //In network order - big endian
		char Text[MAX_MESSAGE_SIZE];
	};
};

extern UUID g_MyGUID;

static SOCKET s_Sock;
static bool s_IPv6 = true;

static CDatagram s_ReceiveGram;
static SOCKADDR_INET s_PeerAddr;
static int s_PeerAddrLen;
static DWORD s_ReceiveFlags;
static WSAOVERLAPPED s_ReceiveOver = {0, 0, 0, 0, 0};

// Util stuff
extern size_t CutToUTF8Size(LPCWSTR ws, size_t TargetSize);
extern void LFToCRLF(LPWSTR p);
extern void RecordPeer(const UUID& IncomingGUID, bool IPv6, const unsigned char *Addr, USHORT Port);
extern void OnReceivedMessage(const UUID& IncomingGUID, LPCWSTR ws);
extern void GUIDWindowsToNet(GUID& g, unsigned char ng[]);
extern void GUIDNetToWindows(unsigned char ng[], GUID& g);
extern bool IsReallyV4(const unsigned char* Addr);

void MakePeerName(bool IPv6, const unsigned char* Addr, USHORT Port, wstring& Name)
{
	wchar_t ip[60], * p;
	if(IPv6)
	{
		if(IsReallyV4(Addr))
			p = RtlIpv4AddressToString((IN_ADDR*)(Addr + 12), ip);
		else
		{
			*ip = '[';
			p = RtlIpv6AddressToString((IN6_ADDR*)Addr, ip + 1);
			*p++ = ']';
		}
	}
	else
		p = RtlIpv4AddressToString((IN_ADDR*)Addr, ip);
	*p++ = L':';
	_ultow_s(Port, p, _countof(ip) - (p - ip), 10);
	Name = ip;
}

LPCSTR MakePeerName(bool IPv6, const unsigned char* Addr, USHORT Port, LPSTR s, size_t n)
{
	char* p;
	if(IPv6)
	{
		if(IsReallyV4(Addr))
			p = RtlIpv4AddressToStringA((IN_ADDR*)(Addr + 12), s);
		else
		{
			*s = L'[';
			p = RtlIpv6AddressToStringA((IN6_ADDR*)Addr, s + 1);
			*p++ = L']';
		}
	}
	else
		p = RtlIpv4AddressToStringA((IN_ADDR*)Addr, s);
	*p++ = ':';
	_ultoa_s(Port, p, n - (p - s), 10);
	return s;
}

static void WSAAPI OnReceived(DWORD dwError, DWORD nReceived, LPWSAOVERLAPPED lpOverlapped, DWORD dwFlags);

void RequestReceive()
{
	WSABUF wb = {sizeof s_ReceiveGram, s_ReceiveGram.Data};
	s_ReceiveFlags = 0;
	s_PeerAddrLen = s_IPv6 ? sizeof s_PeerAddr.Ipv6 : sizeof s_PeerAddr.Ipv4;
	int r = WSARecvFrom(s_Sock, &wb, 1, 0, &s_ReceiveFlags, (SOCKADDR*)&s_PeerAddr, &s_PeerAddrLen, &s_ReceiveOver, OnReceived);
	if(r == SOCKET_ERROR)
		r = WSAGetLastError();//WSA_IO_PENDING = 997
	return;
}

static void WSAAPI OnReceived(DWORD dwError, DWORD nReceived, LPWSAOVERLAPPED lpOverlapped, DWORD dwFlags)
{
	bool IPv6 = s_PeerAddr.si_family == AF_INET6;

	if(dwError == 0)
	{
	    char ip[60];
		_RPT2(0, "---- Received %d from %s\n", nReceived,
			MakePeerName(IPv6, IPv6 ? s_PeerAddr.Ipv6.sin6_addr.u.Byte : &s_PeerAddr.Ipv4.sin_addr.S_un.S_un_b.s_b1,
				ntohs(IPv6 ? s_PeerAddr.Ipv6.sin6_port : s_PeerAddr.Ipv4.sin_port), ip, sizeof ip));
    }

	if(dwError == 0 && nReceived >= V0_HEADER_SIZE && s_ReceiveGram.Version == 0)
	{
		GUID PeerGUID;
		GUIDNetToWindows(s_ReceiveGram.nGUID, PeerGUID);
		RecordPeer(PeerGUID, IPv6,
			IPv6 ? s_PeerAddr.Ipv6.sin6_addr.u.Byte : &s_PeerAddr.Ipv4.sin_addr.S_un.S_un_b.s_b1,
			ntohs(IPv6 ? s_PeerAddr.Ipv6.sin6_port : s_PeerAddr.Ipv4.sin_port));

		//LF->CRLF can inflate message size beyond the nominal minimum.
		int TextLen = nReceived - V0_HEADER_SIZE;
		if(TextLen)
		{
			int wLen = MultiByteToWideChar(CP_UTF8, 0, s_ReceiveGram.Text, TextLen, 0, 0);
			//Count LFs; can't count by strchr because not null terminated
			int nLFs = 0;
			for(int i = 0; i < TextLen; i++)
				if(s_ReceiveGram.Text[i] == '\n')
					++nLFs;

			vector<wchar_t> b;
			b.resize(wLen + 1 + nLFs);
			int n = MultiByteToWideChar(CP_UTF8, 0, s_ReceiveGram.Text, TextLen, b.data(), (int)b.size());
			b[n] = 0;
			if(nLFs)
				LFToCRLF(b.data());
			OnReceivedMessage(PeerGUID, b.data());
		}
		else
			OnReceivedMessage(PeerGUID, L"");
	}

	if(s_Sock != INVALID_SOCKET)
	    RequestReceive();
}

void Send(const IP4_ADDRESS Addr4, const IP6_ADDRESS &Addr6, const WORD Port, LPCWSTR Buf)
{
	CDatagram dg;
	dg.Version = 0;
	GUIDWindowsToNet(g_MyGUID, dg.nGUID);
	int n = WideCharToMultiByte(CP_UTF8, 0, Buf, (int)CutToUTF8Size(Buf, sizeof(dg.Text)), dg.Text, sizeof dg.Text, 0, 0);

	WSABUF b = {(ULONG)V0_HEADER_SIZE + (ULONG)n, dg.Data};
	DWORD w;
	SOCKADDR_INET To;
	if(s_IPv6)
	{
		To.Ipv6.sin6_family = AF_INET6;
		To.Ipv6.sin6_port = htons(Port);
		To.Ipv6.sin6_flowinfo = 0;
		To.Ipv6.sin6_scope_id = 0;
		if(IsValidAddress(Addr6))
		    To.Ipv6.sin6_addr = *(IN6_ADDR*)&Addr6;
		else
		{
			ZeroMemory(To.Ipv6.sin6_addr.u.Word, 5 * sizeof(WORD));
			To.Ipv6.sin6_addr.u.Word[5] = 0xFFFF;
			*(IP4_ADDRESS*)(To.Ipv6.sin6_addr.u.Byte + 12) = Addr4;
		}
	}
	else if(Addr4)
	{
		To.Ipv4.sin_family = AF_INET;
		To.Ipv4.sin_port = htons(Port);
		To.Ipv4.sin_addr.S_un.S_addr = Addr4;
	}
	else
		return; // The recipient only supports IPv6 but we don't

	//_RPT5(0, "Send to %d.%d.%d.%d:%d\n", To.sin_addr.S_un.S_un_b.s_b1, To.sin_addr.S_un.S_un_b.s_b2, To.sin_addr.S_un.S_un_b.s_b3, To.sin_addr.S_un.S_un_b.s_b4, ntohs(To.sin_port));
	int r = WSASendTo(s_Sock, &b, 1, &w, 0, (SOCKADDR*)&To, s_IPv6 ? sizeof To.Ipv6 : sizeof To.Ipv4, 0, 0);
	if(r == SOCKET_ERROR)
		r = WSAGetLastError();
	return;
}

int NetInit(USHORT& Port)
{
	SOCKADDR_INET sa;
	ZeroMemory(&sa, sizeof sa);
	//TODO: detect systems with no support for IPV6_V6ONLY and fall back to v4
	s_Sock = WSASocket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP, 0, 0, WSA_FLAG_OVERLAPPED);
	if(s_Sock != INVALID_SOCKET)
	{
		char Opt;
		int sz = 1;
		if(getsockopt(s_Sock, IPPROTO_IPV6, IPV6_V6ONLY, &Opt, &sz))
		    return 1;
		if(Opt)
		{
			Opt = 0;
			if(setsockopt(s_Sock, IPPROTO_IPV6, IPV6_V6ONLY, &Opt, sizeof Opt))
				return 1;
		}
		sa.Ipv6.sin6_family = AF_INET6;
	}
	else if(WSAGetLastError() == WSAEAFNOSUPPORT) // No IPv6, fall down
	{
		s_IPv6 = false;
		s_Sock = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, 0, 0, WSA_FLAG_OVERLAPPED);
		if(s_Sock == INVALID_SOCKET)
			return 1;
		sa.Ipv4.sin_family = AF_INET;
	}
	else
		return 1;

	if(bind(s_Sock, (SOCKADDR*)&sa, sizeof sa))
	{
		closesocket(s_Sock);
		return 1;
	}

	int nl;
	getsockname(s_Sock, (SOCKADDR*)&sa, &(nl = sizeof sa));
	Port = ntohs(sa.si_family == AF_INET6 ? sa.Ipv6.sin6_port : sa.Ipv4.sin_port);
	return 0;
}

void NetCleanup()
{
	closesocket(s_Sock);
	s_Sock = INVALID_SOCKET;
	SleepEx(1, 1); //Let the completion run
}

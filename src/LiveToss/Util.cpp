#include "stdafx.h"

extern HINSTANCE g_hInst;

//Filename-safe Base64 (-_ for the last two digits) with no padding
template<typename T>
static void ToBase64(const unsigned char* p, size_t l, T* s, const T ALPHABET[])
{
    size_t i;
    size_t mod = l % 3;
    size_t ll = l - mod;
    unsigned int triad;
    for(i = 0; i < ll; i += 3)
    {
        triad = (p[i] << 16) | (p[i + 1] << 8) | p[i + 2];
        *s++ = ALPHABET[(triad >> 18) & 0x3f];
        *s++ = ALPHABET[(triad >> 12) & 0x3f];
        *s++ = ALPHABET[(triad >> 6) & 0x3f];
        *s++ = ALPHABET[triad & 0x3f];
    }
    if(mod == 1)
    {
        *s++ = ALPHABET[(p[i] >> 2) & 0x3f];
        *s++ = ALPHABET[(p[i] << 4) & 0x3f];
    }
    if(mod == 2)
    {
        triad = (p[i] << 8) | p[i + 1];
        *s++ = ALPHABET[(triad >> 10) & 0x3f];
        *s++ = ALPHABET[(triad >> 4) & 0x3f];
        *s++ = ALPHABET[(triad << 2) & 0x3f];
    }
    *s = 0;
}

void ToBase64(const unsigned char* p, size_t l, char* s)
{
    ToBase64<char>(p, l, s, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
}

void ToBase64(const unsigned char* p, size_t l, wchar_t* s)
{
    ToBase64<wchar_t>(p, l, s, L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
}

unsigned char FromBase64Char(wchar_t c, bool *Bad)
{
    if(c >= L'A' && c <= L'Z')
        return c - L'A';
    else if(c >= L'a' && c <= L'z')
        return c - L'a' + 26;
    else if(c >= L'0' && c <= L'9')
        return c - L'0' + 52;
    else if(c == L'-')
        return 62;
    else if(c == L'_')
        return 63;
    else
    {
        *Bad = true;
        return 0;
    }
}

bool FromBase64(const wchar_t* s, unsigned char* p, size_t &n)
{
    unsigned char u0, u1, u2, u3;
    bool Bad = false;
    size_t w = 0;
    while(*s && s[1] && s[2] && s[3])
    {
        u0 = FromBase64Char(*s++, &Bad);
        u1 = FromBase64Char(*s++, &Bad);
        u2 = FromBase64Char(*s++, &Bad);
        u3 = FromBase64Char(*s++, &Bad);
        if(Bad || n - w < 3)
            return false;
        *p++ = (u0 << 2) | (u1 >> 4);
        *p++ = (u1 << 4) | (u2 >> 2);
        *p++ = (u2 << 6) | u3;
        w += 3;
    }
    if(*s)
    {
        if(!s[1])
            return false;

        u0 = FromBase64Char(*s++, &Bad);
        u1 = FromBase64Char(*s++, &Bad);

        if(*s)
        {
            u2 = FromBase64Char(*s, &Bad);
            if(Bad && n - w < 2)
                return false;
            *p++ = (u0 << 2) | (u1 >> 4);
            *p++ = (u1 << 4) | (u2 >> 2);
            w += 2;
        }
        else
        {
            if(Bad && n - w < 1)
                return false;
            *p++ = (u0 << 2) | (u1 >> 4);
            ++w;
        }
    }
    n = w;
    return true;
}

void DumpResult(PDNS_QUERY_RESULT Res)
{
    DNS_RECORD* pr;
    _RPT0(0, "---- Start DNS resultset\n");
    for(pr = Res->pQueryRecords; pr; pr = pr->pNext)
    {
        if(pr->wType == DNS_TYPE_PTR)
            _RPT2(0, "        %S PTR %S\n", pr->pName, pr->Data.PTR.pNameHost);
        else if(pr->wType == DNS_TYPE_SRV)
            _RPT5(0, "        %S SRV %S port %d pri %d w %d\n", pr->pName, pr->Data.SRV.pNameTarget, pr->Data.SRV.wPort, pr->Data.SRV.wPriority, pr->Data.SRV.wWeight);
        else if(pr->wType == DNS_TYPE_TEXT)
        {
            _RPT2(0, "        %S TEXT %d\n", pr->pName, pr->Data.TXT.dwStringCount);
            for(DWORD i = 0; i < pr->Data.TXT.dwStringCount; i++)
                _RPT1(0, "            %S\n", pr->Data.TXT.pStringArray[i]);
        }
        else if(pr->wType == DNS_TYPE_A)
        {
            union
            {
                IP4_ADDRESS a;
                unsigned char ab[4];
            };
            a = pr->Data.A.IpAddress;
            _RPT5(0, "        %S A %d.%d.%d.%d\n", pr->pName, ab[0], ab[1], ab[2], ab[3]);
        }
        else if(pr->wType == DNS_TYPE_AAAA)
        {
            unsigned char* b = pr->Data.AAAA.Ip6Address.IP6Byte;
            char aa[33];
            static const char HEXALPHA[] = "0123456789ABCDEF";
            for(int i = 0; i < 16; i++)
            {
                aa[i * 2] = HEXALPHA[b[i] >> 4];
                aa[i * 2 + 1] = HEXALPHA[b[i] & 0xF];
            }
            aa[32] = 0;
            _RPT2(0, "        %S AAAA %s\n", pr->pName, aa);
        }
        else
            _RPT2(0, "        %S %d\n", pr->pName, (int)pr->wType, pr->Data.AAAA.Ip6Address);
    }
    _RPT0(0, "---- End\n");
}

void LoadShortString(int IDS, wstring& ws)
{
    wchar_t Buf[101];
    LoadString(g_hInst, IDS, Buf, _countof(Buf));
    ws = Buf;
}

LPCWSTR LoadStringInto(UINT IDS, LPWSTR p, int n)
{
    LoadString(g_hInst, IDS, p, n);
    return p;
}


//Returns the largest length of ws that will fit into TargetSize bytes as UTF-8,
//no null terminator assumed. The source string is expected to be null terminated.
//ws expected to be well formed UTF-16 - no broken surrogate pairs.
//TargetSize is expected to be reasonable - at least 4.
size_t CutToUTF8Size(LPCWSTR ws, size_t TargetSize)
{
    size_t wn = wcslen(ws);

    for(;;)
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, ws, (int)wn, 0, 0, 0, 0);
        if((size_t)n <= TargetSize)
            return wn;
        --wn;
        if((ws[wn] & 0xFC00) == 0xDC00) //Low word of a UTF-16 surrogate pair
            --wn;
    }
}

//CRLFs and standlone CRs to LFs in place.
//Assumes a null terminated string.
void NormalizeEndOfLines(LPWSTR p)
{
    while((p = wcschr(p, L'\r')))
    {
        if(p[1] == L'\n') //CR followed by LF
        {
            wmemmove(p, p + 1, wcslen(p + 1) + 1);
            ++p;
        }
        else //Standalone CR
            *p++ = L'\n';
    }
}

void LFToCRLF(LPWSTR p)
{
    while(p = wcschr(p, L'\n'))
    {
        wmemmove(p + 1, p, wcslen(p) + 1);
        *p = '\r';
        p += 2;
    }
}

void GUIDNetToWindows(unsigned char ng[], GUID& g)
{
    g.Data1 = _byteswap_ulong(*(unsigned long*)ng);
    g.Data2 = _byteswap_ushort(*(unsigned short*)(ng + 4));
    g.Data3 = _byteswap_ushort(*(unsigned short*)(ng + 6));
    memcpy(g.Data4, ng + 8, 8);
}

void GUIDWindowsToNet(GUID& g, unsigned char ng[])
{
    *(unsigned long*)ng = _byteswap_ulong(g.Data1);
    *(unsigned short*)(ng + 4) = _byteswap_ushort(g.Data2);
    *(unsigned short*)(ng + 6) = _byteswap_ushort(g.Data3);
    memcpy(ng + 8, g.Data4, 8);
}

void GetMyVersion(wstring& Ver)
{
    wchar_t fn[MAX_PATH + 1];
    GetModuleFileName(g_hInst, fn, _countof(fn));
    DWORD h, sz = GetFileVersionInfoSizeEx(FILE_VER_GET_NEUTRAL, fn, &h);
    vector<char> b;
    b.resize(sz);
    GetFileVersionInfoEx(FILE_VER_GET_NEUTRAL, fn, 0, sz, b.data());
    LPCWSTR pVer;
    UINT VerLen;
    //English-US with codepage 1200 (UTF-16LE)
    VerQueryValue(b.data(), L"\\StringFileInfo\\040904b0\\ProductVersion", (LPVOID*)&pVer, &VerLen);
    Ver = pVer;
}

bool IsReallyV4(const unsigned char* Addr)
{
    return !Addr[0] && !Addr[1] && !Addr[2] && !Addr[3] &&
        !Addr[4] && !Addr[5] && !Addr[6] && !Addr[7] &&
        !Addr[8] && !Addr[9] && Addr[10] == 0xFF && Addr[11] == 0xFF;
}

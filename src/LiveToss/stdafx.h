#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2ipdef.h>
#include <Mstcpip.h>
#include <windns.h>
#include <crtdbg.h>
#include <string>
#include <vector>

using namespace std;

static const size_t MAX_MESSAGE_SIZE = 492; //Max message size in UTF-8 bytes; but we'll reuse it to limit UI message size in wide chars. Better than nothing.

#ifdef _WIN64
static bool IsValidAddress(const IP6_ADDRESS& Addr)
{
	return Addr.IP6Qword[0] || Addr.IP6Qword[1];
}
#else
static bool IsValidAddress(const IP6_ADDRESS& Addr)
{
	return Addr.IP6Dword[0] || Addr.IP6Dword[1] || Addr.IP6Dword[2] || Addr.IP6Dword[3];
}
#endif

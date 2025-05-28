#include <iostream>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/timeval.h>
#include "toss.h"

using namespace std;

static const size_t GUID_BASE64_LEN = 22;

static AvahiClient *s_cli = 0;
static AvahiServiceBrowser *s_browser = 0;
static AvahiSimplePoll *s_poll = 0;
static AvahiWatch *s_stdin_watch = 0,
    *s_sock_watch = 0;
static AvahiTimeout *s_timeout = 0;
static AvahiEntryGroup *s_group = 0;

extern void process_input();
extern void receive_dgram();
extern void timeout_elapsed();
extern void found_peer(const uuid_t &peer_guid, const string &name, bool ipv6, const uint8_t *address, unsigned short port);

void av_break()
{
    avahi_simple_poll_quit(s_poll);
}

static void on_error(const char *msg)
{
    cout << msg << endl;
    if(s_cli)
        av_break();
    else
        exit(1);
}

static void group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, AVAHI_GCC_UNUSED void *)
{
    if(state == AVAHI_ENTRY_GROUP_ESTABLISHED)
        ;//cout << "Registered" << endl << "Press q to quit" << endl;
    else if (state == AVAHI_ENTRY_GROUP_FAILURE)
        on_error("Advertisement failure (group).");
}

//May comes before client_new returns
static void advertise(AvahiClient *cli)
{
    if(!(s_group = avahi_entry_group_new(cli, group_callback, 0)))
        return on_error("Advertisement failure (group).");

    char kv_guid[60] = "GUID=";
    to_base64(g_my_guid, sizeof g_my_guid, kv_guid + 5);

    char hostname[HOST_NAME_MAX + 1];
    gethostname(hostname, sizeof hostname);

    if(avahi_entry_group_add_service(s_group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, (AvahiPublishFlags)0, hostname, "_toss._udp.", 0, 0, g_port, kv_guid, 0) < 0)
        return on_error("Advertisement failure (service).");

    if(avahi_entry_group_commit(s_group) < 0)
        return on_error("Advertisement failure (commit).");

    if(g_mode == Mode::RECEIVE)
    {
        char hostname[HOST_NAME_MAX + 1];
        gethostname(hostname, sizeof hostname);
        cout << "Advertising as " << hostname << ", waiting for messages from other Toss instances." << endl <<
            "Press \"q\" to quit." << endl << endl;
    }
}

static void resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol proto,
    AvahiResolverEvent event, const char *name, const char *type, const char *domain,
    const char *host_name, const AvahiAddress *a, uint16_t port,
    AvahiStringList *txt, AvahiLookupResultFlags flags, void *)
{
    if(event == AVAHI_RESOLVER_FOUND)
    {
        uuid_t peer_guid;
        bool got_guid = false;
        string peer_name;
        for(;txt;txt = txt->next)
        {
            if(txt->size >= 5 + GUID_BASE64_LEN && !strncmp((const char*)txt->text, "GUID=", 5))
            {
                size_t sz = sizeof peer_guid;
                got_guid = from_base64((const char*)txt->text + 5, peer_guid, sz) && sz == sizeof peer_guid;
            }
            else if(txt->size >= 6 && !strncmp((const char*)txt->text, "Name=", 5))
                peer_name = string((const char*)txt->text + 5, txt->size - 5);
        }
        if(!peer_name.size())
            peer_name = name;
        
        if(proto != AVAHI_PROTO_UNSPEC && got_guid && uuid_compare(peer_guid, g_my_guid))
            found_peer(peer_guid, peer_name, a->proto == AVAHI_PROTO_INET6, a->data.data, port);
    }
    avahi_service_resolver_free(r);
}

static void browse_callback(AvahiServiceBrowser *b, AvahiIfIndex iface, AvahiProtocol proto,
    AvahiBrowserEvent event, const char *name, const char *type,
    const char *domain, AvahiLookupResultFlags flags, void *s)
{
    if(event == AVAHI_BROWSER_NEW)
    {
        //cout << "Discovered " << name << " " << type << " " << domain << endl;
        avahi_service_resolver_new(s_cli, iface, proto, name, type, domain, AVAHI_PROTO_UNSPEC, (AvahiLookupFlags)0, resolve_callback, 0);
    }
}

static void discover(AvahiClient *cli)
{
    if(!(s_browser = avahi_service_browser_new(cli, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_toss._udp", 0, (AvahiLookupFlags)0, browse_callback, 0)))
        return on_error("Error starting discovery.");

    if(g_mode == Mode::LIST)
        cout << "Looking for running Toss instances nearby, press \"q\" to quit..." << endl;
    else if(g_mode == Mode::SEND)
        cout << "Looking for the recipient for up to 10 seconds..." << endl;
}

// Called when we connect to Avahi - can't neither discover nor advertise until then
void connect_callback(AvahiClient *cli, AvahiClientState state, void *)
{
    if(state == AVAHI_CLIENT_S_RUNNING)
    {
        //cout << "Connected." << endl;
        if(g_mode == Mode::RECEIVE)
            advertise(cli);
        discover(cli);
    }
    else
        on_error("Error connecting to the Avahi daemon.");
}

bool av_start(int sock)
{
    s_poll = avahi_simple_poll_new();
    if(g_mode == Mode::RECEIVE || g_mode == Mode::LIST)
    // RECEIVE - adversite and watch for incoming datagrams and quit keypress
    // LIST - watch for the quit keypress
    {
        s_stdin_watch = ((AvahiPoll*)s_poll)->watch_new((AvahiPoll*)s_poll, STDIN_FILENO, AVAHI_WATCH_IN, 
            [](AvahiWatch *, int, AvahiWatchEvent, void *){ process_input(); }, 0);

        if(g_mode == Mode::RECEIVE)
            s_sock_watch = ((AvahiPoll*)s_poll)->watch_new((AvahiPoll*)s_poll, sock, AVAHI_WATCH_IN,
                [](AvahiWatch *, int, AvahiWatchEvent, void *){ receive_dgram(); }, 0);
    }
    else //SEND - start discovery with a timeout
    {
        struct timeval tv;
        avahi_elapse_time(&tv, 1000*10, 0); //TODO: configurable wait time
        s_timeout = ((AvahiPoll*)s_poll)->timeout_new((AvahiPoll*)s_poll, &tv,
            [](AvahiTimeout*, void*){timeout_elapsed();}, 0);
    }
    
    int e;
    s_cli = avahi_client_new((AvahiPoll*)s_poll, (AvahiClientFlags)0, connect_callback, 0, &e);
    return s_cli;
}

void av_end()
{
    if(s_cli)
    {
        if(s_browser)
            avahi_service_browser_free(s_browser);

        avahi_client_free(s_cli);

        if(s_stdin_watch)
            ((AvahiPoll*)s_poll)->watch_free(s_stdin_watch);
        if(s_sock_watch)
            ((AvahiPoll*)s_poll)->watch_free(s_sock_watch);
        if(s_timeout)
            ((AvahiPoll*)s_poll)->timeout_free(s_timeout);

        avahi_simple_poll_free(s_poll);
    }
}

void av_loop()
{
    avahi_simple_poll_loop(s_poll);
}

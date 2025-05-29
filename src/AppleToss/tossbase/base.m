#import "base.h"
#import <Network/Network.h>
#include <sys/errno.h>

@interface Peer()
{
@public
    NSString *name;
    nw_endpoint_t endpoint;
    NSUUID *guid;
}
@end

@implementation Peer
@end

@interface TossBase()
{
    __weak NSObject<TossDelegate> *_delegate;
    nw_browser_t _browser;
    nw_listener_t _listener;
    nw_endpoint_t _listen_endpoint;
    NSMutableArray<Peer*> *_peers;
    NSUUID *_myGUID;
}
@end

static NSError *toNS(nw_error_t e)
{
    return e ? (__bridge NSError*)nw_error_copy_cf_error(e) :
        [NSError errorWithDomain: NSCocoaErrorDomain code: 0 userInfo: nil];
}

@implementation TossBase
#pragma mark -
#pragma mark Init

-(id)initWithDelegate:(NSObject<TossDelegate> *)d
{
    if(self = [super init])
    {
        _delegate = d;
        _peers = [[NSMutableArray alloc] initWithCapacity:5];
    }
    return self;
}

-(void)ensureGUID
{
    NSUserDefaults *Def = NSUserDefaults.standardUserDefaults;
    NSString *s = [Def stringForKey:@"MyGUID"];
    if(s)
        _myGUID = [[NSUUID alloc] initWithUUIDString: s];
    else
    {
        _myGUID = [NSUUID UUID];
        [Def setObject: _myGUID.UUIDString forKey: @"MyGUID"];
    }
}

-(NSUInteger)findPeer:(NSUUID*)guid
{
    return [_peers indexOfObjectPassingTest: ^ BOOL (Peer *p, NSUInteger i, BOOL *s) {return [p->guid compare: guid] == NSOrderedSame ? YES : NO;}];
}

#pragma mark -
#pragma mark Server - advertising and receiving

-(void)setAdvertGUID:(nw_txt_record_t)txt
{
    unsigned char g[16];
    [_myGUID getUUIDBytes: g];
    NSMutableString *s = [NSMutableString stringWithString: [[NSData dataWithBytes: g length: 16] base64EncodedStringWithOptions: 0]];
    //We know for a fact there is a == padding
    NSUInteger n = s.length - 2;
    [s deleteCharactersInRange: NSMakeRange(n, 2)];
    NSRange r = NSMakeRange(0, n);
    [s replaceOccurrencesOfString: @"+" withString:@"-" options: NSLiteralSearch range: r];
    [s replaceOccurrencesOfString: @"/" withString:@"_" options: NSLiteralSearch range: r];
    unsigned char gs[GUID_BASE64_LEN];
    NSUInteger sz;
    [s getBytes: gs maxLength: sizeof gs usedLength: &sz encoding: NSASCIIStringEncoding options:0 range:r remainingRange: nil];
    nw_txt_record_set_key(txt, "GUID", gs, sz);
}

-(nw_advertise_descriptor_t)advertiseDescriptor
{
    // Prepare the Bonjour advertisement
    NSString *name, *host;
    name = [_delegate name: &host];
    
    nw_advertise_descriptor_t adv = nw_advertise_descriptor_create_bonjour_service(host.UTF8String, "_toss._udp.", "local.");
    nw_txt_record_t txt = nw_txt_record_create_dictionary();
    [self setAdvertGUID: txt];
    NSData *nd = [name dataUsingEncoding:NSUTF8StringEncoding];
    nw_txt_record_set_key(txt, "Name", nd.bytes, nd.length);
    nw_advertise_descriptor_set_txt_record_object(adv, txt);
    return adv;
}

-(void)advertise
{
    NSUserDefaults *Defs = NSUserDefaults.standardUserDefaults;
    id o = [Defs objectForKey: @"Port"];
    NSString *port = nil;
    if(o)
        port = [NSString stringWithFormat: @"%@", o];
    
    nw_parameters_t params = nw_parameters_create_secure_udp(NW_PARAMETERS_DISABLE_PROTOCOL, NW_PARAMETERS_DEFAULT_CONFIGURATION);
    if(port)
        _listener = nw_listener_create_with_port(port.UTF8String, params);
    else
        _listener = nw_listener_create(params);

    nw_listener_set_advertise_descriptor(_listener, self.advertiseDescriptor);
    nw_listener_set_queue(_listener, dispatch_get_main_queue());
    nw_listener_set_new_connection_handler(_listener, ^(nw_connection_t conn){ [self onIncomingClient: conn]; });
    nw_listener_set_state_changed_handler(_listener, ^(nw_listener_state_t state, _Nullable nw_error_t error){
        if(state == nw_listener_state_ready && !port)
            [Defs setInteger: nw_listener_get_port(self->_listener) forKey: @"Port"];
        else if(state == nw_listener_state_failed)
        {
            if(error &&
               nw_error_get_error_domain(error) == nw_error_domain_posix &&
               nw_error_get_error_code(error) == EADDRINUSE)
            {
                // Repeat, and get a random port
                [Defs removeObjectForKey: @"Port"];
                [self advertise];
            }
            else
                [self->_delegate onError: toNS(error) ctxt: @"Advertise error:\n"];
        }
    });
    nw_listener_set_advertised_endpoint_changed_handler(_listener, ^(nw_endpoint_t ep, bool added) {
        self->_listen_endpoint =  added ? ep : nil;
    });
    nw_listener_start(_listener);
}

-(void)onDatagram:(dispatch_data_t)d fromEndpoint:(nw_endpoint_t)fromEndpoint
{
    size_t sz  = dispatch_data_get_size(d);
    if(sz >= V0_HEADER_SIZE)
    {
        NSMutableData *dgramData = [NSMutableData dataWithCapacity: sz];
        //What if fragmentation?
        dispatch_data_apply(d, ^ bool (dispatch_data_t region, size_t offset, const void *data, size_t size){ [dgramData appendBytes: data length: size]; return true; });
        
        struct dgram_t
        {
            uint32_t version;
            unsigned char guid[16];
            unsigned char text[1];
        };
        
        const struct dgram_t *dgram = (const struct dgram_t*)dgramData.bytes;
        if(dgram->version == 0)
        {
            NSUUID *peerGUID = [[NSUUID alloc] initWithUUIDBytes:dgram->guid];
            //NSLog(@"Received %lu from %@", sz, peerGUID);
            
            NSString *text = nil;
            if(sz > V0_HEADER_SIZE)
                text = [[NSString alloc] initWithBytes: dgram->text length: sz - V0_HEADER_SIZE encoding: NSUTF8StringEncoding];
            
            //Resolve the from name
            NSUInteger peerno = [self findPeer:peerGUID];
            NSString *fromName;
            if(peerno != NSNotFound)
                fromName = _peers[peerno]->name;
            else
            {
                NSUserDefaults *Def = NSUserDefaults.standardUserDefaults;
                NSString *savedName = [Def stringForKey: [NSString stringWithFormat: @"NameForGUID%@", peerGUID.UUIDString]];
                if(savedName)
                    fromName = savedName;
                else
                    fromName = [NSString stringWithFormat: @"%@", fromEndpoint];
                
                Peer *p = [[Peer alloc] init];
                p->name = fromName;
                p->endpoint = fromEndpoint;
                p->guid = peerGUID;
                [_peers addObject:p];
                [_delegate refreshTable];
            }
            
            [_delegate onMessageFrom: fromName text:text];
        }
    }
}

-(void)onIncomingClient:(nw_connection_t) conn
{
    nw_connection_set_queue(conn, dispatch_get_main_queue());
    nw_connection_set_state_changed_handler(conn, ^(nw_connection_state_t state, nw_error_t e){
        if(state == nw_connection_state_ready)
        {
            nw_connection_receive_message(conn, ^(dispatch_data_t d, nw_content_context_t c, _Bool b, nw_error_t e){
                nw_path_t path = nw_connection_copy_current_path(conn);
                nw_endpoint_t ep = nw_path_copy_effective_remote_endpoint(path);

                //TODO: make callbacks work.
                [self onDatagram: d fromEndpoint: ep];
                nw_connection_cancel(conn);
            });
        }});
    nw_connection_start(conn);
}

#pragma mark -
#pragma mark Client - discovery and sending

-(void)discover
{
    nw_browse_descriptor_t desc = nw_browse_descriptor_create_bonjour_service("_toss._udp.", "local.");
    nw_browse_descriptor_set_include_txt_record(desc, true);
    _browser = nw_browser_create(desc, nw_parameters_create());
    nw_browser_set_browse_results_changed_handler(_browser, ^(nw_browse_result_t prev, nw_browse_result_t res, _Bool b) { [self onDiscovered: res]; });
    nw_browser_set_queue(_browser, dispatch_get_main_queue());
    nw_browser_start(_browser);
}

-(void)onDiscovered:(nw_browse_result_t) res;
{
    //TODO: service disappearance
    if(res)
    {
        NSUUID __block *peerGUID = nil;
        NSString __block *displayName = nil;
        NSString *serviceName = nil;

        nw_endpoint_t ep = nw_browse_result_copy_endpoint(res);
        if(ep && nw_endpoint_get_type(ep) == nw_endpoint_type_bonjour_service)
            serviceName = [NSString stringWithCString: nw_endpoint_get_bonjour_service_name(ep) encoding: NSUTF8StringEncoding];
        
        nw_txt_record_t txt = nw_browse_result_copy_txt_record_object(res);
        if(txt && nw_txt_record_is_dictionary(txt))
        {
            nw_txt_record_access_key(txt, "GUID", ^ _Bool (const char *n, nw_txt_record_find_key_t k, const unsigned char *value, unsigned long l)
            {
                if(k == nw_txt_record_find_key_non_empty_value && l >= GUID_BASE64_LEN)
                {
                    NSMutableString *s = [[NSMutableString alloc] initWithBytes: value length: l encoding: NSASCIIStringEncoding];
                    NSRange r = NSMakeRange(0, l);
                    [s replaceOccurrencesOfString: @"-" withString:@"+" options: NSLiteralSearch range: r];
                    [s replaceOccurrencesOfString: @"_" withString:@"/" options: NSLiteralSearch range: r];
                    if(s.length % 4 == 2)
                        [s appendString: @"=="];
                    else if(s.length % 4 == 3)
                        [s appendString: @"="];
                    
                    NSData *d = [[NSData alloc] initWithBase64EncodedString:s options: 0];
                    peerGUID = [[NSUUID alloc] initWithUUIDBytes: d.bytes];
                }
                return true;
            });
            nw_txt_record_access_key(txt, "Name", ^ _Bool (const char *n, nw_txt_record_find_key_t k, const unsigned char *value, unsigned long l)
            {
                if(k == nw_txt_record_find_key_non_empty_value)
                    displayName = [NSMutableString stringWithCString: (const char*)value encoding: NSUTF8StringEncoding];
                return true;
            });
            
            if(peerGUID && (displayName || serviceName) && ep && ![_myGUID isEqual: peerGUID])
            {
                //NSLog(@"Found %@ as %@ for %@", displayName, serviceName, peerGUID);
                
                NSString *peerName = displayName ? displayName : serviceName;
                NSUInteger peerno = [self findPeer:peerGUID];
                if(peerno != NSNotFound)
                {
                    Peer *peer = _peers[peerno];
                    peer->endpoint = ep;
                    if(![peerName isEqualToString: peer->name])
                    {
                        _peers[peerno]->name = peerName;
                        [_delegate refreshTable];
                     }
                }
                else
                {
                    Peer *p = [[Peer alloc] init];
                    p->name = peerName;
                    p->guid = peerGUID;
                    p->endpoint = ep;
                    [_peers addObject:p];
                    [_delegate refreshTable];
                }
                
                // Save GUID/name mapping in defaults
                NSUserDefaults *Def = NSUserDefaults.standardUserDefaults;
                NSString *key = [NSString stringWithFormat: @"NameForGUID%@", peerGUID.UUIDString];
                NSString *s = [Def stringForKey: key];
                if(![peerName isEqualToString:s])
                    [Def setObject: peerName forKey: key];
            }
        }
    }
}

-(void)sendText:(NSString*) text peerIndex:(NSInteger)peerIndex
{
    NSData *td = [text dataUsingEncoding:NSUTF8StringEncoding];
    if(td.length >= MAX_MESSAGE_SIZE)
    {
        size_t l = MAX_MESSAGE_SIZE;
        const unsigned char *p = (const unsigned char *)td.bytes;
        while((p[l] & 0xC0) == 0x80)
            --l;
        td = [NSData dataWithBytes: p length: l];
    }
    unsigned char *data = malloc(V0_HEADER_SIZE + td.length);
    *(uint32_t*)data = 0;
    [_myGUID getUUIDBytes: data + 4];
    if(td.length)
        memcpy(data + V0_HEADER_SIZE, td.bytes, td.length);
    dispatch_data_t disp = dispatch_data_create(data, V0_HEADER_SIZE + td.length, dispatch_get_main_queue(), DISPATCH_DATA_DESTRUCTOR_FREE);
    
    nw_endpoint_t toEndpoint = _peers[peerIndex]->endpoint;
    nw_parameters_t params = nw_parameters_create_secure_udp(NW_PARAMETERS_DISABLE_PROTOCOL, NW_PARAMETERS_DEFAULT_CONFIGURATION);
    
    uint16_t port = nw_listener_get_port(_listener);
    // Is there a better way to use the listener port as the outgoing port?
    nw_endpoint_t localEndpoint = nw_endpoint_create_host("0.0.0.0", [NSString stringWithFormat: @"%d", port].UTF8String);
    nw_parameters_set_local_endpoint(params, localEndpoint);
    
    nw_connection_t conn = nw_connection_create(toEndpoint, params);
    nw_connection_set_queue(conn, dispatch_get_main_queue());
    nw_connection_set_state_changed_handler(conn, ^(nw_connection_state_t state, nw_error_t err){
        if(state == nw_connection_state_ready)
        {
            //To see the real IP of the peer, use nw_connection_access_establishment_report() here
            nw_connection_send(conn, disp, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, true, ^(nw_error_t e){
                nw_connection_cancel(conn);
            });
        }
       });
    nw_connection_start(conn);
}

#pragma mark -
#pragma mark Peer list access

-(NSInteger)peerCount
{
    return _peers.count;
}

-(NSString*)peerNameAt:(NSInteger)i
{
    return _peers[i]->name;
}

@end

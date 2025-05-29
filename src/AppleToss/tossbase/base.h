#import <Foundation/Foundation.h>

static const size_t
    GUID_BASE64_LEN = 22,
    V0_HEADER_SIZE = 20,
    MAX_MESSAGE_SIZE = 492;

@interface Peer: NSObject
@end

#pragma mark Callbacks
@protocol TossDelegate
-(NSString*)name: (NSString**)hostname;
-(void)refreshTable;
-(void)onMessageFrom:(NSString*)fromName text:(NSString*)text;
-(void)onError:(NSError*)err ctxt:(NSString*)ctxt;
@end

#pragma mare Core functionality - client and server
@interface TossBase : NSObject
-(id)initWithDelegate:(NSObject<TossDelegate>*)d;

-(NSInteger)peerCount;
-(NSString*)peerNameAt:(NSInteger)i;

-(void)ensureGUID;
-(void)advertise;
-(void)discover;
-(void)sendText:(NSString*) text peerIndex:(NSInteger)peerIndex;
@end

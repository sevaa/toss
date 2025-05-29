#import <Cocoa/Cocoa.h>
#import "../tossbase/base.h"

@interface AppDelegate : NSObject <NSApplicationDelegate, TossDelegate>

@property(nonatomic, weak) IBOutlet NSTableView *theTable;
@property(nonatomic, weak) IBOutlet NSTextField *theText;
@property(nonatomic, weak) IBOutlet NSButton *theSend;
@property(nonatomic, weak) IBOutlet NSTextField *advAs;
@property(nonatomic, weak) IBOutlet NSTextField *sendTo;

-(IBAction)onSend:(id)_;
@end


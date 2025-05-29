#import "AppDelegate.h"
#import "MessageWC.h"

@interface AppDelegate ()
{
    TossBase *_base;
}

@property (strong) IBOutlet NSWindow *window;
@end

@implementation AppDelegate

static NSString *cutLocal(NSString *s)
{
    return [s hasSuffix: @".local"] ? [s substringToIndex: s.length - 6] : s;
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    _window.restorable = NO;
    _base = [[TossBase alloc] initWithDelegate: self];
    [_base ensureGUID];
    [_base advertise];
    [_base discover];
    _advAs.stringValue = [NSString stringWithFormat: @"Advertising as \"%@\".", cutLocal(NSProcessInfo.processInfo.hostName)];
}

-(BOOL)applicationShouldHandleReopen:(NSApplication *)theApplication hasVisibleWindows:(BOOL)visibleWindows
{
    return NO;
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)app
{
    return NO;
}

- (BOOL) applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)_
{
    return YES;
}

-(NSString*)name:(NSString**)host
{
    return *host = cutLocal(NSProcessInfo.processInfo.hostName);
}

-(void)onError:(NSError*)err ctxt:(NSString*)ctxt
{
    NSAlert *a = [NSAlert alertWithError:err];
    a.alertStyle = NSAlertStyleCritical;
    a.messageText = [ctxt stringByAppendingString:a.messageText];
    [a runModal];
}

-(IBAction)onAbout:(id)_
{
    NSDictionary *d =
    @{
        NSAboutPanelOptionApplicationName: @"Toss",
        NSAboutPanelOptionCredits: [[NSAttributedString alloc] initWithString:@"Seva Alekseyev, 2025\nhttps://toss.jishop.com/"]
    };
    [NSApplication.sharedApplication orderFrontStandardAboutPanelWithOptions: d];
}

#pragma mark -

-(void)onMessageFrom:(NSString *)fromName text:(NSString *)text
{
    MessageWC *mwc = [[MessageWC alloc] initWithWindowNibName: @"MessageDlg"];
    [mwc loadWindow];
    mwc.window.restorable = NO;
    mwc.window.title = [NSString stringWithFormat: @"%@ says:", fromName];
    mwc.theText.stringValue = text;
    [mwc.window makeFirstResponder: mwc.theText];
    [mwc.window makeKeyAndOrderFront:self];
    [mwc.theText selectText: self];
}

-(IBAction)onSend:(id)_
{
    [_base sendText: _theText.stringValue peerIndex: _theTable.selectedRow];
}

#pragma mark -
#pragma mark Table population

-(NSInteger)numberOfRowsInTableView:(NSTableView*)_
{
    return [_base peerCount];
}

-(NSView *)tableView:(NSTableView *) tv viewForTableColumn:(NSTableColumn *) col row:(NSInteger) row;
{
    NSTableCellView *v = [tv makeViewWithIdentifier:col.identifier owner: self];
    v.textField.stringValue = [_base peerNameAt:row];
    return v;
}

-(void)tableViewSelectionDidChange:(NSNotification *) notification
{
    NSInteger sel = _theTable.selectedRow;
    _theText.enabled =
        _theSend.enabled = sel >= 0 ? YES : NO;
    _sendTo.stringValue = sel >= 0 ? [NSString stringWithFormat: @"Send to %@", [_base peerNameAt: sel]] : nil;
    [_theText becomeFirstResponder];
}

-(void)refreshTable
{
    //TODO: reload data at index
    [_theTable reloadData];
}
@end

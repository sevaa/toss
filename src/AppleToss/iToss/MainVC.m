#import "MainVC.h"
#include <sys/errno.h>

@interface MainVC ()
{
    TossBase *_base;
    NSInteger _sel;
    NSMutableArray *_incoming;
    bool _alert;
}
@end

@interface IncomingMessage: NSObject
{
@public
    NSString *from, *text;
}
@end

@implementation IncomingMessage
@end

@implementation MainVC

- (void)viewDidLoad
{
    [super viewDidLoad];
    _base = [[TossBase alloc] initWithDelegate: self];
    [_base ensureGUID];
    
    _incoming = [[NSMutableArray alloc] initWithCapacity: 10];
    _advAs.text = [NSString stringWithFormat: @"Advertising as \"%@\".", UIDevice.currentDevice.name];
    
    [_base advertise];
    [_base discover];
}

-(NSString*)name:(NSString**)hostname
{
    // TODO: the business with UIDevice.name
    // See entitlement com.apple.developer.device-information.user-assigned-device-name
    
    UIDevice *d = UIDevice.currentDevice;
    *hostname = d.model;
    return d.name;
}

-(void)refreshTable
{
    [_theTable reloadData];
}

-(void)onError:(NSError*)err ctxt:(NSString*)ctxt
{
}

-(void)onMessageFrom:(NSString*) fromName text:(NSString*)text
{
    if(_alert)
    {
        IncomingMessage *im = [[IncomingMessage alloc] init];
        im->from = fromName;
        im->text = text;
        [_incoming addObject: im];
    }
    else
        [self displayMessageFrom: fromName text: text];
}

-(IBAction)onAbout:(id)bu
{
    NSString *ver = [NSBundle.mainBundle.infoDictionary objectForKey: @"CFBundleShortVersionString"];
    NSString *s = [NSString stringWithFormat: @"Toss - sending small text strings between devices on the same network\n"
                   "Seva Alekseyev, 2025\n"
                   "Version %@\n"
                   "You can find Toss for other platforms at their respective app stores.\n"
                   "The sources are at https://github.com/sevaa/toss/", ver];
    
    UIAlertController *ac = [UIAlertController alertControllerWithTitle:@"About Toss" message: s preferredStyle: UIAlertControllerStyleActionSheet];
    ac.popoverPresentationController.barButtonItem = (UIBarButtonItem*)bu;
    [ac addAction: [UIAlertAction actionWithTitle: @"OK" style: UIAlertActionStyleDefault handler: ^(UIAlertAction *a) { [ac dismissViewControllerAnimated:YES completion: nil]; }]];
    [self presentViewController: ac animated:YES completion: nil];
}

-(void)shareText:(NSString*)s from:(UIView*) v
{
    UIActivityViewController *actVC = [[UIActivityViewController alloc] initWithActivityItems: @[s] applicationActivities:nil];
    actVC.excludedActivityTypes = @[UIActivityTypePrint, UIActivityTypeCopyToPasteboard, UIActivityTypeAssignToContact, UIActivityTypeSaveToCameraRoll]; //Exclude whichever aren't relevant
    actVC.popoverPresentationController.sourceView = v;
    actVC.popoverPresentationController.sourceRect = v.frame;
    
    [self presentViewController:actVC animated:YES completion:nil];
}

-(void)displayMessageFrom:(NSString*)fromName text:(NSString*)text
{
    NSString *title = [NSString stringWithFormat: @"%@ says:", fromName];
    UIAlertController *ac = [UIAlertController alertControllerWithTitle: title message:text preferredStyle:UIAlertControllerStyleAlert];
    UIAlertAction *okBu = [UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleCancel handler:^(UIAlertAction *a) { [self dismissAlert:ac]; }];
    [ac addAction: okBu];
    if(text && text.length)
    {
        [ac addAction: [UIAlertAction actionWithTitle:@"Copy" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a) { UIPasteboard.generalPasteboard.string = text; }]];
        [ac addAction: [UIAlertAction actionWithTitle:@"Share" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a) { [self shareText:text from: ac.view]; }]];
        if([text hasPrefix:@"https://"] || [text hasPrefix: @"http://"])
            [ac addAction: [UIAlertAction actionWithTitle:@"Browse" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a) { [UIApplication.sharedApplication openURL: [NSURL URLWithString: text]]; }]];
    }
    _alert = true;
    [self presentViewController: ac animated:YES completion: nil];
}

-(void)dismissAlert:(UIAlertController*)ac
{
    [ac dismissViewControllerAnimated: YES completion: ^(){
        self->_alert = false;
        if(self->_incoming.count)
        {
            IncomingMessage *im = self->_incoming.firstObject;
            [self->_incoming removeObjectAtIndex: 0];
            [self performSelector: @selector(onQueuedMessage:) withObject: im afterDelay: 1];
        }
    }];
}

-(void)onQueuedMessage:(IncomingMessage*)im
{
    [self displayMessageFrom: im->from text: im->text];
}

#pragma mark -
#pragma mark Sending

-(IBAction)onSend:(id)_
{
    [_base sendText :_theText.text peerIndex: _sel];
}

#pragma mark -
#pragma mark Table population

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    return _base.peerCount;
}

-(UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)ip
{
    UITableViewCell *Cell;
    if((Cell = [tableView dequeueReusableCellWithIdentifier:@"A"]) == nil)
        Cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"A"];

    Cell.textLabel.text = [_base peerNameAt: ip.row];
    return Cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)ip
{
    _sel = ip.row;
    _theText.editable = YES;
    _theSend.enabled = YES;
    _sendTo.text = [NSString stringWithFormat: @"Send to %@", [_base peerNameAt:_sel]];
    [_theText becomeFirstResponder];
}

@end

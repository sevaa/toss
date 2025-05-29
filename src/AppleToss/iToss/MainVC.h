#import <UIKit/UIKit.h>
#import "../tossbase/base.h"

@interface MainVC : UIViewController
<UITableViewDelegate,
UITableViewDataSource,
TossDelegate>
{
    __weak UITableView *_theTable;
    __weak UITextView *_theText;
    __weak UIButton *_theSend;
    __weak UILabel *_advAs;
    __weak UILabel *_sendTo;
    __weak UIView *_topPanel;
    __weak UIView *_bottomPanel;
}

@property(nonatomic, weak) IBOutlet UITableView *theTable;
@property(nonatomic, weak) IBOutlet UITextView *theText;
@property(nonatomic, weak) IBOutlet UIButton *theSend;
@property(nonatomic, weak) IBOutlet UILabel *advAs;
@property(nonatomic, weak) IBOutlet UILabel *sendTo;
@property(nonatomic, weak) IBOutlet UIView *topPanel;
@property(nonatomic, weak) IBOutlet UIView *bottomPanel;

-(IBAction)onSend:(id)_;
-(IBAction)onAbout:(id)bu;
@end


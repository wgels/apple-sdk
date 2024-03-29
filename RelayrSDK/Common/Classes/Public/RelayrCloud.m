#import "RelayrCloud.h"         // Header
#import "RLAWebService.h"       // Relayr.framework (Protocols/Web)
#import "RLAWebService+Cloud.h" // Relayr.framework (Protocols/Web)
#import "RLAWebService+User.h"  // Relayr.framework (Protocols/Web)
#import "RelayrErrors.h"        // Relayr.framework (Utilities)
#import "RLALog.h"              // Relayr.framework (Utilities)

@implementation RelayrCloud

#pragma mark - Public API

- (instancetype)init
{
    [self doesNotRecognizeSelector:_cmd];
    return nil;
}

+ (void)isReachable:(void (^)(NSError* error, NSNumber* isReachable))completion
{
    if (!completion) { return [RLALog debug:RelayrErrorMissingArgument.localizedDescription]; }
    [RLAWebService isRelayrCloudReachable:completion];
}

+ (void)isUserWithEmail:(NSString*)email registered:(void (^)(NSError* error, NSNumber* isUserRegistered))completion
{
    if (!completion) { return [RLALog debug:RelayrErrorMissingArgument.localizedDescription]; }
    [RLAWebService isUserWithEmail:email registeredInRelayrCloud:completion];
}

@end

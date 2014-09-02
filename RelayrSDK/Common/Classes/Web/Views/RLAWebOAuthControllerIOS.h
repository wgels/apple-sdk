@import UIKit;                      // Apple
#import "RLAWebOAuthController.h"   // Relayr.framework (Web)

/*!
 *  @class RLAWebOAuthControllerIOS
 *
 *  @abstract It creates a viewController hosting a <code>UIWebView</code> that will ask for Relayr user credentials.
 *
 *  @see RLAWebOAuthController
 */
@interface RLAWebOAuthControllerIOS : UIViewController <RLAWebOAuthController>

/*!
 *  @method initWithURLRequest:redirectURI:
 *
 *  @abstract It initializes the <code>UIViewController</code> with a request URL and a completion block.
 *  @discussion Both arguments must be valid for the method to not return <code>nil</code>.
 *
 *  @param absoluteURL Request URL shown in the WebView.
 *  @param redirectURI URI used for security measures to test that the answer is coming from the right place.
 *	@return A fully initialised <code>RLAWebOAuthController</code> object implementation.
 */
- (instancetype)initWithURLRequest:(NSURLRequest*)urlRequest
                       redirectURI:(NSString*)redirectURI
                        completion:(void (^)(NSError*, NSString*))completion;

@end
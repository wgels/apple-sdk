@import Foundation;     // Apple

/*!
 *  @abstract This class provides several methods to find out Network properties
 */
@interface RLANetworkInfo : NSObject

/*!
 *  @abstract It returns an array with all the names of the detected networks.
 *
 *	@return <code>NSArray</code> containing <code>NSString</code>s with the names of all detected wifi networks.
 */
+ (NSArray*)networksSSIDs;

/*!
 *  @abstract It finds out the name of the currently connected wifi network.
 *  @discussion If none, <code>nil</code> is returned.
 *
 *	@return <code>NSString</code> with the wifi SSID.
 */
+ (NSString*)currentNetworkSSID;

@end

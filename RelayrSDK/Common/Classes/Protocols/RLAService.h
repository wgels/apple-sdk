@import Foundation;     // Apple
@class RelayrDevice;    // Relayr.framework (Public)

/*!
 *  @abstract All Relayr SDK services must implement the classes listed on this protocol.
 *  @discussion Services will check if the devices subscribed are still <i>living</i> and whehter the have subscription blocks.
 */
@protocol RLAService <NSObject>

@required
/*!
 *  @abstract This methods query the device's data source for the last piece of data.
 *  @discussion The technology used for the query is dependent on which service is this method called onto.
 *
 *  @param device The device interested on.
 *  @param completion Block indicating the subscription status.
 *
 *  @see RelayrDevice
 */
- (void)queryDataFromDevice:(RelayrDevice*)device
                 completion:(void (^)(NSError* error, id value, NSDate* date))completion;

@required
/*!
 *  @abstract This method subscribes to all data from a specific device.
 *  @discussion The technology used for the subscription is dependant on which service is this method called onto.
 *
 *  @param device The device interested on.
 *  @param completion Block indicating the subscription status.
 *
 *  @see RelayrDevice
 */
- (void)subscribeToDataFromDevice:(RelayrDevice*)device
                       completion:(void (^)(NSError* error))completion;

@required
/*!
 *  @abstract This method unsubscribes to all data coming from a specific device.
 *  @discussion The technology used for the subscription is dependant on which service is this method called onto.
 *
 *  @param device The device interested on.
 *
 *  @see RelayrDevice
 */
- (void)unsubscribeToDataFromDevice:(RelayrDevice*)device;

@end

#import "AppDelegate.h"

#import "AppView.h"

@implementation AppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
  self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
  UIViewController* rootViewController = [[UIViewController alloc] init];
  UIView*           view               = [[AppView alloc] initWithFrame:self.window.bounds];

  [rootViewController setView:view];
  [self.window setRootViewController:rootViewController];
  [self.window makeKeyAndVisible];

  return YES;
}

@end

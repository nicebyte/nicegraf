#import "AppView.h"

#import "common.h"
#import "imgui.h"

#import <UIKit/UIKit.h>

@implementation AppView {
  CADisplayLink* _displayLink;
}

- (instancetype)initWithFrame:(CGRect)frame {
  self        = [super initWithFrame:frame];
  self.device = MTLCreateSystemDefaultDevice();
  if (self) {
    // Create and add the display link
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(update:)];
    [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSDefaultRunLoopMode];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationDidEnterBackground)
                                                 name:UIApplicationDidEnterBackgroundNotification
                                               object:nil];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationWillEnterForeground)
                                                 name:UIApplicationWillEnterForegroundNotification
                                               object:nil];
  }
  return self;
}

- (void)didMoveToWindow {
  [super didMoveToWindow];

  ngf_samples::init();
}

- (void)update:(CADisplayLink*)displayLink {
  ngf_samples::draw_frame();
}

- (void)willMoveToWindow:(UIWindow*)newWindow {
  [super willMoveToWindow:newWindow];

  if (newWindow == nil) { ngf_samples::shutdown(); }
}

// Method to pause the display link when the app enters the background
- (void)applicationDidEnterBackground {
  [_displayLink setPaused:YES];
}

// Method to resume the display link when the app becomes active again
- (void)applicationWillEnterForeground {
  [_displayLink setPaused:NO];
}

// Make sure to remove the observers when the object is deallocated
- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];

  [super dealloc];
}

- (void)updateIOWithTouchEvent:(UIEvent*)event {
  UITouch* anyTouch      = event.allTouches.anyObject;
  CGPoint  touchLocation = [anyTouch locationInView:self];
  ImGuiIO& io            = ImGui::GetIO();
  io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
  io.AddMousePosEvent((float)touchLocation.x, (float)touchLocation.y);

  BOOL hasActiveTouch = NO;
  for (UITouch* touch in event.allTouches) {
    if (touch.phase != UITouchPhaseEnded && touch.phase != UITouchPhaseCancelled) {
      hasActiveTouch = YES;
      break;
    }
  }
  io.AddMouseButtonEvent(0, hasActiveTouch);
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  [self updateIOWithTouchEvent:event];
}
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  [self updateIOWithTouchEvent:event];
}
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  [self updateIOWithTouchEvent:event];
}
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  [self updateIOWithTouchEvent:event];
}

@end

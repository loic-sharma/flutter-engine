#import "flutter/shell/platform/darwin/macos/framework/Source/FlutterThreadSynchronizer.h"

#import <QuartzCore/QuartzCore.h>

#include <mutex>
#include <unordered_map>
#include <vector>

#import "flutter/fml/logging.h"
#import "flutter/fml/synchronization/waitable_event.h"

@interface FlutterThreadSynchronizer () {
  std::mutex _mutex;
  BOOL _shuttingDown;
  std::unordered_map<int64_t, CGSize> _contentSizes;
  std::vector<dispatch_block_t> _scheduledBlocks;

  BOOL _beginResizeWaiting;

  // Used to block [beginResize:].
  std::condition_variable _condBlockBeginResize;
}

- (BOOL)allViewsHaveFrame;

@end

@implementation FlutterThreadSynchronizer

- (BOOL)allViewsHaveFrame {
  for (auto const& [viewId, contentSize] : _contentSizes) {
    if (CGSizeEqualToSize(contentSize, CGSizeZero)) {
      return NO;
    }
  }
  return YES;
}

- (void)drain {
  FML_DCHECK([NSThread isMainThread]);

  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  for (dispatch_block_t block : _scheduledBlocks) {
    block();
  }
  [CATransaction commit];
  _scheduledBlocks.clear();
}

- (void)blockUntilFrameAvailable {
  std::unique_lock<std::mutex> lock(_mutex);

  _beginResizeWaiting = YES;

  while (![self allViewsHaveFrame] && !_shuttingDown) {
    _condBlockBeginResize.wait(lock);
    [self drain];
  }

  _beginResizeWaiting = NO;
}

- (void)beginResizeForView:(int64_t)viewId size:(CGSize)size notify:(nonnull dispatch_block_t)notify {
  std::unique_lock<std::mutex> lock(_mutex);

  if (![self allViewsHaveFrame] || _shuttingDown) {
    // No blocking until framework produces at least one frame
    notify();
    return;
  }

  [self drain];

  notify();

  _contentSizes[viewId] = CGSizeMake(-1, -1);

  _beginResizeWaiting = YES;

  while (true) {
    if (_shuttingDown) {
      break;
    }
    const CGSize& contentSize = _contentSizes[viewId];
    if (CGSizeEqualToSize(contentSize, size) ||
        CGSizeEqualToSize(contentSize, CGSizeZero)) {
      break;
    }
    _condBlockBeginResize.wait(lock);
    [self drain];
  }

  _beginResizeWaiting = NO;
}

- (void)performCommitForView:(int64_t)viewId size:(CGSize)size notify:(nonnull dispatch_block_t)notify {
  fml::AutoResetWaitableEvent event;
  {
    std::unique_lock<std::mutex> lock(_mutex);
    if (_shuttingDown) {
      // Engine is shutting down, main thread may be blocked by the engine
      // waiting for raster thread to finish.
      return;
    }
    fml::AutoResetWaitableEvent& e = event;
    _scheduledBlocks.push_back(^{
      notify();
      _contentSizes[viewId] = size;
      e.Signal();
    });
    if (_beginResizeWaiting) {
      _condBlockBeginResize.notify_all();
    } else {
      dispatch_async(dispatch_get_main_queue(), ^{
        std::unique_lock<std::mutex> lock(_mutex);
        [self drain];
      });
    }
  }
  event.Wait();
}

- (void)registerView:(int64_t)viewId {
  _contentSizes[viewId] = CGSizeZero;
}

- (void)deregisterView:(int64_t)viewId {
  _contentSizes.erase(viewId);
}

- (void)shutdown {
  std::unique_lock<std::mutex> lock(_mutex);
  _shuttingDown = YES;
  _condBlockBeginResize.notify_all();
  [self drain];
}

@end

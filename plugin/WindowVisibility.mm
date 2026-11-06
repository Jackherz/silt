#include "WindowVisibility.h"

#import <AppKit/AppKit.h>

namespace silt::ui
{
bool windowIsSeen (juce::Component& component)
{
    auto* peer = component.getPeer();
    if (peer == nullptr)
        return true;
    NSWindow* window = [(NSView*) peer->getNativeHandle() window];
    if (window == nil)
        return false;
    return [window isVisible] && ! [window isMiniaturized] && ([window occlusionState] & NSWindowOcclusionStateVisible) != 0;
}
}

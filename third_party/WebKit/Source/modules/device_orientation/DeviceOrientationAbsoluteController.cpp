// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "modules/device_orientation/DeviceOrientationAbsoluteController.h"

#include "core/dom/Document.h"
#include "core/frame/Settings.h"
#include "modules/device_orientation/DeviceOrientationDispatcher.h"

namespace blink {

DeviceOrientationAbsoluteController::DeviceOrientationAbsoluteController(Document& document)
    : DeviceOrientationController(document)
{
}

DeviceOrientationAbsoluteController::~DeviceOrientationAbsoluteController()
{
#if !ENABLE(OILPAN)
    stopUpdating();
#endif
}

const char* DeviceOrientationAbsoluteController::supplementName()
{
    return "DeviceOrientationAbsoluteController";
}

DeviceOrientationAbsoluteController& DeviceOrientationAbsoluteController::from(Document& document)
{
    DeviceOrientationAbsoluteController* controller = static_cast<DeviceOrientationAbsoluteController*>(WillBeHeapSupplement<Document>::from(document, DeviceOrientationAbsoluteController::supplementName()));
    if (!controller) {
        controller = new DeviceOrientationAbsoluteController(document);
        WillBeHeapSupplement<Document>::provideTo(document, DeviceOrientationAbsoluteController::supplementName(), adoptPtrWillBeNoop(controller));
    }
    return *controller;
}

void DeviceOrientationAbsoluteController::didAddEventListener(LocalDOMWindow* window, const AtomicString& eventType)
{
    if (eventType != eventTypeName())
        return;

    if (document().frame()) {
        String errorMessage;
        if (document().isSecureContext(errorMessage)) {
            UseCounter::count(document().frame(), UseCounter::DeviceOrientationAbsoluteSecureOrigin);
        } else {
            UseCounter::countDeprecation(document().frame(), UseCounter::DeviceOrientationAbsoluteInsecureOrigin);
            // TODO: add rappor logging of insecure origins as in DeviceOrientationController.
            if (document().frame()->settings()->strictPowerfulFeatureRestrictions())
                return;
        }
    }

    // TODO: add rappor url logging as in DeviceOrientationController.

    DeviceSingleWindowEventController::didAddEventListener(window, eventType);
}

DeviceOrientationDispatcher& DeviceOrientationAbsoluteController::dispatcherInstance() const
{
    return DeviceOrientationDispatcher::instance(true);
}

const AtomicString& DeviceOrientationAbsoluteController::eventTypeName() const
{
    return EventTypeNames::deviceorientationabsolute;
}

DEFINE_TRACE(DeviceOrientationAbsoluteController)
{
    DeviceOrientationController::trace(visitor);
}

} // namespace blink

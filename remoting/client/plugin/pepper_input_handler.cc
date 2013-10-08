// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/client/plugin/pepper_input_handler.h"

#include "base/logging.h"
#include "ppapi/c/dev/ppb_keyboard_input_event_dev.h"
#include "ppapi/cpp/input_event.h"
#include "ppapi/cpp/module_impl.h"
#include "ppapi/cpp/point.h"
#include "remoting/proto/event.pb.h"

namespace remoting {

PepperInputHandler::PepperInputHandler(protocol::InputStub* input_stub)
    : input_stub_(input_stub),
      wheel_delta_x_(0),
      wheel_delta_y_(0),
      wheel_ticks_x_(0),
      wheel_ticks_y_(0) {
}

PepperInputHandler::~PepperInputHandler() {
}

// Helper function to get the USB key code using the Dev InputEvent interface.
uint32_t GetUsbKeyCode(pp::KeyboardInputEvent pp_key_event) {
  const PPB_KeyboardInputEvent_Dev* key_event_interface =
      reinterpret_cast<const PPB_KeyboardInputEvent_Dev*>(
          pp::Module::Get()->GetBrowserInterface(
              PPB_KEYBOARD_INPUT_EVENT_DEV_INTERFACE));
  if (!key_event_interface)
    return 0;
  return key_event_interface->GetUsbKeyCode(pp_key_event.pp_resource());
}

bool PepperInputHandler::HandleInputEvent(const pp::InputEvent& event) {
  switch (event.GetType()) {
    case PP_INPUTEVENT_TYPE_CONTEXTMENU: {
      // We need to return true here or else we'll get a local (plugin) context
      // menu instead of the mouseup event for the right click.
      return true;
    }

    case PP_INPUTEVENT_TYPE_KEYDOWN:
    case PP_INPUTEVENT_TYPE_KEYUP: {
      pp::KeyboardInputEvent pp_key_event(event);
      uint32_t modifiers = event.GetModifiers();
      uint32_t lock_states = 0;

      if (modifiers & PP_INPUTEVENT_MODIFIER_CAPSLOCKKEY)
        lock_states |= protocol::KeyEvent::LOCK_STATES_CAPSLOCK;

      if (modifiers & PP_INPUTEVENT_MODIFIER_NUMLOCKKEY)
        lock_states |= protocol::KeyEvent::LOCK_STATES_NUMLOCK;

      protocol::KeyEvent key_event;
      key_event.set_usb_keycode(GetUsbKeyCode(pp_key_event));
      key_event.set_pressed(event.GetType() == PP_INPUTEVENT_TYPE_KEYDOWN);
      key_event.set_lock_states(lock_states);

      input_stub_->InjectKeyEvent(key_event);
      return true;
    }

    case PP_INPUTEVENT_TYPE_MOUSEDOWN:
    case PP_INPUTEVENT_TYPE_MOUSEUP: {
      pp::MouseInputEvent pp_mouse_event(event);
      protocol::MouseEvent mouse_event;
      switch (pp_mouse_event.GetButton()) {
        case PP_INPUTEVENT_MOUSEBUTTON_LEFT:
          mouse_event.set_button(protocol::MouseEvent::BUTTON_LEFT);
          break;
        case PP_INPUTEVENT_MOUSEBUTTON_MIDDLE:
          mouse_event.set_button(protocol::MouseEvent::BUTTON_MIDDLE);
          break;
        case PP_INPUTEVENT_MOUSEBUTTON_RIGHT:
          mouse_event.set_button(protocol::MouseEvent::BUTTON_RIGHT);
          break;
        case PP_INPUTEVENT_MOUSEBUTTON_NONE:
          break;
      }
      if (mouse_event.has_button()) {
        bool is_down = (event.GetType() == PP_INPUTEVENT_TYPE_MOUSEDOWN);
        mouse_event.set_button_down(is_down);
        mouse_event.set_x(pp_mouse_event.GetPosition().x());
        mouse_event.set_y(pp_mouse_event.GetPosition().y());
        input_stub_->InjectMouseEvent(mouse_event);
      }
      return true;
    }

    case PP_INPUTEVENT_TYPE_MOUSEMOVE:
    case PP_INPUTEVENT_TYPE_MOUSEENTER:
    case PP_INPUTEVENT_TYPE_MOUSELEAVE: {
      pp::MouseInputEvent pp_mouse_event(event);
      protocol::MouseEvent mouse_event;
      mouse_event.set_x(pp_mouse_event.GetPosition().x());
      mouse_event.set_y(pp_mouse_event.GetPosition().y());
      input_stub_->InjectMouseEvent(mouse_event);
      return true;
    }

    case PP_INPUTEVENT_TYPE_WHEEL: {
      pp::WheelInputEvent pp_wheel_event(event);

      // Don't handle scroll-by-page events, for now.
      if (pp_wheel_event.GetScrollByPage())
        return false;

      // Add this event to our accumulated sub-pixel deltas and clicks.
      pp::FloatPoint delta = pp_wheel_event.GetDelta();
      wheel_delta_x_ += delta.x();
      wheel_delta_y_ += delta.y();
      pp::FloatPoint ticks = pp_wheel_event.GetTicks();
      wheel_ticks_x_ += ticks.x();
      wheel_ticks_y_ += ticks.y();

      // If there is at least a pixel's movement, emit an event. We don't
      // ever expect to accumulate one tick's worth of scrolling without
      // accumulating a pixel's worth at the same time, so this is safe.
      int delta_x = static_cast<int>(wheel_delta_x_);
      int delta_y = static_cast<int>(wheel_delta_y_);
      if (delta_x != 0 || delta_y != 0) {
        wheel_delta_x_ -= delta_x;
        wheel_delta_y_ -= delta_y;
        protocol::MouseEvent mouse_event;
        mouse_event.set_wheel_delta_x(delta_x);
        mouse_event.set_wheel_delta_y(delta_y);

        // Always include the ticks in the event, even if insufficient pixel
        // scrolling has accumulated for a single tick. This informs hosts
        // that can't inject pixel-based scroll events that the client will
        // accumulate them into tick-based scrolling, which gives a better
        // overall experience than trying to do this host-side.
        int ticks_x = static_cast<int>(wheel_ticks_x_);
        int ticks_y = static_cast<int>(wheel_ticks_y_);
        wheel_ticks_x_ -= ticks_x;
        wheel_ticks_y_ -= ticks_y;
        mouse_event.set_wheel_ticks_x(ticks_x);
        mouse_event.set_wheel_ticks_y(ticks_y);

        input_stub_->InjectMouseEvent(mouse_event);
      }
      return true;
    }

    case PP_INPUTEVENT_TYPE_CHAR:
      // Consume but ignore character input events.
      return true;

    default: {
      LOG(INFO) << "Unhandled input event: " << event.GetType();
      break;
    }
  }

  return false;
}

}  // namespace remoting

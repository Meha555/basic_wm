/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  Copyright (C) 2013-2017 Chuan Ji <ji@chu4n.com>                          *
 *                                                                           *
 *  Licensed under the Apache License, Version 2.0 (the "License");          *
 *  you may not use this file except in compliance with the License.         *
 *  You may obtain a copy of the License at                                  *
 *                                                                           *
 *   http://www.apache.org/licenses/LICENSE-2.0                              *
 *                                                                           *
 *  Unless required by applicable law or agreed to in writing, software      *
 *  distributed under the License is distributed on an "AS IS" BASIS,        *
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. *
 *  See the License for the specific language governing permissions and      *
 *  limitations under the License.                                           *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "window_manager.hpp"
extern "C" {
#include <X11/Xutil.h>
}
#include <cstring>
#include <algorithm>
#include <glog/logging.h>
#include "util.hpp"

using ::std::max;
using ::std::mutex;
using ::std::string;
using ::std::unique_ptr;

bool WindowManager::wm_detected_;
mutex WindowManager::wm_detected_mutex_;

/// @brief 
/// @param display_str 是创建窗口的CLI参数，决定WM的实例特性
/// @return 函数返回的是临时unique_ptr，因此是亡值，调用移动构造函数（因此成立，unique_ptr没有拷贝构造）
unique_ptr<WindowManager> WindowManager::Create(const string& display_str) {
  // 1. Open X display.
  const char* display_c_str =
        display_str.empty() ? nullptr : display_str.c_str();
  Display* display = XOpenDisplay(display_c_str);
  if (display == nullptr) {
    LOG(ERROR) << "Failed to open X display " << XDisplayName(display_c_str);
    return nullptr;
  }
  // 2. Construct WindowManager instance.
  return unique_ptr<WindowManager>(new WindowManager(display));
}

WindowManager::WindowManager(Display* display)
    : display_(CHECK_NOTNULL(display)),
      root_(DefaultRootWindow(display_)), //获取顶层窗口ID
      WM_PROTOCOLS(XInternAtom(display_, "WM_PROTOCOLS", false)),//不存在就创建
      WM_DELETE_WINDOW(XInternAtom(display_, "WM_DELETE_WINDOW", false)) //不存在就创建
{}

WindowManager::~WindowManager() {
  XCloseDisplay(display_);
}

void WindowManager::Run() {
  // 1. Initialization.
  //   a. Select events on root window. Use a special error handler so we can
  //   exit gracefully if another window manager is already running.
  // 1. 初始化。
  //   a. 为根窗口指定一个错误处理回调，这样可以在多次启动WM时处理退出工作。
  {
    ::std::lock_guard<mutex> lock(wm_detected_mutex_);

    wm_detected_ = false;
    XSetErrorHandler(&WindowManager::OnWMDetected);
    XSelectInput(
        display_,
        root_,
        SubstructureRedirectMask | SubstructureNotifyMask); //NOTE - 窗口管理器要能响应子结构重定向事件（手册上只有这两个与子结构重定向的事件掩码）
    XSync(display_, false); //REVIEW - 不会丢弃在事件队列中的事件
    if (wm_detected_) {
      LOG(ERROR) << "Detected another window manager on display "
                 << XDisplayString(display_);
      return;
    }
  }
  //   b. Set error handler.
  XSetErrorHandler(&WindowManager::OnXError);
  //   c. Grab X server to prevent windows from changing under us.
  XGrabServer(display_);
  //   d. Reparent existing top-level windows.
  //     i. Query existing top-level windows.
  Window returned_root, returned_parent;
  Window* top_level_windows;
  unsigned int num_top_level_windows;
  //由于是查询根窗口的所有子窗口，因此就能得到当前所有的子窗口
  //从而对所有子窗口进行repaint/frame
  CHECK(XQueryTree(
      display_,
      root_,
      &returned_root,//根窗口ID
      &returned_parent,//父窗口ID
      &top_level_windows, //子窗口ID数组
      &num_top_level_windows));//子窗口的数量
  CHECK_EQ(returned_root, root_);
  //     ii. Frame each top-level window.
  for (unsigned int i = 0; i < num_top_level_windows; ++i) {
    //显然，在Run()调用时，当前存在的所有窗口都是在WM创建前创建的
    Frame(top_level_windows[i], true);
  }

  //REVIEW - 为什么要释放在WM启动之前的所有窗口？？
  //     iii. Free top-level window array.
  XFree(top_level_windows);
  //   e. Ungrab X server.
  XUngrabServer(display_);

  // 2. Main event loop.
  for (;;) {
    // 1. Get next event.
    XEvent e;
    XNextEvent(display_, &e);
    LOG(INFO) << "Received event: " << ToString(e);

    // 2. Dispatch event.
    switch (e.type) {
      case CreateNotify:
        OnCreateNotify(e.xcreatewindow);
        break;
      case DestroyNotify:
        OnDestroyNotify(e.xdestroywindow);
        break;
      case ReparentNotify:
        OnReparentNotify(e.xreparent);
        break;
      case MapNotify:
        OnMapNotify(e.xmap);
        break;
      case UnmapNotify:
        OnUnmapNotify(e.xunmap);
        break;
      case ConfigureNotify:
        OnConfigureNotify(e.xconfigure);
        break;
      case MapRequest:
        OnMapRequest(e.xmaprequest);
        break;
      case ConfigureRequest:
        OnConfigureRequest(e.xconfigurerequest);
        break;
      case ButtonPress:
        OnButtonPress(e.xbutton);
        break;
      case ButtonRelease:
        OnButtonRelease(e.xbutton);
        break;
      case MotionNotify:
        // Skip any already pending motion events.
        while (XCheckTypedWindowEvent(
            display_, e.xmotion.window, MotionNotify, &e)) {}
        OnMotionNotify(e.xmotion);
        break;
      case KeyPress:
        OnKeyPress(e.xkey);
        break;
      case KeyRelease:
        OnKeyRelease(e.xkey);
        break;
      default:
        LOG(WARNING) << "Ignored event";
    }
  }
}

void WindowManager::Frame(Window w, bool was_created_before_window_manager) {
  // Visual properties of the frame to create.
  const unsigned int BORDER_WIDTH = 3;
  const unsigned long BORDER_COLOR = 0xff0000;
  const unsigned long BG_COLOR = 0x0000ff;

  // We shouldn't be framing windows we've already framed.
  CHECK(!clients_.count(w)); //避免重复frame

  // 1. Retrieve attributes of window to frame.
  // 1. 先获取待frame的窗口的属性
  XWindowAttributes x_window_attrs;
  CHECK(XGetWindowAttributes(display_, w, &x_window_attrs));

  // 2. If window was created before window manager started, we should frame
  // it only if it is visible and doesn't set override_redirect.
  // 2. 如果窗口是在窗口管理器启动之前创建的，
  // 我们应该只在它 可见 且 没有设置override_redirect的情况下才对它进行frame。
  if (was_created_before_window_manager) {
    if (x_window_attrs.override_redirect ||
        x_window_attrs.map_state != IsViewable) {
      return;
    }
  }

  // 3. Create frame.
  const Window frame = XCreateSimpleWindow(
      display_,
      root_,
      x_window_attrs.x,
      x_window_attrs.y,
      x_window_attrs.width,
      x_window_attrs.height,
      BORDER_WIDTH,
      BORDER_COLOR,
      BG_COLOR);
  // 4. Select events on frame.
  XSelectInput(
      display_,
      frame,
      SubstructureRedirectMask | SubstructureNotifyMask);
  // 5. Add client to save set, so that it will be restored and kept alive if we
  // crash.
  XAddToSaveSet(display_, w); //REVIEW - save set是什么？
  // 6. Reparent client window.
  XReparentWindow(
      display_,
      w,
      frame,
      0, 0);  // Offset of client window within frame.
  // 7. Map frame.
  XMapWindow(display_, frame); //REVIEW - 什么是Map操作和Unmap操作？
  // 8. Save frame handle.
  clients_[w] = frame; //存入哈希表
  // 9. Grab universal window management actions on client window.
  //   a. Move windows with alt + left button.
  //REVIEW - 什么是Grab操作？
  XGrabButton(
      display_,
      Button1,
      Mod1Mask,
      w,
      false,
      ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
      GrabModeAsync,
      GrabModeAsync,
      None,
      None);
  //   b. Resize windows with alt + right button.
  XGrabButton(
      display_,
      Button3,
      Mod1Mask,
      w,
      false,
      ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
      GrabModeAsync,
      GrabModeAsync,
      None,
      None);
  //   c. Kill windows with alt + f4.
  XGrabKey(
      display_,
      XKeysymToKeycode(display_, XK_F4),
      Mod1Mask,
      w,
      false,
      GrabModeAsync,
      GrabModeAsync);
  //   d. Switch windows with alt + tab.
  XGrabKey(
      display_,
      XKeysymToKeycode(display_, XK_Tab),
      Mod1Mask,
      w,
      false,
      GrabModeAsync,
      GrabModeAsync);

  LOG(INFO) << "Framed window " << w << " [" << frame << "]";
}

void WindowManager::Unframe(Window w) {
  CHECK(clients_.count(w));

  // We reverse the steps taken in Frame().
  // unframe的步骤就是倒过来的frame步骤
  const Window frame = clients_[w];
  // 1. Unmap frame.
  XUnmapWindow(display_, frame);
  // 2. Reparent client window.
  XReparentWindow(
      display_,
      w,
      root_,
      0, 0);  // Offset of client window within root.
  // 3. Remove client window from save set, as it is now unrelated to us.
  XRemoveFromSaveSet(display_, w);
  // 4. Destroy frame.
  XDestroyWindow(display_, frame);
  // 5. Drop reference to frame handle.
  clients_.erase(w);

  LOG(INFO) << "Unframed window " << w << " [" << frame << "]";
}

void WindowManager::OnCreateNotify(const XCreateWindowEvent& e) {}

void WindowManager::OnDestroyNotify(const XDestroyWindowEvent& e) {}

void WindowManager::OnReparentNotify(const XReparentEvent& e) {}

void WindowManager::OnMapNotify(const XMapEvent& e) {}

void WindowManager::OnUnmapNotify(const XUnmapEvent& e) {
  // If the window is a client window we manage, unframe it upon UnmapNotify.
  // We need the check because we will receive an UnmapNotify event for a
  // frame window we just destroyed ourselves.
  // 如果窗口是我们管理的客户端窗口，在UnmapNotify这个函数中进行unframe
  if (!clients_.count(e.window)) {
      LOG(INFO) << "Ignore UnmapNotify for non-client window " << e.window;
    return;
  }

  // Ignore event if it is triggered by reparenting a window that was mapped
  // before the window manager started.
  // 忽略由WM启动之前mapped的窗口的reparent操作所出发的事件
  // Since we receive UnmapNotify events from the SubstructureNotify mask, the
  // event attribute specifies the parent window of the window that was
  // unmapped. This means that an UnmapNotify event from a normal client window
  // should have this attribute set to a frame window we maintain. Only an
  // UnmapNotify event triggered by reparenting a pre-existing window will have
  // this attribute set to the root window.
  //REVIEW - 没懂
  /*
  因为我们从SubstructureNotify掩码接收UnmapNotify事件，所以event属性指定了unmap的窗口的父窗口。
  这意味着来自普通客户端窗口的UnmapNotify事件应该将此属性设置到我们维护的frame窗口上。
  只有当repaint一个已经存在的窗口时触发的UnmapNotify事件才会将这个属性设置到根窗口上。
  */
  if (e.event == root_) {
    LOG(INFO) << "Ignore UnmapNotify for reparented pre-existing window "
              << e.window;
    return;
  }

  Unframe(e.window);
}

void WindowManager::OnConfigureNotify(const XConfigureEvent& e) {}

void WindowManager::OnMapRequest(const XMapRequestEvent& e) {
  // 1. Frame or re-frame window.
  Frame(e.window, false);
  // 2. Actually map window.
  XMapWindow(display_, e.window);
}

void WindowManager::OnConfigureRequest(const XConfigureRequestEvent& e) {
  XWindowChanges changes{
    .x = e.x,
    .y = e.y,
    .width = e.width,
    .height = e.height,
    .border_width = e.border_width,
    .sibling = e.above,
    .stack_mode = e.detail
  };
  if (clients_.count(e.window)) { // 如果该窗口拥有frame窗口，则frame窗口也需要改变config
    const Window frame = clients_[e.window];
    XConfigureWindow(display_, frame, e.value_mask, &changes);
    LOG(INFO) << "Resize [" << frame << "] to " << Size<int>(e.width, e.height);
  }
  XConfigureWindow(display_, e.window, e.value_mask, &changes);
  LOG(INFO) << "Resize " << e.window << " to " << Size<int>(e.width, e.height);
}

void WindowManager::OnButtonPress(const XButtonEvent& e) {
  //检查并获取frame窗口
  CHECK(clients_.count(e.window));
  const Window frame = clients_[e.window];

  // 1. Save initial cursor position.
  drag_start_pos_ = Position<int>(e.x_root, e.y_root);

  // 2. Save initial window info.
  Window returned_root;
  int x, y;
  unsigned width, height, border_width, depth;
  CHECK(XGetGeometry(
      display_,
      frame,
      &returned_root,
      &x, &y,
      &width, &height,
      &border_width,
      &depth));
  drag_start_frame_pos_ = Position<int>(x, y);
  drag_start_frame_size_ = Size<int>(width, height);

  // 3. Raise clicked window to top.
  XRaiseWindow(display_, frame);
}

void WindowManager::OnButtonRelease(const XButtonEvent& e) {}
//REVIEW - 暂时没研究怎么实现变换的
void WindowManager::OnMotionNotify(const XMotionEvent& e) {
  //检查并获取frame窗口
  CHECK(clients_.count(e.window));
  const Window frame = clients_[e.window];
  
  const Position<int> drag_pos(e.x_root, e.y_root);
  const Vector2D<int> delta = drag_pos - drag_start_pos_;

  if (e.state & Button1Mask ) {
    // alt + left button: Move window.
    const Position<int> dest_frame_pos = drag_start_frame_pos_ + delta;
    XMoveWindow(
        display_,
        frame,
        dest_frame_pos.x, dest_frame_pos.y);
  } else if (e.state & Button3Mask) {
    // alt + right button: Resize window.
    // Window dimensions cannot be negative.
    const Vector2D<int> size_delta(
        max(delta.x, -drag_start_frame_size_.width),
        max(delta.y, -drag_start_frame_size_.height));
    const Size<int> dest_frame_size = drag_start_frame_size_ + size_delta;
    // 1. Resize frame.
    XResizeWindow(
        display_,
        frame,
        dest_frame_size.width, dest_frame_size.height);
    // 2. Resize client window.
    XResizeWindow(
        display_,
        e.window,
        dest_frame_size.width, dest_frame_size.height);
  }
}

void WindowManager::OnKeyPress(const XKeyEvent& e) {
  if ((e.state & Mod1Mask) &&
      (e.keycode == XKeysymToKeycode(display_, XK_F4))) {
    // alt + f4: Close window.
    //
    // There are two ways to tell an X window to close. The first is to send it
    // a message of type WM_PROTOCOLS and value WM_DELETE_WINDOW. If the client
    // has not explicitly marked itself as supporting this more civilized
    // behavior (using XSetWMProtocols()), we kill it with XKillClient().
    /*
    有两种方法可以让X窗口关闭。
    - 第一种是发送一个类型为WM_PROTOCOLS的消息，值为WM_DELETE_WINDOW。
    - 否则如果客户端没有使用XSetWMProtocols()来标记自己支持这种更安全的关闭方式，就得使用XKillClient()关闭窗口。
    */
    // 因此先检查要关闭的窗体是否有WM_DELETE_WINDOW的属性
    Atom* supported_protocols;
    int num_supported_protocols;
    if (XGetWMProtocols(display_,
                        e.window,
                        &supported_protocols,
                        &num_supported_protocols) &&
        (::std::find(supported_protocols,
                     supported_protocols + num_supported_protocols,
                     WM_DELETE_WINDOW) !=
         supported_protocols + num_supported_protocols)) {
      LOG(INFO) << "Gracefully deleting window " << e.window;
      // 1. Construct message.
      XEvent msg;
      memset(&msg, 0, sizeof(msg));
      msg.xclient.type = ClientMessage;
      msg.xclient.message_type = WM_PROTOCOLS;
      msg.xclient.window = e.window;
      msg.xclient.format = 32;
      msg.xclient.data.l[0] = WM_DELETE_WINDOW;
      // 2. Send message to window to be closed.
      CHECK(XSendEvent(display_, e.window, false, 0, &msg));
    } else {
      LOG(INFO) << "Killing window " << e.window;
      XKillClient(display_, e.window);
    }
  } else if ((e.state & Mod1Mask) &&
             (e.keycode == XKeysymToKeycode(display_, XK_Tab))) {
    // alt + tab: Switch window.
    // 1. Find next window.
    auto i = clients_.find(e.window);
    CHECK(i != clients_.end());
    ++i;//拿到当前的窗口的下一个窗口的迭代器
    if (i == clients_.end()) 
      i = clients_.begin();
    // 2. Raise and set focus.
    XRaiseWindow(display_, i->second);
    XSetInputFocus(display_, i->first, RevertToPointerRoot, CurrentTime);
  }
}

void WindowManager::OnKeyRelease(const XKeyEvent& e) {}

int WindowManager::OnXError(Display* display, XErrorEvent* e) {
  const int MAX_ERROR_TEXT_LENGTH = 1024;
  char error_text[MAX_ERROR_TEXT_LENGTH];
  XGetErrorText(display, e->error_code, error_text, sizeof(error_text));
  LOG(ERROR) << "Received X error:\n"
             << "    Request: " << int(e->request_code)
             << " - " << XRequestCodeToString(e->request_code) << "\n"
             << "    Error code: " << int(e->error_code)
             << " - " << error_text << "\n"
             << "    Resource ID: " << e->resourceid;
  // The return value is ignored.
  return 0;
}

int WindowManager::OnWMDetected(Display* display, XErrorEvent* e) {
  // In the case of an already running window manager, the error code from
  // XSelectInput is BadAccess. We don't expect this handler to receive any
  // other errors.
  CHECK_EQ(static_cast<int>(e->error_code), BadAccess);
  // Set flag.
  wm_detected_ = true;
  // The return value is ignored.
  return 0;
}

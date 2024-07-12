Basic Window Manager
====================

basic_wm is a simple reparenting, non-compositing X window manager that demonstrates
how to implement the fundamental functionality of a window manager:

  * Managing the life cycle of windows
  * Reparenting windows
  * Basic window operations (moving, resizing, closing)

It serves as a pedagogical example for my series of articles,
[_How X Window Managers Work, And How To Write One_](https://jichu4n.com/posts/how-x-window-managers-work-and-how-to-write-one-part-i/).


Building and Running
--------------------

To build this example window manager, you will need:

* A C++-11 enabled C++ compiler
* [GNU Make](https://www.gnu.org/software/make/)
* Xlib headers and libraries
* [google-glog](https://code.google.com/p/google-glog/) library

To run and test it, you will need:

* [Xephyr](http://www.freedesktop.org/wiki/Software/Xephyr/)
* `xinit`
* Random X utilities such as `xclock`, `xeyes`, and `xterm` to play with

On [Ubuntu](https://www.ubuntu.com)/[Debian](https://www.debian.org/), you can
install these dependencies with:

    sudo apt-get install \
        build-essential libx11-dev libgoogle-glog-dev \
        xserver-xephyr xinit x11-apps xterm

On [Fedora](https://getfedora.org)/[CentOS](https://www.centos.org), you can install these with:

    sudo yum install \
        make gcc gcc-c++ libX11-devel glog-devel \
        xorg-x11-server-Xephyr xorg-x11-apps xterm

On [Arch Linux](http://www.archlinux.org), you can install these with:

    yaourt -S base-devel libx11 google-glog \
        xorg-server-xephyr xorg-xinit xorg-xclock xorg-xeyes xterm

Consult your own distro's documentation for how to install these.

Once you have all the dependencies, building and running it is as simple as:

    ./build_and_run.sh

This will launch a simple Xephyr session like in the following screenshot:
![Screenshot](basic_wm_screenshot.png)


Usage
-----

> 存在小键盘的键盘，在开启NumLock时，按下的键会带上一个NumLock

我使用的是带小键盘的电脑，使用时需要先关闭所有的键盘修饰键（如 <kbd>NumLock</kbd>、<kbd>CapsLock</kbd> ）。

可以使用 `xmodmap` 命令查看key modifier 掩码：

```shell
xmodmap:  up to 4 keys per modifier, (keycodes in parentheses):

shift       Shift_L (0x32),  Shift_R (0x3e)
lock        Caps_Lock (0x42)
control     Control_L (0x25),  Control_R (0x69)
mod1        Alt_L (0x40),  Alt_R (0x6c),  Meta_L (0xcd)
mod2        Num_Lock (0x4d)
mod3      
mod4        Super_L (0x85),  Super_R (0x86),  Super_L (0xce),  Hyper_L (0xcf)
mod5        ISO_Level3_Shift (0x5c),  Mode_switch (0xcb)
```

Supported keyboard shortcuts:

* **Alt + Left Click**: Move window
* **Alt + Right Click**: Resize window
* **Alt + F4**: Close window
* **Alt + Tab**: Switch window


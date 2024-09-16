# evdevpp

<p>
    <a href="https://github.com/mikael-s-persson/evdevpp/blob/main/LICENSE"></a>
</p>

This library provides bindings to the generic input event interface in Linux.
The *evdev* interface serves the purpose of passing events generated in the
kernel directly to userspace through character devices that are typically
located in `/dev/input/`.

This library also comes with bindings to *uinput*, the userspace input
subsystem. *Uinput* allows userspace programs to create and handle input devices
that can inject events directly into the input subsystem.

### Building

This library uses the [Bazel](https://bazel.build/) build system. As such, to
build this library from source, use the following:

```
$ git clone https://github.com/mikael-s-persson/evdevpp.git
$ cd evdevpp
$ bazel build //...:all
```

Configurations available include `--config=clang` (for Clang) and `--config=libc++` (for Clang + libc++).

To import this library into your own Bazel project, put the following in your MODULE.bazel:

```
# Load git_repository rule if not loaded already
git_repository = use_repo_rule("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

git_repository(
    name = "evdevpp",
    branch = "main",
    remote = "https://github.com/mikael-s-persson/evdevpp.git",
)

# Bazel rules should depend on: "@evdevpp"
```

### Reporting issues

This is still a largely untested library. It was ported from python-evdev, which is well-tested, and
relying, of course, on libevdev, which has been a staple of the Linux eco-system for long time.
Nevertheless, there are bugs in this library, I am sure of it.

Please report any issues at: https://github.com/mikael-s-persson/evdevpp/issues

### Documentation

Some useful documentation can be found here:

Linux kernel input subsystem docs: https://www.kernel.org/doc/html/v4.12/input/input_uapi.html

Python-evdev library docs: https://python-evdev.readthedocs.io/en/latest/

### Development

https://github.com/mikael-s-persson/evdevpp

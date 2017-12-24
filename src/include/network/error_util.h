//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// error_util.h
//
// Identification: src/include/network/error_util.h
//
// Copyright (c) 2015-17, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <exception>
#include <string>
#include <utility>

namespace peloton {
namespace network {
/**
 * Used to signal that something has gone wrong when processing network connections.
 *
 * More details should be included in the error message and accessed with what().
 */
struct NetworkProcessException : public std::exception {
public:
  explicit NetworkProcessException(std::string error_msg): error_msg_(std::move(error_msg)) {}

  const char *what() const _NOEXCEPT override {
    return exception::what();
  }
private:
  std::string error_msg_;
};

class EventUtil {
private:
  template <typename T>
  static bool NotNull(T *ptr) {
    return ptr != nullptr;
  }

  static bool IsZero(int arg) {
    return arg == 0;
  }

  static bool NonNegative(int arg) {
    return arg >= 0;
  }

  template <typename T>
  static T Wrap(T value, bool(*check)(T), const char *error_msg) {
    if (!check(value)) throw NetworkProcessException(error_msg);
    return value;
  }

public:
  EventUtil() = delete;

  static struct event_base *EventBaseNew() {
    return Wrap(event_base_new(), NotNull<struct event_base>, "Can't allocate event base");
  }

  static int EventBaseLoopExit(struct event_base *base, const struct timeval *timeout) {
    return Wrap(event_base_loopexit(base, timeout), IsZero, "Error when exiting loop");
  }

  static int EventDel(struct event *event) {
    return Wrap(event_del(event), IsZero, "Error when deleting event");
  }

  static int EventAdd(struct event *event, const struct timeval *timeout) {
    return Wrap(event_add(event, timeout), IsZero, "Error when adding event");
  }

  static int EventAssign(struct event *event, struct event_base *base, int fd,
                         short flags, event_callback_fn callback, void *arg) {
    return Wrap(event_assign(event, base, fd, flags, callback, arg), IsZero, "Error when assigning event");
  }

  static int EventBaseDispatch(struct event_base *base) {
    return Wrap(event_base_dispatch(base), NonNegative, "Error in event base dispatch");
  }

};

}
}

// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_CHANNEL_H
#define MUDUO_NET_CHANNEL_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/Timestamp.h"

#include <functional>
#include <memory>

namespace muduo
{
namespace net
{

class EventLoop;

///
/// A selectable I/O channel.
///
/// This class doesn't own the file descriptor.
/// The file descriptor could be a socket,
/// an eventfd, a timerfd, or a signalfd
class Channel : noncopyable
{
 public:
  typedef std::function<void()> EventCallback;      //事件的回调处理
  typedef std::function<void(Timestamp)> ReadEventCallback;   //读时间的回调处理，多了一个时间戳

  Channel(EventLoop* loop, int fd);
  ~Channel();

  //事件处理函数
  void handleEvent(Timestamp receiveTime);

  //回调函数注册
  void setReadCallback(ReadEventCallback cb)
  { readCallback_ = std::move(cb); }
  void setWriteCallback(EventCallback cb)
  { writeCallback_ = std::move(cb); }
  void setCloseCallback(EventCallback cb)
  { closeCallback_ = std::move(cb); }
  void setErrorCallback(EventCallback cb)
  { errorCallback_ = std::move(cb); }

  /// Tie this channel to the owner object managed by shared_ptr,
  /// prevent the owner object being destroyed in handleEvent.
  /// 参数是 shared_ptr
  void tie(const std::shared_ptr<void>&);

  int fd() const { return fd_; }
  int events() const { return events_; }
  void set_revents(int revt) { revents_ = revt; } // used by pollers
  // int revents() const { return revents_; }
  bool isNoneEvent() const { return events_ == kNoneEvent; }

  //调整通道的事件
  //先调整后调用 update 函数
  void enableReading() { events_ |= kReadEvent; update(); }
  void disableReading() { events_ &= ~kReadEvent; update(); }
  void enableWriting() { events_ |= kWriteEvent; update(); }
  void disableWriting() { events_ &= ~kWriteEvent; update(); }
  void disableAll() { events_ = kNoneEvent; update(); }
  bool isWriting() const { return events_ & kWriteEvent; }
  bool isReading() const { return events_ & kReadEvent; }

  // for Poller
  int index() { return index_; }
  void set_index(int idx) { index_ = idx; }

  // for debug
  string reventsToString() const;
  string eventsToString() const;

  void doNotLogHup() { logHup_ = false; }

  EventLoop* ownerLoop() { return loop_; }

  //实际上调用的是 poller 的 removechannel 函数，将自己从 poll 数组中删除
  void remove();

 private:
  static string eventsToString(int fd, int ev);

  //实际上调用的是 poller 的 updatechannel 函数，将自己加入到 poll 数组中等待监听
  void update();

  //根据 channel 自身的 revent 类型调用对应的处理函数
  void handleEventWithGuard(Timestamp receiveTime);

  static const int kNoneEvent;      //三个事件类型
  static const int kReadEvent;
  static const int kWriteEvent;

  EventLoop* loop_;     //记录当前 channel 所属的 EvenLoop
  const int  fd_;       //套接字
  int        events_;   //关注事件
  int        revents_;  //实际返回的事件 it's the received event types of epoll or poll
  int        index_;    // used by Poller. 在 Poller 数组中的序号，如果是 -1  需要添加进数组中，在 epoll 中表示通道的状态
  bool       logHup_;

  std::weak_ptr<void> tie_;           //weak_ptr，弱引用
  bool tied_;
  bool eventHandling_;                //是否处于处理事件中
  bool addedToLoop_;                  //是否添加到 EvenLoop 中
  ReadEventCallback readCallback_;    //读事件的回调函数
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_CHANNEL_H

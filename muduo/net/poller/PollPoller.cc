// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/poller/PollPoller.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Types.h"
#include "muduo/net/Channel.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>

using namespace muduo;
using namespace muduo::net;

PollPoller::PollPoller(EventLoop* loop)
  : Poller(loop)
{
}

PollPoller::~PollPoller() = default;

//timeoutMs 是超时时间，activeChannels 用于返回活跃通道列表
//返回时间戳是调用 poll() 的时间
Timestamp PollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
  // XXX pollfds_ shouldn't change
  int numEvents = ::poll(&*pollfds_.begin(), pollfds_.size(), timeoutMs);
  int savedErrno = errno;
  Timestamp now(Timestamp::now());
  if (numEvents > 0)
  {
    LOG_TRACE << numEvents << " events happened";
    //将事件放到通道里面
    fillActiveChannels(numEvents, activeChannels);
  }
  //超时则输出 nothing happened
  else if (numEvents == 0)
  {
    LOG_TRACE << " nothing happened";
  }
  else
  {
    if (savedErrno != EINTR)
    {
      errno = savedErrno;
      LOG_SYSERR << "PollPoller::poll()";
    }
  }
  return now;
}

//将发生的事件存放到 activeChannels 中
void PollPoller::fillActiveChannels(int numEvents,
                                    ChannelList* activeChannels) const
{
  for (PollFdList::const_iterator pfd = pollfds_.begin();
      pfd != pollfds_.end() && numEvents > 0; ++pfd)
  {
    if (pfd->revents > 0)
    {
      --numEvents;
      //通过文件描述符找到该通道
      ChannelMap::const_iterator ch = channels_.find(pfd->fd);
      assert(ch != channels_.end());
      Channel* channel = ch->second;
      assert(channel->fd() == pfd->fd);
      //pollfd对象的 revent 改变了，但通道没有
      //所以要设置 channel 的事件
      channel->set_revents(pfd->revents);
      // pfd->revents = 0;
      activeChannels->push_back(channel);
    }
  }
}

//在 IO 线程中调用
//判断 channle 是注册还是更新
void PollPoller::updateChannel(Channel* channel)
{
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
  
  //注册事件时，由于我们不知道该事件在 pollfds_ 中位置
  //首先判断该文件描述符是否存在【理论上是不存在的】
  //其次将事件放入到数组的末尾
  //将通道放入 channels_ 中
  if (channel->index() < 0)
  {
    // a new one, add to pollfds_
    assert(channels_.find(channel->fd()) == channels_.end());
    struct pollfd pfd;
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
    pollfds_.push_back(pfd);
    int idx = static_cast<int>(pollfds_.size())-1;
    channel->set_index(idx);
    channels_[pfd.fd] = channel;
  }

  //更新通道
  else
  {
    // update existing one
    assert(channels_.find(channel->fd()) != channels_.end());
    assert(channels_[channel->fd()] == channel);
    int idx = channel->index();
    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
    //更新
    struct pollfd& pfd = pollfds_[idx];
    //有可能原本的事件是不活跃的
    assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd()-1);
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
    //如果一个通道不关注事件，我们就改变其套接字但不删除
    //这里只改变 pollfds_ 中的 fd
    if (channel->isNoneEvent())
    {
      // ignore this pollfd
      pfd.fd = -channel->fd()-1;    //其实这里可以写成 -1
    }
  }
}

//将事件移除
void PollPoller::removeChannel(Channel* channel)
{
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd();
  assert(channels_.find(channel->fd()) != channels_.end());
  assert(channels_[channel->fd()] == channel);
  assert(channel->isNoneEvent());
  int idx = channel->index();
  assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
  const struct pollfd& pfd = pollfds_[idx]; (void)pfd;
  //只有空闲的事件可以被删除
  assert(pfd.fd == -channel->fd()-1 && pfd.events == channel->events());
  //通过 key 将键值对从 map 中移除
  size_t n = channels_.erase(channel->fd());
  assert(n == 1); (void)n;
  //将通道从 pollfds_ 中移除
  //在最后一个
  if (implicit_cast<size_t>(idx) == pollfds_.size()-1)
  {
    pollfds_.pop_back();
  }
  //如果不在最后一个，我们将最后一个元素和他进行交换
  //并且更新 map 中的 key 以及 channel 本身的 idx
  else
  {
    int channelAtEnd = pollfds_.back().fd;
    iter_swap(pollfds_.begin()+idx, pollfds_.end()-1);
    if (channelAtEnd < 0)
    {
      channelAtEnd = -channelAtEnd-1;
    }
    //找到 channel，重新设置 idx
    channels_[channelAtEnd]->set_index(idx);
    //从尾部删除
    pollfds_.pop_back();
  }
}


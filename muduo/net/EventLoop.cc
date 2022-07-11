// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/EventLoop.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Mutex.h"
#include "muduo/net/Channel.h"
#include "muduo/net/Poller.h"
#include "muduo/net/SocketsOps.h"
#include "muduo/net/TimerQueue.h"

#include <algorithm>

#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

namespace
{
//线程局部变量，当前线程的 EvenLoop 对象
//一个线程只能有一个 EvenLoop 对象
__thread EventLoop* t_loopInThisThread = 0;   

const int kPollTimeMs = 10000;

int createEventfd()
{
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0)
  {
    LOG_SYSERR << "Failed in eventfd";
    abort();
  }
  return evtfd;
}

#pragma GCC diagnostic ignored "-Wold-style-cast"
class IgnoreSigPipe
{
 public:
  IgnoreSigPipe()
  {
    ::signal(SIGPIPE, SIG_IGN);
    // LOG_TRACE << "Ignore SIGPIPE";
  }
};
#pragma GCC diagnostic error "-Wold-style-cast"

IgnoreSigPipe initObj;
}  // namespace

EventLoop* EventLoop::getEventLoopOfCurrentThread()
{
  return t_loopInThisThread;
}

EventLoop::EventLoop()
  : looping_(false),
    quit_(false),
    eventHandling_(false),
    callingPendingFunctors_(false),
    iteration_(0),
    threadId_(CurrentThread::tid()),
    //指向默认的派生类对象，需要为派生类传入 EvenPoll 对象
    poller_(Poller::newDefaultPoller(this)),
    timerQueue_(new TimerQueue(this)),
    wakeupFd_(createEventfd()),
    wakeupChannel_(new Channel(this, wakeupFd_)),
    currentActiveChannel_(NULL)
{
  LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
  //判断当前线程是否已经存在 EvenLoop
  if (t_loopInThisThread)
  {
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  }
  else
  {
    t_loopInThisThread = this;
  }
  //wakeupfd 的回调函数设置
  wakeupChannel_->setReadCallback(
      std::bind(&EventLoop::handleRead, this));
  // we are always reading the wakeupfd
  wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
  LOG_DEBUG << "EventLoop " << this << " of thread " << threadId_
            << " destructs in thread " << CurrentThread::tid();
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
  t_loopInThisThread = NULL;
}

void EventLoop::loop()
{
  assert(!looping_);
  assertInLoopThread();
  looping_ = true;
  quit_ = false;  // FIXME: what if someone calls quit() before loop() ?
  LOG_TRACE << "EventLoop " << this << " start looping";

  while (!quit_)
  {
    activeChannels_.clear();
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
    ++iteration_;
    if (Logger::logLevel() <= Logger::TRACE)
    {
      printActiveChannels();
    }
    // TODO sort channel by priority
    //处理事件
    eventHandling_ = true;

    //遍历所以事件，调用 handleEvent 的处理函数
    for (Channel* channel : activeChannels_)
    {
      currentActiveChannel_ = channel;
      currentActiveChannel_->handleEvent(pollReturnTime_);
    }
    //事件处理结束，当前没有活跃事件
    currentActiveChannel_ = NULL;
    eventHandling_ = false;
    //让 IO 线程也能执行一些计算任务
    //这时候引用计数为 1
    doPendingFunctors();
  }

  LOG_TRACE << "EventLoop " << this << " stop looping";
  looping_ = false;
}

//可以跨线程使用
//如果不在同一个线程中使用，需要唤醒 poll
void EventLoop::quit()
{
  quit_ = true;
  // There is a chance that loop() just executes while(!quit_) and exits,
  // then EventLoop destructs, then we are accessing an invalid object.
  // Can be fixed using mutex_ in both places.
  if (!isInLoopThread())
  {
    wakeup();
  }
}

//在 IO 线程中执行某个回调函数，该函数可以跨线程调用
void EventLoop::runInLoop(Functor cb)
{
  //如果是在当前线程中调用，则同步调用 cb
  if (isInLoopThread())
  {
    cb();
  }
  //如果是在其他线程调用，则异步地将 cb 添加到队列中
  else
  {
    queueInLoop(std::move(cb));
  }
}

void EventLoop::queueInLoop(Functor cb)
{
  {
  MutexLockGuard lock(mutex_);
  pendingFunctors_.push_back(std::move(cb));
  }

  //如果调用 queueInLoop 的线程不是当前 IO 线程需要唤醒
  //或者调用 queueInLoop 的线程是当前线程，且正在执行 pendingFunc 
  //第二种情况发生在 doPendingFunctor() 函数中调用 queueInLoop，具体的例子是 TcpServer::removeConnection 函数
  if (!isInLoopThread() || callingPendingFunctors_)
  {
    wakeup();
  }
}

size_t EventLoop::queueSize() const
{
  MutexLockGuard lock(mutex_);
  return pendingFunctors_.size();
}

//默认是只执行一次
//指定时间调用 cb
TimerId EventLoop::runAt(Timestamp time, TimerCallback cb)
{
  return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

//在多少秒后调用 cb
TimerId EventLoop::runAfter(double delay, TimerCallback cb)
{
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, std::move(cb));
}

//每隔 intervals 调用一次 cb
TimerId EventLoop::runEvery(double interval, TimerCallback cb)
{
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(std::move(cb), time, interval);
}

void EventLoop::cancel(TimerId timerId)
{
  return timerQueue_->cancel(timerId);
}

void EventLoop::updateChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  if (eventHandling_)
  {
    assert(currentActiveChannel_ == channel ||
        std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
  }
  poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  return poller_->hasChannel(channel);
}

void EventLoop::abortNotInLoopThread()
{
  LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this
            << " was created in threadId_ = " << threadId_
            << ", current thread id = " <<  CurrentThread::tid();
}

void EventLoop::wakeup()
{
  //8个字节的缓冲区
  uint64_t one = 1;
  //wakeupfd 对应的文件描述符的可读事件被激活
  ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}

void EventLoop::handleRead()
{
  uint64_t one = 1;
  ssize_t n = sockets::read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
  }
}

void EventLoop::doPendingFunctors()
{
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;


  /*
    这么做是为了减少临界区的长度，避免阻塞其他线程
    也避免了死锁，functor 中也有可能调用 queueLoop()
  */
  {
  MutexLockGuard lock(mutex_);
  functors.swap(pendingFunctors_);
  }
  //此时 pendingFunctors_ 的任务都放到 functors 中

  for (const Functor& functor : functors)
  {
    functor();
  }
  callingPendingFunctors_ = false;
}

void EventLoop::printActiveChannels() const
{
  for (const Channel* channel : activeChannels_)
  {
    LOG_TRACE << "{" << channel->reventsToString() << "} ";
  }
}


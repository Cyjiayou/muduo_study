// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include "muduo/net/TimerQueue.h"

#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/Timer.h"
#include "muduo/net/TimerId.h"

#include <sys/timerfd.h>
#include <unistd.h>

namespace muduo
{
namespace net
{
namespace detail
{

int createTimerfd()
{
  int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
                                 TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerfd < 0)
  {
    LOG_SYSFATAL << "Failed in timerfd_create";
  }
  return timerfd;
}

//计算超时时刻与当前时间的时间差
struct timespec howMuchTimeFromNow(Timestamp when)
{
  int64_t microseconds = when.microSecondsSinceEpoch()
                         - Timestamp::now().microSecondsSinceEpoch();
  if (microseconds < 100)
  {
    microseconds = 100;
  }
  struct timespec ts;
  ts.tv_sec = static_cast<time_t>(
      microseconds / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec = static_cast<long>(
      (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
  return ts;
}

void readTimerfd(int timerfd, Timestamp now)
{
  uint64_t howmany;
  //调用 read 清除缓冲区的内容
  ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
  LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
  if (n != sizeof howmany)
  {
    LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
  }
}

//重置定时器的超时时间
void resetTimerfd(int timerfd, Timestamp expiration)
{
  // wake up loop by timerfd_settime()
  struct itimerspec newValue;
  struct itimerspec oldValue;
  memZero(&newValue, sizeof newValue);
  memZero(&oldValue, sizeof oldValue);
  //howMuchTimeFromNow 函数时间 timestamp 类型转化为 timespec 类型
  newValue.it_value = howMuchTimeFromNow(expiration);
  int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
  if (ret)
  {
    LOG_SYSERR << "timerfd_settime()";
  }
}

}  // namespace detail
}  // namespace net
}  // namespace muduo

using namespace muduo;
using namespace muduo::net;
using namespace muduo::net::detail;

//创建一个 TimerQueue 对象，就会将 TimeQueue 的套接字放到管道中
//并添加到 channels_ 中，等待事件发生
//对于 TimeQueue 的套接字，但超时时间到达后，可读时间将会被触发
TimerQueue::TimerQueue(EventLoop* loop)
  : loop_(loop),
    timerfd_(createTimerfd()),
    timerfdChannel_(loop, timerfd_),
    timers_(),
    callingExpiredTimers_(false)
{
  // 一旦可读事件产生就会调用 handlerRead 函数
  timerfdChannel_.setReadCallback(
      std::bind(&TimerQueue::handleRead, this));
  // we are always reading the timerfd, we disarm it with timerfd_settime.
  // 将通道加入 Poller 中
  timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue()
{
  timerfdChannel_.disableAll();
  timerfdChannel_.remove();
  ::close(timerfd_);
  // do not remove channel, since we're in EventLoop::dtor();
  for (const Entry& timer : timers_)
  {
    delete timer.second;
  }
}

//增加一个定时器
TimerId TimerQueue::addTimer(TimerCallback cb,
                             Timestamp when,
                             double interval)
{
  Timer* timer = new Timer(std::move(cb), when, interval);
  
  //可以跨线程的代码，在其他线程调用时，会将 addTimerInLoop 函数放到 pendingFunctors_ 中等待线程处理
  loop_->runInLoop(
      std::bind(&TimerQueue::addTimerInLoop, this, timer));
  //不能跨线程的代码
  //addTimerInLoop(timer);
  return TimerId(timer, timer->sequence());
}


//取消一个定时器
void TimerQueue::cancel(TimerId timerId)
{
  //可以跨线程的代码
  loop_->runInLoop(
      std::bind(&TimerQueue::cancelInLoop, this, timerId));
  //不能跨线程的代码
  //cancelInLoop(timerId);
}

void TimerQueue::addTimerInLoop(Timer* timer)
{
  loop_->assertInLoopThread();

  //插入一个定时器，有可能会使得最早到期的定时器发生变化
  bool earliestChanged = insert(timer);

  if (earliestChanged)
  {
    //重置定时器的超时时刻
    resetTimerfd(timerfd_, timer->expiration());
  }
}

void TimerQueue::cancelInLoop(TimerId timerId)
{
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  //通过参数 timerId 构造 ActiveTimer 对象
  ActiveTimer timer(timerId.timer_, timerId.sequence_);
  ActiveTimerSet::iterator it = activeTimers_.find(timer);
  //如果找到了该定时器
  if (it != activeTimers_.end())
  {
    //从 timers_ 中erase 掉
    size_t n = timers_.erase(Entry(it->first->expiration(), it->first));
    assert(n == 1); (void)n;
    delete it->first; // FIXME: no delete please
    //从 activeTimers_ 中移除
    activeTimers_.erase(it);    
  }
  //如果不在定时器当中，可能因为他已经到期，被我们删除了
  //加入到 cancelingTimers_ 中，不重启该定时器  
  else if (callingExpiredTimers_)
  {
    cancelingTimers_.insert(timer);
  }
  //否则就无法取消
  assert(timers_.size() == activeTimers_.size());
}

void TimerQueue::handleRead()
{
  loop_->assertInLoopThread();
  Timestamp now(Timestamp::now());
  readTimerfd(timerfd_, now);     //清除该事件，避免一直触发

  //获取该时刻之前的所有定时器列表（即超时定时器列表）
  //可能有多个定时器的超时时间是相同的
  std::vector<Entry> expired = getExpired(now);

  callingExpiredTimers_ = true;
  cancelingTimers_.clear();
  // safe to callback outside critical section
  //调用定时器的 run 函数
  for (const Entry& it : expired)
  {
    it.second->run();
  }
  callingExpiredTimers_ = false;

  //如果不是一次性定时器，重启
  reset(expired, now);
}

//rvo 优化，返回时不会调用拷贝构造函数
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
  assert(timers_.size() == activeTimers_.size());
  std::vector<Entry> expired;

  //这里的 UINTPTR_MAX 表示位 Timer 的地址，这里最大
  Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
  //lower_bound 返回第一个值大于等于 sentry 的元素的 iterator
  TimerList::iterator end = timers_.lower_bound(sentry);
  //为什么 end->first 的值会大于 now ?
  //因为 Entry 是一个 pair 结构，重载了比较运算符，等号相求两个值都想等
  //sentry 的第二个参数是一个地址，值是最大的
  //所以 end 的值必然是大于超时时间的一个元素
  assert(end == timers_.end() || now < end->first);
  //将到期的定时器放入到 expired
  //back_inserter 是插入迭代器
  std::copy(timers_.begin(), end, back_inserter(expired));
  //tiemrs_ 移除定时器
  timers_.erase(timers_.begin(), end);

  //activeTimers_ 移除定时器
  for (const Entry& it : expired)
  {
    ActiveTimer timer(it.second, it.second->sequence());
    //根据对象移除值
    size_t n = activeTimers_.erase(timer);
    assert(n == 1); (void)n;
  }

  assert(timers_.size() == activeTimers_.size());
  return expired;
}

void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
  Timestamp nextExpire;

  for (const Entry& it : expired)
  {
    ActiveTimer timer(it.second, it.second->sequence());
    //如果是重复定时器，并且是未取消的定时器，则重启该定时器
    if (it.second->repeat()
        && cancelingTimers_.find(timer) == cancelingTimers_.end())
    {
      //重新计算下一个超时时刻
      it.second->restart(now);
      
      //expired 已经从定时器列表中移除了，如果是重复的则加入到列表中
      insert(it.second);
    }
    else
    {
      delete it.second;
    }
  }

  if (!timers_.empty())
  {
    nextExpire = timers_.begin()->second->expiration();
  }

  if (nextExpire.valid())
  {
    //重新设置定时器的超时时间
    resetTimerfd(timerfd_, nextExpire);
  }
}

bool TimerQueue::insert(Timer* timer)
{
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  //最早到期时间是否改变，一开始没有改变
  bool earliestChanged = false;
  Timestamp when = timer->expiration();
  TimerList::iterator it = timers_.begin();
  // 如果 timers_ 为空或者 timers_ 中最小的超时时间比新加的超时时间大
  if (it == timers_.end() || when < it->first)
  {
    earliestChanged = true;
  }
  //将 timer 插入的 timers_ 中，里面存放的都是对象，所以调用类型构造函数
  {
    std::pair<TimerList::iterator, bool> result
      = timers_.insert(Entry(when, timer));
    assert(result.second); (void)result;
  }
  //将 timer 插入到 activeTimers_ 中
  {
    std::pair<ActiveTimerSet::iterator, bool> result
      = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
    assert(result.second); (void)result;
  }

  assert(timers_.size() == activeTimers_.size());
  return earliestChanged;
}


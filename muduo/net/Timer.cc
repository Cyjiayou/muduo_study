// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/Timer.h"

using namespace muduo;
using namespace muduo::net;

AtomicInt64 Timer::s_numCreated_;   //初始值为 0，调用构造函数

void Timer::restart(Timestamp now)
{
  //如果是重复定时器，重新计算下一个超时时刻
  //如果不是，下一个超时时刻等于非法时间
  if (repeat_)
  {
    //当前时间加上时间间隔等于下一个超时时刻
    //timestamp 中的函数
    expiration_ = addTime(now, interval_);
  }
  else
  {
    expiration_ = Timestamp::invalid();
  }
}

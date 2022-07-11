// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/Acceptor.h"

#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/SocketsOps.h"

#include <errno.h>
#include <fcntl.h>
//#include <sys/types.h>
//#include <sys/stat.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport)
  : loop_(loop),
    acceptSocket_(sockets::createNonblockingOrDie(listenAddr.family())),
    acceptChannel_(loop, acceptSocket_.fd()),
    listening_(false),
    idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC))
{
  assert(idleFd_ >= 0);
  //设置地址重复利用
  acceptSocket_.setReuseAddr(true);
  acceptSocket_.setReusePort(reuseport);
  //绑定地址
  acceptSocket_.bindAddress(listenAddr);
  //为 channel 设置一个 handlerRead 回调函数
  acceptChannel_.setReadCallback(
      std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
  acceptChannel_.disableAll();
  acceptChannel_.remove();
  ::close(idleFd_);
}

void Acceptor::listen()
{
  loop_->assertInLoopThread();
  listening_ = true;
  //监听套接字
  acceptSocket_.listen();
  //关注套接字的可读事件
  acceptChannel_.enableReading();
}


//用于处理客户端连接
void Acceptor::handleRead()
{
  loop_->assertInLoopThread();
  //准备一个对等方地址
  InetAddress peerAddr;
  //FIXME loop until no more
  //接收客户端连接
  int connfd = acceptSocket_.accept(&peerAddr);
  if (connfd >= 0)
  {
    // string hostport = peerAddr.toIpPort();
    // LOG_TRACE << "Accepts of " << hostport;
    if (newConnectionCallback_)
    {
      //回调客户端函数
      //主要是显示客户端连接和创建一个客户端连接对象
      newConnectionCallback_(connfd, peerAddr);
    }
    else
    {
      sockets::close(connfd);
    }
  }
  //失败的处理
  else
  {
    LOG_SYSERR << "in Acceptor::handleRead";
    //如果客户端套接字太多了，此时我们需要进行处理
    //对于电平触发，如果不读取缓冲区的内容，将一直处于高电平就会 busyloop
    //对于边沿触发，下次事件到来时，因为电平一直为高，不会触发
    if (errno == EMFILE)
    {
      ::close(idleFd_);
      idleFd_ = ::accept(acceptSocket_.fd(), NULL, NULL);
      ::close(idleFd_);
      idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    }
  }
}


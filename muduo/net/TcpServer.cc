// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/TcpServer.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Acceptor.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThreadPool.h"
#include "muduo/net/SocketsOps.h"

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;

TcpServer::TcpServer(EventLoop* loop,
                     const InetAddress& listenAddr,
                     const string& nameArg,
                     Option option)
  : loop_(CHECK_NOTNULL(loop)),   //检查 loop 指针是否为 NULL
    ipPort_(listenAddr.toIpPort()),
    name_(nameArg),
    acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
    threadPool_(new EventLoopThreadPool(loop, name_)),
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),
    nextConnId_(1)
{
  //设置 newConnection 的回调函数，因为有两个参数，所以有两个占位符
  //第一个参数是客户端套接字，第二个参数是客户端地址
  acceptor_->setNewConnectionCallback(
      std::bind(&TcpServer::newConnection, this, _1, _2));
}

TcpServer::~TcpServer()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "TcpServer::~TcpServer [" << name_ << "] destructing";

  for (auto& item : connections_)
  {
    TcpConnectionPtr conn(item.second);
    item.second.reset();
    conn->getLoop()->runInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));
  }
}

void TcpServer::setThreadNum(int numThreads)
{
  assert(0 <= numThreads);
  threadPool_->setThreadNum(numThreads);
}


//该函数多次调用是无害的，第一次调用 started_ 就不为 0 了，就不会调用监听函数了
//该函数可以跨线程调用
//生成 Acceptor 对象时，只绑定了地址，但还没有调用 listen 函数
void TcpServer::start()
{
  //started_ 一开始值为 0，getAndSet 想取值后设置值
  if (started_.getAndSet(1) == 0)
  {
    //启动线程池
    threadPool_->start(threadInitCallback_);

    assert(!acceptor_->listening());
    //使用了 runInLoop 函数，跨线程
    //执行 acceptor_ 指针对应的 listen 对象
    loop_->runInLoop(
        std::bind(&Acceptor::listen, get_pointer(acceptor_)));
  }
}


//客户端连接后的回调函数
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
  //断言在 IO 线程中
  loop_->assertInLoopThread();
  //按照轮叫的方式选择一个 EvenLoop
  EventLoop* ioLoop = threadPool_->getNextLoop();
  char buf[64];
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;
  //连接的名称是 服务器名称 + 服务器端口号 + 连接 ID
  string connName = name_ + buf;

  LOG_INFO << "TcpServer::newConnection [" << name_
           << "] - new connection [" << connName
           << "] from " << peerAddr.toIpPort();

  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary
  // 创建一个 shared_ptr 对象
  TcpConnectionPtr conn(new TcpConnection(ioLoop,
                                          connName,
                                          sockfd,
                                          localAddr,
                                          peerAddr));
  //此时引用计数应该是 1
  LOG_TRACE  << "[1] usercount = " << conn.use_count();
  //加入到 connection_ 中，引用计数加 1
  connections_[connName] = conn;
  //引用计数是 2
  LOG_TRACE  << "[2] usercount = " << conn.use_count();
  //将 TcpServer 中的回调函数设置到 TcpConnection 中
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe
  
  //让事件所连接的 IO 线程调用 connectEstablished 函数
  ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
  
  //假设在同一线程下，此时引用计数是 2
  LOG_TRACE  << "[4] usercount = " << conn.use_count();
  
  //当这个函数执行完毕后， conn 被释放，此时只有 connections_ 有 TcpConnection 对象
  //引用计数为 1，这时候客户端断开连接，触发读事件
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
  // FIXME: unsafe
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
           << "] - connection " << conn->name();
  
  
  //这里还是 3
  LOG_TRACE  << "[8] usercount = " << conn.use_count();
  
  //将对象从列表中移除
  size_t n = connections_.erase(conn->name());
  //释放了一个对象，引用计数变为 2
  LOG_TRACE  << "[9] usercount = " << conn.use_count();
  (void)n;
  assert(n == 1);
  EventLoop* ioLoop = conn->getLoop();

  //此时还处于 handleEvent() 中，而 connectDestroyed 是在 loop() 最后处理的
  //这里将 conn 传入函数中，引用计数会加 1
  ioLoop->queueInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));

  //引用计数为 3
  LOG_TRACE  << "[10] usercount = " << conn.use_count();

}


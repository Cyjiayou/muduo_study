// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/TcpConnection.h"

#include "muduo/base/Logging.h"
#include "muduo/base/WeakCallback.h"
#include "muduo/net/Channel.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/Socket.h"
#include "muduo/net/SocketsOps.h"

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

void muduo::net::defaultConnectionCallback(const TcpConnectionPtr& conn)
{
  LOG_TRACE << conn->localAddress().toIpPort() << " -> "
            << conn->peerAddress().toIpPort() << " is "
            << (conn->connected() ? "UP" : "DOWN");
  // do not call conn->forceClose(), because some users want to register message callback only.
}

void muduo::net::defaultMessageCallback(const TcpConnectionPtr&,
                                        Buffer* buf,
                                        Timestamp)
{
  buf->retrieveAll();
}

TcpConnection::TcpConnection(EventLoop* loop,
                             const string& nameArg,
                             int sockfd,
                             const InetAddress& localAddr,
                             const InetAddress& peerAddr)
  : loop_(CHECK_NOTNULL(loop)),
    name_(nameArg),
    state_(kConnecting),
    reading_(true),
    socket_(new Socket(sockfd)),
    channel_(new Channel(loop, sockfd)),
    localAddr_(localAddr),
    peerAddr_(peerAddr),
    highWaterMark_(64*1024*1024)
{
  //可读事件到来，回调 handleRead
  channel_->setReadCallback(
      std::bind(&TcpConnection::handleRead, this, _1));
  channel_->setWriteCallback(
      std::bind(&TcpConnection::handleWrite, this));
  //连接关闭，回调 handleClose
  channel_->setCloseCallback(
      std::bind(&TcpConnection::handleClose, this));
  //发生错误，回调 handleError
  channel_->setErrorCallback(
      std::bind(&TcpConnection::handleError, this));
  LOG_DEBUG << "TcpConnection::ctor[" <<  name_ << "] at " << this
            << " fd=" << sockfd;
  socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
  LOG_DEBUG << "TcpConnection::dtor[" <<  name_ << "] at " << this
            << " fd=" << channel_->fd()
            << " state=" << stateToString();
  assert(state_ == kDisconnected);
}

bool TcpConnection::getTcpInfo(struct tcp_info* tcpi) const
{
  return socket_->getTcpInfo(tcpi);
}

string TcpConnection::getTcpInfoString() const
{
  char buf[1024];
  buf[0] = '\0';
  socket_->getTcpInfoString(buf, sizeof buf);
  return buf;
}


//线程安全，可以跨线程调用
void TcpConnection::send(const void* data, int len)
{
  send(StringPiece(static_cast<const char*>(data), len));
}

void TcpConnection::send(const StringPiece& message)
{
  if (state_ == kConnected)
  {
    //如果在 IO 线程直接调用 sendInLoop
    if (loop_->isInLoopThread())
    {
      sendInLoop(message);
    }
    //否则就到 IO 线程中的 dopending 中调用，调用的函数都是一样的
    else
    {
      //这里的 fp 就是 sendInLoop()
      void (TcpConnection::*fp)(const StringPiece& message) = &TcpConnection::sendInLoop;
      loop_->runInLoop(
          std::bind(fp,
                    this,     // FIXME
                    message.as_string()));
                    //std::forward<string>(message)));
    }
  }
}

// FIXME efficiency!!!
// 将缓冲区中的内容发送，可以发现 buffer 并没有加锁，即 buffer 本身不是线程安全的
// 但 buffer 实际上就是属于一个线程的，所以不需要加锁
void TcpConnection::send(Buffer* buf)
{
  if (state_ == kConnected)
  {
    if (loop_->isInLoopThread())
    {
      sendInLoop(buf->peek(), buf->readableBytes());
      buf->retrieveAll();
    }
    else
    {
      void (TcpConnection::*fp)(const StringPiece& message) = &TcpConnection::sendInLoop;
      loop_->runInLoop(
          std::bind(fp,
                    this,     // FIXME
                    buf->retrieveAllAsString()));
                    //std::forward<string>(message)));
    }
  }
}

//只能在 IO 线程中调用
void TcpConnection::sendInLoop(const StringPiece& message)
{
  sendInLoop(message.data(), message.size());
}

void TcpConnection::sendInLoop(const void* data, size_t len)
{
  loop_->assertInLoopThread();
  ssize_t nwrote = 0;
  size_t remaining = len;
  bool faultError = false;
  if (state_ == kDisconnected)
  {
    LOG_WARN << "disconnected, give up writing";
    return;
  }
  // if no thing in output queue, try writing directly
  // 通道没有关注可写事件并且发送缓冲区没有数据，直接 write
  if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
  {
    nwrote = sockets::write(channel_->fd(), data, len);
    if (nwrote >= 0)
    {
      remaining = len - nwrote;
      //如果写完了，则回调 writeCompleteCallback_
      if (remaining == 0 && writeCompleteCallback_)
      {
        loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
      }
    }
    //小于 0 表示出错
    else // nwrote < 0
    {
      nwrote = 0;
      if (errno != EWOULDBLOCK)
      {
        LOG_SYSERR << "TcpConnection::sendInLoop";
        if (errno == EPIPE || errno == ECONNRESET) // FIXME: any others?
        {
          faultError = true;
        }
      }
    }
  }

  assert(remaining <= len);
  //如果没有错误，并且还剩余一些数据没有发送
  //说明内核发送缓冲区满了，要将未写完的数据添加到 output buffer 中
  if (!faultError && remaining > 0)
  {
    LOG_TRACE << "I am going to write more data";
    size_t oldLen = outputBuffer_.readableBytes();
    //如果超过高水位标，则回调 highWaterMarkCallback_
    if (oldLen + remaining >= highWaterMark_
        && oldLen < highWaterMark_
        && highWaterMarkCallback_)
    {
      loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
    }
    outputBuffer_.append(static_cast<const char*>(data)+nwrote, remaining);
    //缓冲区有数据了，所以我们要关注写事件
    if (!channel_->isWriting())
    {
      channel_->enableWriting();
    }
  }
}

//可以跨线程调用
void TcpConnection::shutdown()
{
  // FIXME: use compare and swap
  // 如果还有数据没有发送完，则只是将连接状态更改为 disConnecting
  // 并没有关闭连接
  // 所以如果 outbuffer 写完了就应该判断当前状态是不是 disConnecting
  // 如果是的话就关闭 shutdownInLoop
  // 此时相当于服务器端主动断开与客户端的连接
  // 这意味着客户端的 read() 返回为 0，从而 closs(conn)
  // 则服务器端将接收到 POLLHUP 和 POLLIN
  if (state_ == kConnected)
  {
    setState(kDisconnecting);
    // FIXME: shared_from_this()?
    loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
  }
}

void TcpConnection::shutdownInLoop()
{
  loop_->assertInLoopThread();

  //如果 channel_ 不处于 isWriting 的状态
  //当 channel 不关注 pollout 事件则不处于该状态
  if (!channel_->isWriting())
  {
    // we are not writing
    socket_->shutdownWrite();
  }
}

// void TcpConnection::shutdownAndForceCloseAfter(double seconds)
// {
//   // FIXME: use compare and swap
//   if (state_ == kConnected)
//   {
//     setState(kDisconnecting);
//     loop_->runInLoop(std::bind(&TcpConnection::shutdownAndForceCloseInLoop, this, seconds));
//   }
// }

// void TcpConnection::shutdownAndForceCloseInLoop(double seconds)
// {
//   loop_->assertInLoopThread();
//   if (!channel_->isWriting())
//   {
//     // we are not writing
//     socket_->shutdownWrite();
//   }
//   loop_->runAfter(
//       seconds,
//       makeWeakCallback(shared_from_this(),
//                        &TcpConnection::forceCloseInLoop));
// }

void TcpConnection::forceClose()
{
  // FIXME: use compare and swap
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    setState(kDisconnecting);
    loop_->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
  }
}

void TcpConnection::forceCloseWithDelay(double seconds)
{
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    setState(kDisconnecting);
    loop_->runAfter(
        seconds,
        makeWeakCallback(shared_from_this(),
                         &TcpConnection::forceClose));  // not forceCloseInLoop to avoid race condition
  }
}

void TcpConnection::forceCloseInLoop()
{
  loop_->assertInLoopThread();
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    // as if we received 0 byte in handleRead();
    handleClose();
  }
}

const char* TcpConnection::stateToString() const
{
  switch (state_)
  {
    case kDisconnected:
      return "kDisconnected";
    case kConnecting:
      return "kConnecting";
    case kConnected:
      return "kConnected";
    case kDisconnecting:
      return "kDisconnecting";
    default:
      return "unknown state";
  }
}

void TcpConnection::setTcpNoDelay(bool on)
{
  socket_->setTcpNoDelay(on);
}

void TcpConnection::startRead()
{
  loop_->runInLoop(std::bind(&TcpConnection::startReadInLoop, this));
}

void TcpConnection::startReadInLoop()
{
  loop_->assertInLoopThread();
  if (!reading_ || !channel_->isReading())
  {
    channel_->enableReading();
    reading_ = true;
  }
}

void TcpConnection::stopRead()
{
  loop_->runInLoop(std::bind(&TcpConnection::stopReadInLoop, this));
}

void TcpConnection::stopReadInLoop()
{
  loop_->assertInLoopThread();
  if (reading_ || channel_->isReading())
  {
    channel_->disableReading();
    reading_ = false;
  }
}

void TcpConnection::connectEstablished()
{
  loop_->assertInLoopThread();
  assert(state_ == kConnecting);
  setState( kConnected);
  

  // 调用 tie 时又创建了一个 shared_ptr 对象，所以有变为 3
  // tie 中是 weak_ptr，不会增加计数
  // 调用完以后临时对象又被销毁，变为 2
  channel_->tie(shared_from_this());
  //关注 TcpConnect 的可读事件
  channel_->enableReading();

  //连接建立后，客户端调用它的连接回调函数
  // 一开时又变为 3，调用完后变为 2
  connectionCallback_(shared_from_this());

  //这里引用计数是 2
  LOG_TRACE  << "[3] usercount = " << conn.use_count();
}

void TcpConnection::connectDestroyed()
{
  loop_->assertInLoopThread();
  //这里不会调用，因为 handleClose() 已经处理过了
  if (state_ == kConnected)
  {
    setState(kDisconnected);
    channel_->disableAll();

    connectionCallback_(shared_from_this());
  }
  //将channel 从 poll 中移除
  channel_->remove();
  //这个函数默认传递 this 指针，但我们这里传递的是一个 share_ptr 对象
  //运行完毕之后引用对象被销毁，引用计数减 1，引用计数为 0，TcpConnection 对象被释放
}

//接受客户端的信息
void TcpConnection::handleRead(Timestamp receiveTime)
{
  
  loop_->assertInLoopThread();
  int savedErrno = 0;
  ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
  if (n > 0)
  {
    messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
  }
  else if (n == 0)
  {
    handleClose();
  }
  else
  {
    errno = savedErrno;
    LOG_SYSERR << "TcpConnection::handleRead";
    handleError();
  }
  
  /*
  loop_->assertInLoopThread();
  int saveErrno = 0;
  char buf[65536];
  ssize_t n = ::read(channel->fd(), buf, sizeof buf);
  if (n > 0) {
    messageCallback(shared_from_this(), buf, n);
  }
  //处理连接断开
  else if (n == 0) {
    handleClose();
    //此时引用计数为 2
  }
  //处理错误情况 
  else {
    errno = saveErrno;
    LOG_SYSERR << "TcpConnection::handleRead";
    handleError();
 }
 */
}


//内核缓冲区有空间了，回调该函数
void TcpConnection::handleWrite()
{
  loop_->assertInLoopThread();
  if (channel_->isWriting())
  {
    //不一定会将缓冲区中的内容全部写完
    ssize_t n = sockets::write(channel_->fd(),
                               outputBuffer_.peek(),
                               outputBuffer_.readableBytes());
    if (n > 0)
    {
      outputBuffer_.retrieve(n);
      //应用缓冲区清空
      if (outputBuffer_.readableBytes() == 0)
      {
        //不关注 pollout 事件避免 busyLoop
        channel_->disableWriting();
        //调用 writeCompleteCallback_
        if (writeCompleteCallback_)
        {
          loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
        }
        //发送缓冲区已清空并且连接状态是 KDisconning，要关闭连接
        if (state_ == kDisconnecting)
        {
          shutdownInLoop();
        }
      }
    }
    else
    {
      LOG_SYSERR << "TcpConnection::handleWrite";
      // if (state_ == kDisconnecting)
      // {
      //   shutdownInLoop();
      // }
    }
  }
  else
  {
    LOG_TRACE << "Connection fd = " << channel_->fd()
              << " is down, no more writing";
  }
}

//连接断开的处理情况
void TcpConnection::handleClose()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "fd = " << channel_->fd() << " state = " << stateToString();
  assert(state_ == kConnected || state_ == kDisconnecting);
  // we don't close fd, leave it to dtor, so we can find leaks easily.
  setState(kDisconnected);

  //这里并没有调用 channel_ 的 remove()，因为当前还处于 channel_ 的 handleEvent() 函数中
  channel_->disableAll();
 
  TcpConnectionPtr guardThis(shared_from_this());
  connectionCallback_(guardThis);
  
  //这里引用计数仍然为 3
  LOG_TRACE  << "[6] usercount = " << guardThis.use_count();
  // must be the last line
  //这里调用的 TcpServer::removeConnection 函数
  closeCallback_(guardThis);
  //引用计数还是 3
  LOG_TRACE  << "[11] usercount = " << guardThis.use_count();
  //退出后 guardThis 临时对象被释放，引用计数减一，引用计数为 2
}

void TcpConnection::handleError()
{
  int err = sockets::getSocketError(channel_->fd());
  LOG_ERROR << "TcpConnection::handleError [" << name_
            << "] - SO_ERROR = " << err << " " << strerror_tl(err);
}


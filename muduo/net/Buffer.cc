// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/Buffer.h"

#include "muduo/net/SocketsOps.h"

#include <errno.h>
#include <sys/uio.h>

using namespace muduo;
using namespace muduo::net;

const char Buffer::kCRLF[] = "\r\n";

const size_t Buffer::kCheapPrepend;
const size_t Buffer::kInitialSize;

// 结合栈上的空间，避免内存使用过大，提高内存使用率
// 如果有 5K 个连接，每个连接就分配 64 K + 64 K 的缓冲区的话，将占用640 M内存
// 而大多数时候，这些缓冲区的使用率很低
ssize_t Buffer::readFd(int fd, int* savedErrno)
{
  // 尽量一次性将所有数据读完
  // saved an ioctl()/FIONREAD call to tell how much to read
  // 栈上缓冲区的空间够大，可以节省一次 loctl() 系统调用（获取有多少可读数据）
  char extrabuf[65536];
  
  //准备两个缓冲区
  struct iovec vec[2];
  const size_t writable = writableBytes();
  //第一块缓冲区指向可写入的位置
  vec[0].iov_base = begin()+writerIndex_;
  vec[0].iov_len = writable;
  //第二块缓冲区指向栈上内存
  vec[1].iov_base = extrabuf;
  vec[1].iov_len = sizeof extrabuf;
  // when there is enough space in this buffer, don't read into extrabuf.
  // when extrabuf is used, we read 128k-1 bytes at most.
  const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
  
  //读取数据
  const ssize_t n = sockets::readv(fd, vec, iovcnt);
  if (n < 0)
  {
    *savedErrno = errno;
  }
  
  //如果第一块缓冲区足够容纳
  else if (implicit_cast<size_t>(n) <= writable)
  {
    writerIndex_ += n;
  }

  //如果当前缓冲区不够容纳，那么部分数据就被添加到栈上空间
  //我们就将栈上空间添加到都一块缓冲区中
  else
  {
    writerIndex_ = buffer_.size();
    append(extrabuf, n - writable);
  }
  // if (n == writable + sizeof extrabuf)
  // {
  //   goto line_30;
  // }
  return n;
}


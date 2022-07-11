// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/http/HttpResponse.h"
#include "muduo/net/Buffer.h"

#include <stdio.h>

using namespace muduo;
using namespace muduo::net;

//将应答信息保存到 buffer 中
void HttpResponse::appendToBuffer(Buffer* output) const
{
  char buf[32];
  //添加响应头
  snprintf(buf, sizeof buf, "HTTP/1.1 %d ", statusCode_);
  output->append(buf);
  output->append(statusMessage_);
  output->append("\r\n");

  //短连接不需要告诉客户端长度，因为不存在粘包问题
  if (closeConnection_)
  {
    output->append("Connection: close\r\n");
  }
  else
  {
    //实体长度
    snprintf(buf, sizeof buf, "Content-Length: %zd\r\n", body_.size());
    output->append(buf);
    output->append("Connection: Keep-Alive\r\n");
  }

  //header 列表添加进去
  for (const auto& header : headers_)
  {
    output->append(header.first);
    output->append(": ");
    output->append(header.second);
    output->append("\r\n");
  }

  //header 和 body 之间应该存在一个 \r\n
  output->append("\r\n");

  //添加 body
  output->append(body_);
}

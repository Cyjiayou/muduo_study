// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/Buffer.h"
#include "muduo/net/http/HttpContext.h"

using namespace muduo;
using namespace muduo::net;

bool HttpContext::processRequestLine(const char* begin, const char* end)
{
  bool succeed = false;
  const char* start = begin;
  //查找第一个空格位置
  const char* space = std::find(start, end, ' ');
  //存在空格并且成功设置 request_ 的状态
  if (space != end && request_.setMethod(start, space))
  {
    start = space+1;
    space = std::find(start, end, ' ');
    if (space != end)
    {
      //解析 url
      const char* question = std::find(start, space, '?');
      if (question != space)
      {
        request_.setPath(start, question);
        request_.setQuery(question, space);
      }
      else
      {
        request_.setPath(start, space);
      }
      start = space+1;
      //确定版本
      succeed = end-start == 8 && std::equal(start, end-1, "HTTP/1.");
      if (succeed)
      {
        if (*(end-1) == '1')
        {
          request_.setVersion(HttpRequest::kHttp11);
        }
        else if (*(end-1) == '0')
        {
          request_.setVersion(HttpRequest::kHttp10);
        }
        else
        {
          succeed = false;
        }
      }
    }
  }
  return succeed;
}

// return false if any error
// 状态机
bool HttpContext::parseRequest(Buffer* buf, Timestamp receiveTime)
{
  bool ok = true;
  bool hasMore = true;
  while (hasMore)
  {
    //处于解析请求行
    if (state_ == kExpectRequestLine)
    {
      //缓冲区中查找第一个 \r\n
      const char* crlf = buf->findCRLF();


      //如果找到
      if (crlf)
      {
        //处理请求行
        ok = processRequestLine(buf->peek(), crlf);
        if (ok)
        {
          request_.setReceiveTime(receiveTime);
          //从 buff 中取走包括 \r\n
          buf->retrieveUntil(crlf + 2);
          state_ = kExpectHeaders;
        }
        else
        {
          hasMore = false;
        }
      }
      else
      {
        hasMore = false;
      }
    }
    else if (state_ == kExpectHeaders)
    {
      //添加头部信息
      const char* crlf = buf->findCRLF();
      if (crlf)
      {
        const char* colon = std::find(buf->peek(), crlf, ':');
        if (colon != crlf)
        {
          request_.addHeader(buf->peek(), colon, crlf);
        }
        //空行
        else
        {
          // empty line, end of header
          // FIXME:
          state_ = kGotAll;
          hasMore = false;
        }
        //取走头部信息
        buf->retrieveUntil(crlf + 2);
      }
      else
      {
        hasMore = false;
      }
    }
    else if (state_ == kExpectBody)
    {
      // FIXME:
    }
  }
  return ok;
}

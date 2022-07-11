// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_HTTP_HTTPREQUEST_H
#define MUDUO_NET_HTTP_HTTPREQUEST_H

#include "muduo/base/copyable.h"
#include "muduo/base/Timestamp.h"
#include "muduo/base/Types.h"

#include <map>
#include <assert.h>
#include <stdio.h>

namespace muduo
{
namespace net
{


//用来保存请求报文的信息
class HttpRequest : public muduo::copyable
{
 public:
  enum Method
  {
    kInvalid, kGet, kPost, kHead, kPut, kDelete
  };
  enum Version
  {
    kUnknown, kHttp10, kHttp11
  };

  HttpRequest()
    : method_(kInvalid),
      version_(kUnknown)
  {
  }

  void setVersion(Version v)
  {
    version_ = v;
  }

  Version getVersion() const
  { return version_; }

  //设置方法，根据传入的参数
  bool setMethod(const char* start, const char* end)
  {
    assert(method_ == kInvalid);
    string m(start, end);
    if (m == "GET")
    {
      method_ = kGet;
    }
    else if (m == "POST")
    {
      method_ = kPost;
    }
    else if (m == "HEAD")
    {
      method_ = kHead;
    }
    else if (m == "PUT")
    {
      method_ = kPut;
    }
    else if (m == "DELETE")
    {
      method_ = kDelete;
    }
    else
    {
      method_ = kInvalid;
    }
    return method_ != kInvalid;
  }

  Method method() const
  { return method_; }

  //获取方法的字符串表达式
  const char* methodString() const
  {
    const char* result = "UNKNOWN";
    switch(method_)
    {
      case kGet:
        result = "GET";
        break;
      case kPost:
        result = "POST";
        break;
      case kHead:
        result = "HEAD";
        break;
      case kPut:
        result = "PUT";
        break;
      case kDelete:
        result = "DELETE";
        break;
      default:
        break;
    }
    return result;
  }


  //设置路径
  void setPath(const char* start, const char* end)
  {
    path_.assign(start, end);
  }

  const string& path() const
  { return path_; }


  //设置查询
  void setQuery(const char* start, const char* end)
  {
    query_.assign(start, end);
  }

  const string& query() const
  { return query_; }

  void setReceiveTime(Timestamp t)
  { receiveTime_ = t; }

  Timestamp receiveTime() const
  { return receiveTime_; }


  //添加头部，colon 表示 : 所在的位置
  void addHeader(const char* start, const char* colon, const char* end)
  {
    //这里不包括 : 
    string field(start, colon);
    ++colon;

    // 取出做空格
    while (colon < end && isspace(*colon))
    {
      ++colon;
    }

    // header 值
    string value(colon, end);

    // 去除右空格
    while (!value.empty() && isspace(value[value.size()-1]))
    {
      value.resize(value.size()-1);
    }

    // 保存 header 和 值
    headers_[field] = value;
  }

  //返回 header 对应的值
  string getHeader(const string& field) const
  {
    string result;
    std::map<string, string>::const_iterator it = headers_.find(field);
    if (it != headers_.end())
    {
      result = it->second;
    }
    return result;
  }

  const std::map<string, string>& headers() const
  { return headers_; }

  //交换
  void swap(HttpRequest& that)
  {
    std::swap(method_, that.method_);
    std::swap(version_, that.version_);
    path_.swap(that.path_);
    query_.swap(that.query_);
    receiveTime_.swap(that.receiveTime_);
    headers_.swap(that.headers_);
  }

 private:
  Method method_;         //请求方法
  Version version_;       //请求版本
  string path_;           //请求路径
  string query_;          //请求查询
  Timestamp receiveTime_; //请求时间
  std::map<string, string> headers_;    //header 列表
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_HTTP_HTTPREQUEST_H

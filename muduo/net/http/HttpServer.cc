// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/http/HttpServer.h"

#include "muduo/base/Logging.h"
#include "muduo/net/http/HttpContext.h"
#include "muduo/net/http/HttpRequest.h"
#include "muduo/net/http/HttpResponse.h"

using namespace muduo;
using namespace muduo::net;

namespace muduo
{
namespace net
{
namespace detail
{

void defaultHttpCallback(const HttpRequest&, HttpResponse* resp)
{
  resp->setStatusCode(HttpResponse::k404NotFound);
  resp->setStatusMessage("Not Found");
  resp->setCloseConnection(true);
}

}  // namespace detail
}  // namespace net
}  // namespace muduo

HttpServer::HttpServer(EventLoop* loop,
                       const InetAddress& listenAddr,
                       const string& name,
                       TcpServer::Option option)
  //创建一个 server_ 对象，绑定服务器地址，但没有监听读事件
  : server_(loop, listenAddr, name, option),
  //默认 httpCallback_ 函数
    httpCallback_(detail::defaultHttpCallback)
{
  //设置连接回调函数
  server_.setConnectionCallback(
      std::bind(&HttpServer::onConnection, this, _1));
  
  //设置消息到来回调函数
  server_.setMessageCallback(
      std::bind(&HttpServer::onMessage, this, _1, _2, _3));
}

void HttpServer::start()
{
  LOG_WARN << "HttpServer[" << server_.name()
    << "] starts listening on " << server_.ipPort();
  //打开客户端的读事件【连接事件】
  server_.start();
}

//建立 TcpConnection 时进行绑定
void HttpServer::onConnection(const TcpConnectionPtr& conn)
{
  if (conn->connected())
  {
    // 创建一个 HttpContext 对象，绑定 TcpConnection
    // 通过 context 来解析上下文请求
    conn->setContext(HttpContext());
  }
}

//当客户端发送请求，将客户端数据保存到 buffer 中
//然后调用 messageCallBack
void HttpServer::onMessage(const TcpConnectionPtr& conn,
                           Buffer* buf,
                           Timestamp receiveTime)
{
  //将 context 对象的类型改变为 HttpContext
  HttpContext* context = boost::any_cast<HttpContext>(conn->getMutableContext());

  //开始解析
  if (!context->parseRequest(buf, receiveTime))
  {
    //如果解析失败
    conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
    //服务器断开客户端连接
    conn->shutdown();
  }

  if (context->gotAll())
  {
    //请求解析好后调用 onRequest 函数
    onRequest(conn, context->request());
    context->reset();   //本次请求处理完毕，重置 HttpContext，适用于长连接
  }
}

void HttpServer::onRequest(const TcpConnectionPtr& conn, const HttpRequest& req)
{
  const string& connection = req.getHeader("Connection");
  bool close = connection == "close" ||
    (req.getVersion() == HttpRequest::kHttp10 && connection != "Keep-Alive");
  HttpResponse response(close);
  //httpcallback 应该是根据客户端请求状态返回应答
  httpCallback_(req, &response);
  Buffer buf;
  //将应答信息保存到 buf 中
  response.appendToBuffer(&buf);

  //发送应答信息
  conn->send(&buf);
  if (response.closeConnection())
  {
    conn->shutdown();
  }
}


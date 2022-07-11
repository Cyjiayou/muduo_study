#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThread.h"
#include "muduo/base/Thread.h"

#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

int cnt = 0;
EventLoop* g_loop;

void printTid()
{
  printf("pid = %d, tid = %d\n", getpid(), CurrentThread::tid());
  printf("now %s\n", Timestamp::now().toString().c_str());
}

void print(const char* msg)
{
  printf("msg %s %s\n", Timestamp::now().toString().c_str(), msg);
  if (++cnt == 20)
  {
    g_loop->quit();
  }
}

void cancel(TimerId timer)
{
  g_loop->cancel(timer);
  printf("cancelled at %s\n", Timestamp::now().toString().c_str());
}

int main()
{
  printTid();
  sleep(1);
  {
    EventLoop loop;
    g_loop = &loop;

    print("main");
    //1s 后调用 print 打印一次 once1
    loop.runAfter(1, std::bind(print, "once1"));
    loop.runAfter(1.5, std::bind(print, "once1.5"));
    loop.runAfter(2.5, std::bind(print, "once2.5"));
    loop.runAfter(3.5, std::bind(print, "once3.5"));
    TimerId t45 = loop.runAfter(4.5, std::bind(print, "once4.5"));
    
    //4.2 s 调用 looper 的 cancel 函数取消 4.5 s 的打印
    loop.runAfter(4.2, std::bind(cancel, t45));
    loop.runAfter(4.8, std::bind(cancel, t45));
    //每隔 2s 打印一次 every2
    loop.runEvery(2, std::bind(print, "every2"));
    //每隔 3s 打印一次 every3
    TimerId t3 = loop.runEvery(3, std::bind(print, "every3"));
    //在 9.001 秒时调用 cannel 取消每次 3s 的定时器
    loop.runAfter(9.001, std::bind(cancel, t3));

    loop.loop();
    print("main loop exits");
  }
  sleep(1);
  {
    EventLoopThread loopThread;
    EventLoop* loop = loopThread.startLoop();
    loop->runAfter(2, printTid);
    sleep(3);
    print("thread loop exits");
  }
}

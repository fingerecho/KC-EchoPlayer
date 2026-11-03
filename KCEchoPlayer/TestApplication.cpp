#include "TestApplication.h"
#include "MainThread.h"
#include "logUtils.h"

TestApplication::TestApplication()
{
//    WriteLog(L"我是方克铖TestApplication...");
}

TestApplication::~TestApplication()
{
}

void TestApplication::Run()
{
    // 创建主线程
    MainThread thread;

    // 执行主线程循环
    thread.RunMessageLoop();
}

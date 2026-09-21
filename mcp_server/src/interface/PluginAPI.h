//  The MIT License
//
//  Copyright (C) 2025 Giuseppe Mastrangelo
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  'Software'), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be
//  included in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

#ifndef MCP_SERVER_PLUGINAPI_H
#define MCP_SERVER_PLUGINAPI_H

#ifdef _WIN32
#define PLUGIN_API __declspec(dllexport)
#else
#define PLUGIN_API __attribute__((visibility("default")))        // 让函数、类、变量在动态库（.so）中导出符号， 作为插件接口必须加，否则无法通过dlopen,dysym找到
#endif

#ifdef __cplusplus
extern "C" {
#endif

//定义一个函数指针，它的类型 是一个函数，参数是两个const char*，返回值是void
typedef void (*ClientNotificationCallback)(const char* pluginName, const char* notification);  // define a function pointer type for sending notifications to the client

typedef enum {
    PLUGIN_TYPE_TOOLS = 0,
    PLUGIN_TYPE_PROMPTS = 1,
    PLUGIN_TYPE_RESOURCES = 2
} PluginType;

typedef struct {
    const char* name;
    const char* description;
    const char* inputSchema;  // JSON schema as a string
} PluginTool;

typedef struct {
    const char* name;
    const char* description;
    const char* arguments;  // JSON arguments as a string
} PluginPrompt;

typedef struct {
    const char* name;
    const char* description;
    const char* uri;
    const char* mime;
} PluginResource;

typedef struct {
    ClientNotificationCallback SendToClient;    // you should not touch this
} NotificationSystem;


//插件通常是动态库.so，主程序和插件可能用不同的编译器编译，跨动态库调用类成员函数可能会出现class ABI不兼容
//这里使用纯c风格的struct,就是为了避免cpp class ABI不兼容的问题
//PluginAPI 本质是 “用 C 结构体封装一组 C 风格函数指针”，主程序和插件只要遵守相同的 C ABI 约定，即使编译环境不同，也能正确调用 —— 这是 C++ 类无法做到的。
// 插件只需按约定实现这些函数，并通过下面的 CreatePlugin/DestroyPlugin
// 以统一 C ABI 暴露 PluginAPI 函数指针结构体。
typedef struct {                     
    const char* (*GetName)();
    const char* (*GetVersion)();
    PluginType (*GetType)();
    int (*Initialize)();
    char* (*HandleRequest)(const char* request);  //
    void (*Shutdown)();
    int (*GetToolCount)();
    const PluginTool* (*GetTool)(int index);
    int (*GetPromptCount)();
    const PluginPrompt* (*GetPrompt)(int index);
    int (*GetResourceCount)();
    const PluginResource* (*GetResource)(int index);
    NotificationSystem* notifications;
} PluginAPI;

//插件工厂函数
PLUGIN_API PluginAPI* CreatePlugin(); // PLUGIN_API
PLUGIN_API void DestroyPlugin(PluginAPI*);

#ifdef __cplusplus
}
#endif

#endif //MCP_SERVER_PLUGINAPI_H

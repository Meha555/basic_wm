/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  Copyright (C) 2013-2017 Chuan Ji <ji@chu4n.com>                          *
 *                                                                           *
 *  Licensed under the Apache License, Version 2.0 (the "License");          *
 *  you may not use this file except in compliance with the License.         *
 *  You may obtain a copy of the License at                                  *
 *                                                                           *
 *   http://www.apache.org/licenses/LICENSE-2.0                              *
 *                                                                           *
 *  Unless required by applicable law or agreed to in writing, software      *
 *  distributed under the License is distributed on an "AS IS" BASIS,        *
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. *
 *  See the License for the specific language governing permissions and      *
 *  limitations under the License.                                           *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <cstdlib>
#include <glog/logging.h>
#include "window_manager.hpp"

using ::std::unique_ptr;

inline void errorStackPrinter(const char *str, int size) {
    LOG(ERROR) << ::std::string(str, size);
}

int main(int argc, char** argv) {
  FLAGS_colorlogtostderr = true;
  ::google::InstallFailureSignalHandler(); // 配置安装程序崩溃失败信号处理器
  ::google::InstallFailureWriter(errorStackPrinter); // 安装配置程序失败信号的信息打印过程，设置回调函数
  ::google::InitGoogleLogging(argv[0]);

  unique_ptr<WindowManager> window_manager = WindowManager::Create(); // CLI参数留空，使用DISPLAY环境变量
  if (!window_manager) {
    LOG(ERROR) << "Failed to initialize window manager.";
    return EXIT_FAILURE;
  }

  window_manager->Run();

  return EXIT_SUCCESS;
}

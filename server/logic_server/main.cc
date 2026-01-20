#include <spdlog/spdlog.h>
#include <workflow/WFFacilities.h> // 引入 workflow 头文件测试一下

int main() {
    // 打印一条日志证明活着
    spdlog::info("Logic Server is starting...");

    // 简单测试一下 workflow 的等待组 (WaitGroup)，证明库链接没问题
    WFFacilities::WaitGroup wait_group(1);
    wait_group.done();

    spdlog::info("Logic Server environment check passed!");
    return 0;
}
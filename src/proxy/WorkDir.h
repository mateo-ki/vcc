#pragma once

#include <QString>

#include "core/Result.h"
#include "kernel/KernelTypes.h"

namespace Proxy {

/// 一次内核运行的工作目录：临时目录放配置与日志，mihomo 的 `-d` 另外指向它自己的
/// 托管内核目录（geo 库和 cache.db 落在那里）。
class ProxyWorkDir {
public:
    static Result<ProxyWorkDir> create(Kernel::Kind kernel);

    ProxyWorkDir() = default;
    ProxyWorkDir(ProxyWorkDir &&other) noexcept;
    ProxyWorkDir &operator=(ProxyWorkDir &&other) noexcept;
    ProxyWorkDir(const ProxyWorkDir &) = delete;
    ProxyWorkDir &operator=(const ProxyWorkDir &) = delete;

    QString root() const { return root_; }
    QString dataRoot() const { return dataRoot_; }
    QString configPath() const;
    QString logPath() const;
    bool isValid() const { return !root_.isEmpty(); }

    Status cleanup();

private:
    ProxyWorkDir(QString root, QString dataRoot, Kernel::Kind kernel)
        : root_(std::move(root)), dataRoot_(std::move(dataRoot)), kernel_(kernel)
    {
    }

    QString root_;
    QString dataRoot_;
    Kernel::Kind kernel_ = Kernel::Kind::Mihomo;
};

}

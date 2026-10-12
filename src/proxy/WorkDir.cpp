#include "proxy/WorkDir.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/Ids.h"

// managed_kernel_root 由 kernel 模块提供（并行移植中），这里只声明用到的签名。
namespace Kernel {
QString managedKernelRoot();
}

namespace Proxy {

Result<ProxyWorkDir> ProxyWorkDir::create(Kernel::Kind kernel)
{
    const QString base = QDir::tempPath();
    const QString root = QDir(base).filePath(QStringLiteral("vcc-proxy-%1")
                                                 .arg(Ids::newUuidV4()));
    if (!QDir().mkpath(root)) {
        return Result<ProxyWorkDir>::withError(
            QStringLiteral("创建 %1 工作目录失败: 无法建立 %2").arg(Kernel::kindDisplayName(kernel),
                                                                  root));
    }
    QString dataRoot = root;
    if (kernel == Kernel::Kind::Mihomo) {
        dataRoot = QDir(Kernel::managedKernelRoot()).filePath(Kernel::kindDirectoryName(kernel));
        if (!QDir().mkpath(dataRoot)) {
            QFile::remove(root);
            return Result<ProxyWorkDir>::withError(
                QStringLiteral("无法创建 mihomo 数据目录 %1").arg(dataRoot));
        }
    }
    return Result<ProxyWorkDir>::withValue(ProxyWorkDir(root, dataRoot, kernel));
}

ProxyWorkDir::ProxyWorkDir(ProxyWorkDir &&other) noexcept
    : root_(std::move(other.root_)), dataRoot_(std::move(other.dataRoot_)), kernel_(other.kernel_)
{
    other.root_.clear();
}

ProxyWorkDir &ProxyWorkDir::operator=(ProxyWorkDir &&other) noexcept
{
    if (this != &other) {
        root_ = std::move(other.root_);
        dataRoot_ = std::move(other.dataRoot_);
        kernel_ = other.kernel_;
        other.root_.clear();
    }
    return *this;
}

QString ProxyWorkDir::configPath() const
{
    return QDir(root_).filePath(kernel_ == Kernel::Kind::Mihomo ? QStringLiteral("config.yaml")
                                                                : QStringLiteral("config.json"));
}

QString ProxyWorkDir::logPath() const
{
    const QString name = kernel_ == Kernel::Kind::Mihomo   ? QStringLiteral("mihomo.log")
        : kernel_ == Kernel::Kind::SingBox                 ? QStringLiteral("sing-box.log")
                                                           : QStringLiteral("xray.log");
    return QDir(root_).filePath(name);
}

Status ProxyWorkDir::cleanup()
{
    if (root_.isEmpty()) {
        return Status::succeeded();
    }
    QDir dir(root_);
    if (dir.exists() && !dir.removeRecursively()) {
        return Status::failed(QStringLiteral("删除工作目录失败: %1").arg(root_));
    }
    return Status::succeeded();
}

}

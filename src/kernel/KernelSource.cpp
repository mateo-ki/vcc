#include "kernel/KernelSource.h"

#include <QByteArray>

#include "kernel/KernelMessages.h"

namespace Kernel {

const QList<QPair<QString, QString>> &assetMirrorPrefixes()
{
    // 对应 Rust `ASSET_MIRROR_PREFIXES`：顺序即匹配顺序。
    static const QList<QPair<QString, QString>> prefixes = {
        {QStringLiteral("gh-proxy"), QStringLiteral("https://gh-proxy.com/")},
        {QStringLiteral("ghfast"), QStringLiteral("https://ghfast.top/")},
    };
    return prefixes;
}

const QList<SourceOption> &sourceOptions()
{
    static const QList<SourceOption> options = {
        SourceOption {QStringLiteral("gh-proxy"), Messages::sourceGhProxyLabel(),
                      Messages::sourceGhProxyHint(), Source::GhProxy},
        SourceOption {QStringLiteral("ghfast"), Messages::sourceGhFastLabel(),
                      Messages::sourceGhFastHint(), Source::GhFast},
        SourceOption {QStringLiteral("official"), Messages::sourceOfficialLabel(),
                      Messages::sourceOfficialHint(), Source::Official},
        SourceOption {QStringLiteral("proxy"), Messages::sourceProxyLabel(),
                      Messages::sourceProxyHint(), Source::Proxy},
        SourceOption {QStringLiteral("local"), Messages::sourceLocalLabel(),
                      Messages::sourceLocalHint(), Source::Local},
    };
    return options;
}

Source sourceFromId(const QString &id)
{
    for (const SourceOption &option : sourceOptions()) {
        if (option.id == id) {
            return option.source;
        }
    }
    // 旧的 "race"（三家并发抢跑）已经删掉，存量的值得落到它当时实测最快的那一家。
    return Source::GhProxy;
}

QString sourceId(Source source)
{
    switch (source) {
    case Source::GhProxy:
        return QStringLiteral("gh-proxy");
    case Source::GhFast:
        return QStringLiteral("ghfast");
    case Source::Official:
        return QStringLiteral("official");
    case Source::Proxy:
        return QStringLiteral("proxy");
    case Source::Local:
        return QStringLiteral("local");
    }
    return QStringLiteral("gh-proxy");
}

QString sourceLabel(Source source)
{
    for (const SourceOption &option : sourceOptions()) {
        if (option.source == source) {
            return option.label;
        }
    }
    return Messages::sourceLabelFallback();
}

std::optional<QString> sourceMirrorPrefix(Source source)
{
    const QString id = sourceId(source);
    for (const QPair<QString, QString> &entry : assetMirrorPrefixes()) {
        if (entry.first == id) {
            return entry.second;
        }
    }
    return std::nullopt;
}

QString assetUrlFor(Source source, const QString &directUrl)
{
    const std::optional<QString> prefix = sourceMirrorPrefix(source);
    return prefix.has_value() ? *prefix + directUrl : directUrl;
}

bool sourceAllowsTransport(Source source, const QString &transportLabel)
{
    const bool direct = transportLabel == Messages::directTransport();
    switch (source) {
    // 镜像站本身就在国内，走代理纯属绕路；本地导入根本不联网。
    case Source::GhProxy:
    case Source::GhFast:
    case Source::Official:
        return direct;
    case Source::Proxy:
        return !direct;
    case Source::Local:
        return true;
    }
    return direct;
}

}

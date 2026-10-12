#include "gui/KernelRouting.h"

#include <algorithm>

#include "core/JsonUtil.h"

namespace Gui {

namespace {

bool isSelectableKind(const QString &kind)
{
    // Rust mod.rs:2708-2712：select / selector / url-test / urltest 才算可选路组，
    // 其余（socks/http/direct/reject/单个节点）一律不进列表。
    return kind == QLatin1String("select") || kind == QLatin1String("selector")
        || kind == QLatin1String("url-test") || kind == QLatin1String("urltest");
}

QString stringOf(const Json &object, const char *key)
{
    if (!object.is_object() || !object.contains(key) || !object[key].is_string()) {
        return QString();
    }
    return QString::fromStdString(object[key].get<std::string>());
}

const KernelGroup *groupNamed(const QVector<KernelGroup> &groups, const QString &name)
{
    for (const KernelGroup &group : groups) {
        if (group.name == name) {
            return &group;
        }
    }
    return nullptr;
}

} // namespace

KernelProxies parseKernelProxies(const QByteArray &body)
{
    KernelProxies parsed;
    Json root;
    try {
        root = Json::parse(body.toStdString());
    } catch (...) {
        return parsed;
    }
    if (!root.is_object() || !root.contains("proxies") || !root["proxies"].is_object()) {
        return parsed;
    }
    for (auto entry = root["proxies"].begin(); entry != root["proxies"].end(); ++entry) {
        const Json &item = entry.value();
        if (!item.is_object()) {
            continue;
        }
        const QString name = QString::fromStdString(entry.key());
        const QString kind = QString::fromStdString(stringOf(item, "type").toStdString())
                                 .toLower();

        // history 按时间从旧到新，最后一条才是刚测出来的（Rust mod.rs:2694-2703）。
        if (item.contains("history") && item["history"].is_array() && !item["history"].empty()) {
            const Json &last = item["history"].back();
            KernelLatency latency;
            latency.name = name;
            if (last.is_object() && last.contains("delay") && last["delay"].is_number()) {
                latency.delayMs = int(last["delay"].get<qint64>());
            }
            // alive 缺省按 true（Rust 是 unwrap_or(true)）。
            latency.alive = !(item.contains("alive") && item["alive"].is_boolean()
                              && !item["alive"].get<bool>());
            parsed.latencies.push_back(latency);
        }

        if (!isSelectableKind(kind)) {
            continue;
        }
        KernelGroup group;
        group.name = name;
        group.kind = kind;
        group.now = stringOf(item, "now");   // 空串就等于 None
        if (item.contains("all") && item["all"].is_array()) {
            for (const Json &member : item["all"]) {
                if (member.is_string()) {
                    group.members.push_back(QString::fromStdString(member.get<std::string>()));
                }
            }
        }
        parsed.groups.push_back(group);
    }
    std::sort(parsed.groups.begin(), parsed.groups.end(),
              [](const KernelGroup &a, const KernelGroup &b) { return a.name < b.name; });
    return parsed;
}

QString proxyGroupNow(const QVector<KernelGroup> &groups)
{
    const KernelGroup *proxy = groupNamed(groups, QString::fromLatin1(kProxyGroup));
    return proxy == nullptr ? QString() : proxy->now;
}

QString finalGroupNow(const QVector<KernelGroup> &groups)
{
    const KernelGroup *final_ = groupNamed(groups, QString::fromLatin1(kFinalGroup));
    return final_ == nullptr ? QString() : final_->now;
}

QString kernelEgressName(const QVector<KernelGroup> &groups)
{
    const QString now = proxyGroupNow(groups);
    if (now.isEmpty()) {
        return QString();
    }
    if (now != QString::fromUtf8(kUrlTestGroup)) {
        return now;
    }
    // PROXY 交给「自动选择」了，那真出口是那个组选中的成员。
    const KernelGroup *autoGroup = groupNamed(groups, QString::fromUtf8(kUrlTestGroup));
    return autoGroup == nullptr ? QString() : autoGroup->now;
}

} // namespace Gui

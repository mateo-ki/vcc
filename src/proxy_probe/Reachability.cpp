#include "proxy_probe/Reachability.h"

#include <QTcpSocket>

#include <QtConcurrent>

namespace ProxyProbe {
namespace {

bool tcpReachable(const ReachTarget &target, int timeoutMs)
{
    QTcpSocket socket;
    socket.connectToHost(target.host, target.port);
    return socket.waitForConnected(timeoutMs);
}

}

std::optional<std::pair<QString, QString>> firstReachable(const QVector<ReachTarget> &candidates,
                                                          int timeoutMs)
{
    QVector<QFuture<bool>> probes;
    probes.reserve(candidates.size());
    for (const ReachTarget &target : candidates) {
        probes.push_back(QtConcurrent::run(tcpReachable, target, timeoutMs));
    }
    // 与 Rust 一样先全部 join，再按原顺序取第一个可达的：并发探测的完成顺序不代表优先级。
    for (QFuture<bool> &probe : probes) {
        probe.waitForFinished();
    }
    for (int index = 0; index < probes.size(); ++index) {
        if (probes[index].result()) {
            return std::make_pair(candidates.at(index).id, candidates.at(index).name);
        }
    }
    return std::nullopt;
}

}

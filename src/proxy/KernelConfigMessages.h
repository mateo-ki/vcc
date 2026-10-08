// 由 tools/gen-kernel-config-cjk-codepoints.mjs 从 Rust 源生成，请勿手改。
// 对应 proxy/sing_box.rs 与 proxy/xray.rs 里的用户可见文本：内核不支持某条规则时
// 必须报错而不是静默丢弃，所以这些文本本身就是行为规约的一部分。
#pragma once

#include <QString>

#include <initializer_list>

namespace Proxy::Messages {

/// Rust 的 `format!` 只扫描模板一次，插进去的值不会被二次求值；这里同样一遍扫描，
/// 免得节点名里真有 "{error}" 之类的字面量时被连环替换。
struct Placeholder {
    const char *marker;
    QString value;
};

inline QString fill(const QString &text, std::initializer_list<Placeholder> args)
{
    QString out;
    out.reserve(text.size() + 16);
    for (qsizetype index = 0; index < text.size();) {
        bool matched = false;
        if (text.at(index) == QLatin1Char('{')) {
            for (const Placeholder &arg : args) {
                const QString marker = QLatin1String(arg.marker);
                if (!marker.isEmpty() && text.mid(index, marker.size()) == marker) {
                    out += arg.value;
                    index += marker.size();
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) {
            out += text.at(index);
            ++index;
        }
    }
    return out;
}

QString singBoxKernelNotFound();
QString singBoxBatchFailed();
QString singBoxConfigUnsupported();
QString singBoxStartupFailed();
QString singBoxNoLogLine();
QString singBoxRealProxyOk();
QString singBoxRealProxyFailed();
QString singBoxProbeTargetOk();
QString singBoxUnreachable();
QString probeConcurrencyClosed();
QString dohUrlInvalid();
QString dohUrlMissingHost();
QString singBoxProtocolUnsupported();
QString singBoxWgMissingAddress();
QString singBoxGeoSiteUnsupported();
QString singBoxGeoIpUnsupported();
QString singBoxPortRuleInvalid();
QString singBoxMissingField();
QString xrayKernelNotFound();
QString xrayBatchFailed();
QString xrayConfigUnsupported();
QString xrayRealProxyOk();
QString xrayRealProxyFailed();
QString xrayBatchStartupFailed();
QString xrayUnknownError();
QString xrayTunUnsupported();
QString xrayPortRuleInvalid();
QString xrayProcessNameUnsupported();
QString xrayProtocolUnsupported();
QString xrayMissingField();
QString xrayProcessNameMarker();

}

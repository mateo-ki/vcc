#pragma once

#include <QString>

// 代理主机合法性判定：订阅里大量坏节点是回环/内网/占位域名/真实网站 CDN。
namespace HostUtil {

/// 是否适合作为测速/质量导出的代理主机：拒绝回环、私有、链路本地、组播、
/// 未指定地址，以及坏订阅里常见的占位主机名。
bool isPublicProxyHost(const QString &host);

/// 免费订阅里常被当成假"节点"的真实网站/CDN：TCP+HTTP 探测能过，但不是可用代理。
bool isDecoyWebsiteHost(const QString &host);

/// 质量导出用的更严格白名单。
bool isQualityExportHost(const QString &host);

}

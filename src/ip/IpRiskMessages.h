// 由 tools/gen-geo-ip-cjk-codepoints.mjs 从 Rust 源生成，请勿手改。
// 对应 Rust src/ip_risk.rs 里的用户可见文本与关键字表。
// 这些文本要与 vpn-parser 的 Rust 实现逐字一致（错误消息会进界面和日志）；
// 模板里的 {} / {name} / {error} 等占位符原样保留，由调用方按 Rust 的 format! 语义替换。
#pragma once

#include <QString>
#include <QStringList>

#include <initializer_list>

namespace IpRisk::Messages {

namespace detail {
inline QString fromCodePoints(std::initializer_list<char32_t> points)
{
    return QString::fromUcs4(points.begin(), qsizetype(points.size()));
}
}

// kindDatacenter <- ip_risk.rs:30
// 长度 4，ASCII 骨架: "/"
// UTF-8 hex: e69cbae688bf2fe4ba91
inline QString kindDatacenter()
{
    return detail::fromCodePoints({
        0x673A, 0x623F, 0x002F, 0x4E91,
    });
}

// kindResidential <- ip_risk.rs:32
// 长度 7，ASCII 骨架: "/ISP?"
// UTF-8 hex: e4bd8fe5ae852f4953503f
inline QString kindResidential()
{
    return detail::fromCodePoints({
        0x4F4F, 0x5B85, 0x002F, 0x0049, 0x0053, 0x0050, 0x003F,
    });
}

// hostnameArrow <- ip_risk.rs:38
// 长度 7，ASCII 骨架: "{}  {}"
// UTF-8 hex: 7b7d20e28692207b7d
inline QString hostnameArrow()
{
    return detail::fromCodePoints({
        0x007B, 0x007D, 0x0020, 0x2192, 0x0020, 0x007B, 0x007D,
    });
}

// unknownRegion <- ip_risk.rs:41
// 长度 4，ASCII 骨架: ""
// UTF-8 hex: e69caae79fa5e59cb0e58cba
inline QString unknownRegion()
{
    return detail::fromCodePoints({
        0x672A, 0x77E5, 0x5730, 0x533A,
    });
}

// noAsnInfo <- ip_risk.rs:46
// 长度 8，ASCII 骨架: " ASN "
// UTF-8 hex: e697a02041534e20e4bfa1e681af
inline QString noAsnInfo()
{
    return detail::fromCodePoints({
        0x65E0, 0x0020, 0x0041, 0x0053, 0x004E, 0x0020, 0x4FE1, 0x606F,
    });
}

// headlineJoiner <- ip_risk.rs:52
// 长度 3，ASCII 骨架: "  "
// UTF-8 hex: 20c2b720
inline QString headlineJoiner()
{
    return detail::fromCodePoints({
        0x0020, 0x00B7, 0x0020,
    });
}

// parseFailed <- ip_risk.rs:195
// 长度 19，ASCII 骨架: " IP : {error}"
// UTF-8 hex: e8a7a3e69e9020495020e5bd92e5b19ee5a4b1e8b4a53a207b6572726f727d
inline QString parseFailed()
{
    return detail::fromCodePoints({
        0x89E3, 0x6790, 0x0020, 0x0049, 0x0050, 0x0020, 0x5F52, 0x5C5E,
        0x5931, 0x8D25, 0x003A, 0x0020, 0x007B, 0x0065, 0x0072, 0x0072,
        0x006F, 0x0072, 0x007D,
    });
}

// verdictDatacenter <- ip_risk.rs:204
// 长度 68，ASCII 骨架: "/AI OpenAI / Google / Claude ASN "
// UTF-8 hex: e69cbae688bf2fe4ba91e58e82e59586e587bae58fa3efbc9a414920e7ab99e782b9efbc884f70656e4149202f20476f6f676c65202f20436c61756465efbc89e69bb4e5aeb9e69893e68c892041534e20e9a38ee68ea7efbc8ce68e89e799bbe5bd95e38081e8a681e6b182e9aa8ce8af81e68896e5b081e58fb7e79a84e9a38ee999a9e5818fe9ab98
inline QString verdictDatacenter()
{
    return detail::fromCodePoints({
        0x673A, 0x623F, 0x002F, 0x4E91, 0x5382, 0x5546, 0x51FA, 0x53E3,
        0xFF1A, 0x0041, 0x0049, 0x0020, 0x7AD9, 0x70B9, 0xFF08, 0x004F,
        0x0070, 0x0065, 0x006E, 0x0041, 0x0049, 0x0020, 0x002F, 0x0020,
        0x0047, 0x006F, 0x006F, 0x0067, 0x006C, 0x0065, 0x0020, 0x002F,
        0x0020, 0x0043, 0x006C, 0x0061, 0x0075, 0x0064, 0x0065, 0xFF09,
        0x66F4, 0x5BB9, 0x6613, 0x6309, 0x0020, 0x0041, 0x0053, 0x004E,
        0x0020, 0x98CE, 0x63A7, 0xFF0C, 0x6389, 0x767B, 0x5F55, 0x3001,
        0x8981, 0x6C42, 0x9A8C, 0x8BC1, 0x6216, 0x5C01, 0x53F7, 0x7684,
        0x98CE, 0x9669, 0x504F, 0x9AD8,
    });
}

// verdictPublicProxy <- ip_risk.rs:207
// 长度 34，ASCII 骨架: " / VPN "
// UTF-8 hex: e4b88de698afe69cbae688bfefbc8ce4bd86e8a2abe6a087e68890e585ace5bc80e4bba3e79086202f2056504e20e587bae58fa3efbc9ae8b4a6e58fb7e7b1bbe7ab99e782b9e4bb8de4bc9ae58cbae588abe5afb9e5be85
inline QString verdictPublicProxy()
{
    return detail::fromCodePoints({
        0x4E0D, 0x662F, 0x673A, 0x623F, 0xFF0C, 0x4F46, 0x88AB, 0x6807,
        0x6210, 0x516C, 0x5F00, 0x4EE3, 0x7406, 0x0020, 0x002F, 0x0020,
        0x0056, 0x0050, 0x004E, 0x0020, 0x51FA, 0x53E3, 0xFF1A, 0x8D26,
        0x53F7, 0x7C7B, 0x7AD9, 0x70B9, 0x4ECD, 0x4F1A, 0x533A, 0x522B,
        0x5BF9, 0x5F85,
    });
}

// verdictResidential <- ip_risk.rs:209
// 长度 28，ASCII 骨架: "/ISP "
// UTF-8 hex: e4bd8fe5ae852f49535020e587bae58fa3efbc9ae9a38ee68ea7e58f8be5a5bdefbc8ce9809ae5b8b8e58fafe4bba5e6ada3e5b8b8e799bbe5bd95e4b88ee4bb98e8b4b9e9aa8ce8af81
inline QString verdictResidential()
{
    return detail::fromCodePoints({
        0x4F4F, 0x5B85, 0x002F, 0x0049, 0x0053, 0x0050, 0x0020, 0x51FA,
        0x53E3, 0xFF1A, 0x98CE, 0x63A7, 0x53CB, 0x597D, 0xFF0C, 0x901A,
        0x5E38, 0x53EF, 0x4EE5, 0x6B63, 0x5E38, 0x767B, 0x5F55, 0x4E0E,
        0x4ED8, 0x8D39, 0x9A8C, 0x8BC1,
    });
}

// resolveFailed <- ip_risk.rs:227
// 长度 25，ASCII 骨架: " {host} : {error}"
// UTF-8 hex: e8a7a3e69e90e88a82e782b9e59f9fe5908d207b686f73747d20e5a4b1e8b4a53a207b6572726f727d
inline QString resolveFailed()
{
    return detail::fromCodePoints({
        0x89E3, 0x6790, 0x8282, 0x70B9, 0x57DF, 0x540D, 0x0020, 0x007B,
        0x0068, 0x006F, 0x0073, 0x0074, 0x007D, 0x0020, 0x5931, 0x8D25,
        0x003A, 0x0020, 0x007B, 0x0065, 0x0072, 0x0072, 0x006F, 0x0072,
        0x007D,
    });
}

// noRecords <- ip_risk.rs:235
// 长度 20，ASCII 骨架: " {host} "
// UTF-8 hex: e88a82e782b9e59f9fe5908d207b686f73747d20e6b2a1e69c89e58fafe794a8e8a7a3e69e90e8aeb0e5bd95
inline QString noRecords()
{
    return detail::fromCodePoints({
        0x8282, 0x70B9, 0x57DF, 0x540D, 0x0020, 0x007B, 0x0068, 0x006F,
        0x0073, 0x0074, 0x007D, 0x0020, 0x6CA1, 0x6709, 0x53EF, 0x7528,
        0x89E3, 0x6790, 0x8BB0, 0x5F55,
    });
}

// queryFailed <- ip_risk.rs:246
// 长度 22，ASCII 骨架: "{what} : {error:#}"
// UTF-8 hex: 7b776861747d20e69fa5e8afa2e5a4b1e8b4a53a207b6572726f723a237d
inline QString queryFailed()
{
    return detail::fromCodePoints({
        0x007B, 0x0077, 0x0068, 0x0061, 0x0074, 0x007D, 0x0020, 0x67E5,
        0x8BE2, 0x5931, 0x8D25, 0x003A, 0x0020, 0x007B, 0x0065, 0x0072,
        0x0072, 0x006F, 0x0072, 0x003A, 0x0023, 0x007D,
    });
}

// readFailed <- ip_risk.rs:251
// 长度 24，ASCII 骨架: "{what} : {error:#}"
// UTF-8 hex: 7b776861747d20e8afbbe58f96e5938de5ba94e5a4b1e8b4a53a207b6572726f723a237d
inline QString readFailed()
{
    return detail::fromCodePoints({
        0x007B, 0x0077, 0x0068, 0x0061, 0x0074, 0x007D, 0x0020, 0x8BFB,
        0x53D6, 0x54CD, 0x5E94, 0x5931, 0x8D25, 0x003A, 0x0020, 0x007B,
        0x0065, 0x0072, 0x0072, 0x006F, 0x0072, 0x003A, 0x0023, 0x007D,
    });
}

// invalidProxy <- ip_risk.rs:289
// 长度 15，ASCII 骨架: ": {error}"
// UTF-8 hex: e4bba3e79086e59cb0e59d80e697a0e695883a207b6572726f727d
inline QString invalidProxy()
{
    return detail::fromCodePoints({
        0x4EE3, 0x7406, 0x5730, 0x5740, 0x65E0, 0x6548, 0x003A, 0x0020,
        0x007B, 0x0065, 0x0072, 0x0072, 0x006F, 0x0072, 0x007D,
    });
}

// buildProxyClientFailed <- ip_risk.rs:292
// 长度 18，ASCII 骨架: ": {error}"
// UTF-8 hex: e69e84e5bbbae4bba3e79086e5aea2e688b7e7abafe5a4b1e8b4a53a207b6572726f727d
inline QString buildProxyClientFailed()
{
    return detail::fromCodePoints({
        0x6784, 0x5EFA, 0x4EE3, 0x7406, 0x5BA2, 0x6237, 0x7AEF, 0x5931,
        0x8D25, 0x003A, 0x0020, 0x007B, 0x0065, 0x0072, 0x0072, 0x006F,
        0x0072, 0x007D,
    });
}

// noEgressAddress <- ip_risk.rs:294
// 长度 9，ASCII 骨架: ""
// UTF-8 hex: e6b2a1e69c89e58fafe794a8e79a84e59b9ee698bee59cb0e59d80
inline QString noEgressAddress()
{
    return detail::fromCodePoints({
        0x6CA1, 0x6709, 0x53EF, 0x7528, 0x7684, 0x56DE, 0x663E, 0x5730,
        0x5740,
    });
}

// egressLabel <- ip_risk.rs:296
// 长度 5，ASCII 骨架: " IP"
// UTF-8 hex: e587bae58fa3204950
inline QString egressLabel()
{
    return detail::fromCodePoints({
        0x51FA, 0x53E3, 0x0020, 0x0049, 0x0050,
    });
}

// notAnIp <- ip_risk.rs:307
// 长度 33，ASCII 骨架: "{url}  IP : {candidate:?}"
// UTF-8 hex: 7b75726c7d20e8bf94e59b9ee4ba86e4b88de698af20495020e79a84e58685e5aeb93a207b63616e6469646174653a3f7d
inline QString notAnIp()
{
    return detail::fromCodePoints({
        0x007B, 0x0075, 0x0072, 0x006C, 0x007D, 0x0020, 0x8FD4, 0x56DE,
        0x4E86, 0x4E0D, 0x662F, 0x0020, 0x0049, 0x0050, 0x0020, 0x7684,
        0x5185, 0x5BB9, 0x003A, 0x0020, 0x007B, 0x0063, 0x0061, 0x006E,
        0x0064, 0x0069, 0x0064, 0x0061, 0x0074, 0x0065, 0x003A, 0x003F,
        0x007D,
    });
}

// egressFailed <- ip_risk.rs:312
// 长度 26，ASCII 骨架: " IP : {last_error}"
// UTF-8 hex: e7bb8fe4bba3e79086e58f96e587bae58fa320495020e5a4b1e8b4a53a207b6c6173745f6572726f727d
inline QString egressFailed()
{
    return detail::fromCodePoints({
        0x7ECF, 0x4EE3, 0x7406, 0x53D6, 0x51FA, 0x53E3, 0x0020, 0x0049,
        0x0050, 0x0020, 0x5931, 0x8D25, 0x003A, 0x0020, 0x007B, 0x006C,
        0x0061, 0x0073, 0x0074, 0x005F, 0x0065, 0x0072, 0x0072, 0x006F,
        0x0072, 0x007D,
    });
}

// kDatacenterHints <- ip_risk.rs:97（67 项，逐字照抄）
inline const QStringList kDatacenterHints = {
    QStringLiteral("amazon"), QStringLiteral("aws"), QStringLiteral("microsoft"), QStringLiteral("azure"), QStringLiteral("google"), QStringLiteral("gcp"), QStringLiteral("alibaba"), QStringLiteral("aliyun"),
    QStringLiteral("tencent"), QStringLiteral("huawei"), QStringLiteral("oracle"), QStringLiteral("hetzner"), QStringLiteral("ovh"), QStringLiteral("digitalocean"), QStringLiteral("vultr"), QStringLiteral("linode"),
    QStringLiteral("akamai"), QStringLiteral("bytedance"), QStringLiteral("cloudflare"), QStringLiteral("hosting"), QStringLiteral("data center"), QStringLiteral("datacenter"), QStringLiteral("internet services"), QStringLiteral("servers"),
    QStringLiteral("cloud"), QStringLiteral("acck"), QStringLiteral("contabo"), QStringLiteral("hostinger"), QStringLiteral("ionos"), QStringLiteral("selectel"), QStringLiteral("zomro"), QStringLiteral("racknerd"),
    QStringLiteral("leaseweb"), QStringLiteral("worldstream"), QStringLiteral("serverius"), QStringLiteral("aeza"), QStringLiteral("beget"), QStringLiteral("tickets"), QStringLiteral("psychz"), QStringLiteral("gige"),
    QStringLiteral("buyvm"), QStringLiteral("phoenixnap"), QStringLiteral("vds"), QStringLiteral("vps"), QStringLiteral("solutions llc"), QStringLiteral("technology inc"), QStringLiteral("networks hub"), QStringLiteral("the constant company"),
    QStringLiteral("xtom"), QStringLiteral("evoxt"), QStringLiteral("dmit"), QStringLiteral("akilecloud"), QStringLiteral("hostpapa"), QStringLiteral("multacom"), QStringLiteral("cnservers"), QStringLiteral("it7 networks"),
    QStringLiteral("catixs"), QStringLiteral("bytevirt"), QStringLiteral("cyberverse"), QStringLiteral("akari"), QStringLiteral("starry network"), QStringLiteral("host universal"), QStringLiteral("zouter"), QStringLiteral("idc cube"),
    QStringLiteral("sakura internet"), QStringLiteral("data communication"), QStringLiteral("choopa")
};

// kEgressEchoUrls <- ip_risk.rs:295（2 项，逐字照抄）
inline const QStringList kEgressEchoUrls = {
    QStringLiteral("https://api.ipify.org"), QStringLiteral("https://ipinfo.io/ip")
};

}

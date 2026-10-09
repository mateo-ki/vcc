// 由 tools/gen-cjk-codepoints.mjs 从 Rust 源生成，请勿手改。
// 这些提示文本要与 vpn-parser 的 Rust 实现逐字一致（错误消息会进界面和日志）。
#include "sysproxy/Messages.h"

#include <initializer_list>

namespace SysProxy::Messages {
namespace {
QString fromCodePoints(std::initializer_list<char32_t> points)
{
    return QString::fromUcs4(points.begin(), qsizetype(points.size()));
}

}

// platformUnsupported <- system_proxy/mod.rs:-1
// ASCII 骨架: ""
QString platformUnsupported()
{
    return fromCodePoints({
        0x5F53, 0x524D, 0x5E73, 0x53F0, 0x6682, 0x4E0D, 0x652F, 0x6301,
        0x7CFB, 0x7EDF, 0x4EE3, 0x7406, 0x8BBE, 0x7F6E,
    });
}

// wininetNotifyFailed <- system_proxy/mod.rs:-1
// ASCII 骨架: " WinINet "
QString wininetNotifyFailed()
{
    return fromCodePoints({
        0x7CFB, 0x7EDF, 0x4EE3, 0x7406, 0x5DF2, 0x5199, 0x5165, 0xFF0C,
        0x4F46, 0x901A, 0x77E5, 0x0020, 0x0057, 0x0069, 0x006E, 0x0049,
        0x004E, 0x0065, 0x0074, 0x0020, 0x5237, 0x65B0, 0x5931, 0x8D25,
    });
}

// regExitCode <- system_proxy/mod.rs:-1
// ASCII 骨架: "reg.exe  {:?}"
QString regExitCode()
{
    return fromCodePoints({
        0x0072, 0x0065, 0x0067, 0x002E, 0x0065, 0x0078, 0x0065, 0x0020,
        0x9000, 0x51FA, 0x7801, 0x0020, 0x007B, 0x003A, 0x003F, 0x007D,
    });
}

// deleteValueFailed <- system_proxy/mod.rs:-1
// ASCII 骨架: " {name} : {}"
QString deleteValueFailed()
{
    return fromCodePoints({
        0x5220, 0x9664, 0x7CFB, 0x7EDF, 0x4EE3, 0x7406, 0x6CE8, 0x518C,
        0x8868, 0x503C, 0x0020, 0x007B, 0x006E, 0x0061, 0x006D, 0x0065,
        0x007D, 0x0020, 0x5931, 0x8D25, 0x003A, 0x0020, 0x007B, 0x007D,
    });
}

// modifyFailed <- system_proxy/mod.rs:-1
// ASCII 骨架: ": {detail}"
QString modifyFailed()
{
    return fromCodePoints({
        0x4FEE, 0x6539, 0x7CFB, 0x7EDF, 0x4EE3, 0x7406, 0x5931, 0x8D25,
        0x003A, 0x0020, 0x007B, 0x0064, 0x0065, 0x0074, 0x0061, 0x0069,
        0x006C, 0x007D,
    });
}

// writtenPrefix <- system_proxy/terminal.rs:-1
// ASCII 骨架: " {}"
QString writtenPrefix()
{
    return fromCodePoints({
        0x5DF2, 0x5199, 0x5165, 0x0020, 0x007B, 0x007D,
    });
}

// removedPrefix <- system_proxy/terminal.rs:-1
// ASCII 骨架: " {}"
QString removedPrefix()
{
    return fromCodePoints({
        0x5DF2, 0x6E05, 0x9664, 0x0020, 0x007B, 0x007D,
    });
}

// untouchedPrefix <- system_proxy/terminal.rs:-1
// ASCII 骨架: " {}"
QString untouchedPrefix()
{
    return fromCodePoints({
        0x4FDD, 0x7559, 0x4F60, 0x539F, 0x6709, 0x7684, 0x0020, 0x007B,
        0x007D,
    });
}

// noChange <- system_proxy/terminal.rs:-1
// ASCII 骨架: ""
QString noChange()
{
    return fromCodePoints({
        0x7EC8, 0x7AEF, 0x4EE3, 0x7406, 0x53D8, 0x91CF, 0x65E0, 0x53D8,
        0x5316,
    });
}

// joiner <- system_proxy/terminal.rs:-1
// ASCII 骨架: "  "
QString joiner()
{
    return fromCodePoints({
        0x0020, 0x00B7, 0x0020,
    });
}

// writeFailed <- system_proxy/terminal.rs:-1
// ASCII 骨架: " {name} : {}"
QString writeFailed()
{
    return fromCodePoints({
        0x5199, 0x5165, 0x0020, 0x007B, 0x006E, 0x0061, 0x006D, 0x0065,
        0x007D, 0x0020, 0x5931, 0x8D25, 0x003A, 0x0020, 0x007B, 0x007D,
    });
}

// removeFailed <- system_proxy/terminal.rs:-1
// ASCII 骨架: " {name} : {}"
QString removeFailed()
{
    return fromCodePoints({
        0x5220, 0x9664, 0x0020, 0x007B, 0x006E, 0x0061, 0x006D, 0x0065,
        0x007D, 0x0020, 0x5931, 0x8D25, 0x003A, 0x0020, 0x007B, 0x007D,
    });
}

// alreadyYours <- system_proxy/terminal.rs:-1
// ASCII 骨架: "{name} {existing}"
QString alreadyYours()
{
    return fromCodePoints({
        0x007B, 0x006E, 0x0061, 0x006D, 0x0065, 0x007D, 0x0020, 0x5DF2,
        0x7ECF, 0x662F, 0x4F60, 0x81EA, 0x5DF1, 0x8BBE, 0x7F6E, 0x7684,
        0x503C, 0xFF08, 0x007B, 0x0065, 0x0078, 0x0069, 0x0073, 0x0074,
        0x0069, 0x006E, 0x0067, 0x007D, 0xFF09, 0xFF0C, 0x4E3A, 0x907F,
        0x514D, 0x8986, 0x76D6, 0x7EC8, 0x7AEF, 0x4EE3, 0x7406, 0x914D,
        0x7F6E, 0xFF0C, 0x8BF7, 0x5148, 0x624B, 0x52A8, 0x6E05, 0x6389,
        0x518D, 0x5F00, 0x542F,
    });
}

// windowsOnly <- system_proxy/terminal.rs:-1
// ASCII 骨架: " Windows "
QString windowsOnly()
{
    return fromCodePoints({
        0x4EC5, 0x0020, 0x0057, 0x0069, 0x006E, 0x0064, 0x006F, 0x0077,
        0x0073, 0x0020, 0x652F, 0x6301, 0x5199, 0x5165, 0x7EC8, 0x7AEF,
        0x4EE3, 0x7406, 0x73AF, 0x5883, 0x53D8, 0x91CF,
    });
}

// noProxyValue <- system_proxy/terminal.rs:-1
// ASCII 骨架: "localhost;127.0.0.1;::1;<local>;192.168.*;10.*;172.16.*;172.17.*;172.18.*;172.19.*;172.20.*;172.21.*;172.22.*;172.23.*;172.24.*;172.25.*;172.26.*;172.27.*;172.28.*;172.29.*;172.30.*;172.31.*"
QString noProxyValue()
{
    return QStringLiteral("localhost;127.0.0.1;::1;<local>;192.168.*;10.*;172.16.*;172.17.*;172.18.*;172.19.*;172.20.*;172.21.*;172.22.*;172.23.*;172.24.*;172.25.*;172.26.*;172.27.*;172.28.*;172.29.*;172.30.*;172.31.*");
}

}

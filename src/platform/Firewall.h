#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "core/Result.h"

// Windows 防火墙"允许应用"规则的自动添加，对应 Rust src/platform/firewall.rs。
//
// 为什么需要：这台机上实测出现过"入站到未放行程序的环回连接被防火墙拦掉"——表现成
// `127.0.0.1:445/135` 超时、而 LAN 正常。对这个软件来说更直接：主程序和内核 exe 都要在
// 127.0.0.1 上监听（mixed 9980 / controller 9090），一旦被拦就是"启动代理没反应"。
// 而每次重新编译/更新内核，exe 内容变了但路径不变，所以规则要按路径加才不会被更新打断。
// 加规则需要管理员权限，非提权时只报告不动作。
namespace Platform::Firewall {

/// 规则名前缀，便于识别与幂等检查。对应 Rust `RULE_PREFIX`。
QString rulePrefix();

/// `VPN Parser · <file stem>`：只取文件名的主干，与所在目录无关。对应 Rust `rule_name`。
QString ruleName(const QString &programPath);

/// `netsh advfirewall firewall show rule name=<name>` 的参数表（纯函数，便于测试）。
QStringList showRuleArguments(const QString &name);
/// `netsh advfirewall firewall add rule ...` 的参数表：`program=<路径>` 必须是**单个**参数，
/// 路径带空格时也不能拆开（Rust 用 Command::args 传，引号由系统加）。
QStringList addRuleArguments(const QString &name, const QString &programPath);

/// 判据：netsh 的输出里能不能认出这条规则。对应 Rust `rule_exists` 的后半段——
/// 找不到时 netsh 仍返回 0，只能看输出里有没有这条规则的名字。
/// Rust 用 `String::from_utf8_lossy`，这里保持同一解码方式（不修成 GBK），行为逐字对齐。
bool ruleExistsFromOutput(const QByteArray &stdOut, const QString &name);
/// 跑 netsh 查一次（只读，不修改任何规则）。
bool ruleExists(const QString &programPath);

/// 按路径加一条"入站允许"规则（已存在就跳过）。返回一句给人看的报告。
Result<QString> ensureAllowed(const QString &programPath);

/// 一批程序；单个失败不影响其余（比如某个内核还没下载）。
Result<QString> ensureAll(const QStringList &programPaths);

/// 纯判据：这个路径要不要处理（Rust 里是 `if !program.exists() { continue; }`）。
bool shouldSkipMissing(const QString &programPath);

/// 纯函数：`{stem}: {error}` 形式的失败条目，对应 Rust 的 `{:?}` 文件名单元组打印。
QString failureEntry(const QString &programPath, const QString &error);

}

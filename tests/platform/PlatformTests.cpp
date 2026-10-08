#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

#include "platform/Firewall.h"
#include "platform/Platform.h"
#include "platform/ProcessControl.h"
#include "platform/SingleInstance.h"
#include "platform/Tray.h"
#include "platform/WindowHelpers.h"

using namespace Platform;

// 这些测试全部是纯函数或只读系统查询：不加防火墙规则、不弹 UAC、不杀进程、不显示托盘图标。
// 搬 Rust 的 #[test]：src/platform/mod.rs 的 6 条、src/platform/firewall.rs 的 1 条，
// 以及 tray.rs 那条生命周期测试（见下面那条测试的注释：图标不能真的显示出来）。

// ---- Rust mod.rs: quotes_windows_arguments_with_spaces_and_trailing_slashes ----
TEST(PlatformProcessControl, QuotesWindowsArgumentsWithSpacesAndTrailingSlashes)
{
    EXPECT_EQ(quoteWindowsArgument(QStringLiteral("plain")), QStringLiteral("plain"));
    EXPECT_EQ(quoteWindowsArgument(QString()), QStringLiteral("\"\""));
    EXPECT_EQ(quoteWindowsArgument(QStringLiteral("D:\\proxy data\\")),
              QStringLiteral("\"D:\\proxy data\\\\\""));
}

// ---- Rust mod.rs: escapes_quotes_for_windows_command_line ----
TEST(PlatformProcessControl, EscapesQuotesForWindowsCommandLine)
{
    EXPECT_EQ(quoteWindowsArgument(QStringLiteral("a\"b")), QStringLiteral("\"a\\\"b\""));
}

TEST(PlatformProcessControl, CommandLineJoinsQuotedArgumentsWithSpaces)
{
    // 交接命令行：base64url 值可能以 '-' 开头，但绝不能被加引号或拆开。
    const QStringList args = { elevatedProxyConfigFlag(), QStringLiteral("eyJtb2RlIjoidHVuIn0"),
                               QStringLiteral("--start-proxy-node"), QStringLiteral("db022bb2") };
    EXPECT_EQ(commandLineForArguments(args),
              QStringLiteral("--elevated-proxy-config eyJtb2RlIjoidHVuIn0 --start-proxy-node db022bb2"));
}

// ---- Rust mod.rs: handoff_drops_args_the_new_process_replies_itself ----
TEST(PlatformProcessControl, HandoffDropsArgsTheNewProcessRepliesItself)
{
    const QStringList current = { QStringLiteral("--start-proxy-node"),
                                  QStringLiteral("db022bb2-3d57"), QStringLiteral("--db"),
                                  QStringLiteral("data/vpn-parser.db") };
    const QStringList extra = { elevatedProxyConfigFlag(), QStringLiteral("eyJtb2RlIjoidHVuIn0"),
                                QStringLiteral("--start-proxy-node"),
                                QStringLiteral("aaaaaaaa-new") };
    const QStringList expected = { QStringLiteral("--db"), QStringLiteral("data/vpn-parser.db"),
                                  elevatedProxyConfigFlag(), QStringLiteral("eyJtb2RlIjoidHVuIn0"),
                                  QStringLiteral("--start-proxy-node"),
                                  QStringLiteral("aaaaaaaa-new") };
    EXPECT_EQ(elevatedArguments(current, extra), expected);
}

// ---- Rust mod.rs: handoff_keeps_unrelated_flags_including_equals_forms ----
TEST(PlatformProcessControl, HandoffKeepsUnrelatedFlagsIncludingEqualsForms)
{
    const QStringList current = { QStringLiteral("--db=data/a.db"), QStringLiteral("--api-token"),
                                  QStringLiteral("t0"), QStringLiteral("--start-proxy-node=x") };
    const QStringList expected = { QStringLiteral("--db=data/a.db"), QStringLiteral("--api-token"),
                                   QStringLiteral("t0") };
    EXPECT_EQ(elevatedArguments(current, {}), expected);
}

TEST(PlatformProcessControl, HandoffOwnedArgsMatchRustList)
{
    // Rust: const HANDOFF_OWNED_ARGS: &[&str] = &["--start-proxy-node", "--elevated-proxy-config"];
    EXPECT_EQ(handoffOwnedArgs(),
              (QStringList { QStringLiteral("--start-proxy-node"), elevatedProxyConfigFlag() }));
    EXPECT_EQ(elevatedProxyConfigFlag(), QStringLiteral("--elevated-proxy-config"));
}

// 提权交接的 base64url 必须与 Rust base64 0.22 的 URL_SAFE_NO_PAD 逐字节相同：
// 机器上装的 Rust 实例可能把交接值交给这个 C++ 实例，反之亦然。
TEST(PlatformProcessControl, HandoffBlobRoundTripIsByteCompatibleWithRustBase64Url)
{
    const QByteArray payload = "{\"mode\":\"tun\"}";
    // Rust 测试里出现过的同一个值：URL_SAFE_NO_PAD.encode(br#"{"mode":"tun"}"#)
    EXPECT_EQ(encodeHandoffBlob(payload), QStringLiteral("eyJtb2RlIjoidHVuIn0"));

    const Result<QByteArray> decoded = decodeHandoffBlob(QStringLiteral("eyJtb2RlIjoidHVuIn0"));
    ASSERT_TRUE(decoded.isOk());
    EXPECT_EQ(decoded.value(), payload);

    // 0xFB 0xFF 在标准字母表是 "+/8="，URL_SAFE_NO_PAD 必须是 "-_8"（无填充）。
    const QByteArray tricky = QByteArray::fromRawData("\xFB\xFF", 2);
    EXPECT_EQ(encodeHandoffBlob(tricky), QStringLiteral("-_8"));
    const Result<QByteArray> back = decodeHandoffBlob(QStringLiteral("-_8"));
    ASSERT_TRUE(back.isOk());
    EXPECT_EQ(back.value(), tricky);

    // 无填充引擎拒绝带 '=' 的值，和 Rust URL_SAFE_NO_PAD 一致。
    EXPECT_TRUE(decodeHandoffBlob(QStringLiteral("eyJtb2RlIjoidHVuIn0=")).isErr());
}

TEST(PlatformProcessControl, HandoffPollPlanIsThirtyAttemptsOfOneHundredMilliseconds)
{
    // Rust main.rs：`for attempt in 1..=30` + `sleep(100ms)`，最坏情况 3 秒。
    const QVector<HandoffPollStep> plan = handoffPollPlan();
    ASSERT_EQ(plan.size(), kHandoffMaxAttempts);
    EXPECT_EQ(plan.first().attempt, 1);
    EXPECT_EQ(plan.last().attempt, kHandoffMaxAttempts);
    int totalMs = 0;
    for (const HandoffPollStep &step : plan) {
        EXPECT_EQ(step.sleepMsAfterFailure, kHandoffAttemptDelayMs);
        totalMs += step.sleepMsAfterFailure;
    }
    EXPECT_EQ(totalMs, 3000);
}

TEST(PlatformProcessControl, OrphanCoreImageNamesComeFromKernelExecutableNames)
{
    // Rust 写死 ["mihomo.exe", "sing-box.exe", "xray.exe"]；这边由 Kernel::kindExecutableName 推导。
    const QStringList names = orphanCoreImageNames();
    ASSERT_EQ(names.size(), 3);
    EXPECT_TRUE(names.contains(QStringLiteral("mihomo.exe")));
    EXPECT_TRUE(names.contains(QStringLiteral("sing-box.exe")));
    EXPECT_TRUE(names.contains(QStringLiteral("xray.exe")));

    EXPECT_TRUE(isOrphanProxyCoreImage(QStringLiteral("MIHOMO.EXE")));
    EXPECT_TRUE(isOrphanProxyCoreImage(QStringLiteral("xray.exe")));
    EXPECT_FALSE(isOrphanProxyCoreImage(QStringLiteral("vpn-parser.exe")));
    EXPECT_FALSE(isOrphanProxyCoreImage(QStringLiteral("mihomo")));

    EXPECT_TRUE(isDuplicateAppImage(QStringLiteral("vpn-parser.exe")));
    EXPECT_TRUE(isDuplicateAppImage(QStringLiteral("VPN-Parser.EXE")));
    EXPECT_FALSE(isDuplicateAppImage(QStringLiteral("vcc.exe")));
}

// ---- Rust mod.rs: job_creation_does_not_capture_the_app_process ----
TEST(PlatformProcessControl, JobCreationDoesNotCaptureTheAppProcess)
{
    // 这条守卫的是"TUN 提权交接后父实例能不能自己退掉"：主进程一旦被圈进
    // KILL_ON_JOB_CLOSE 的作业，交接出去的提权实例会算在同一个作业里，父进程退出
    // 就连带打死提权实例和它的内核（实测踩过，9980 当场变死端口）。
    // 若这里失败，说明测试运行时本身就被包在某个作业里 —— 那要改写这条判据，
    // 别直接删掉。
    ensureJobInitialized();
    EXPECT_FALSE(currentProcessInOurJob())
        << "主进程被圈进作业对象，提权交接实例会被连带杀死";
}

TEST(PlatformProcessControl, ProcessSnapshotIsReadOnlyAndSeesItself)
{
    // snapshotProcesses 只做 ToolHelp 快照；杀进程的路径不在测试里碰。
    const QVector<ProcessEntry> entries = snapshotProcesses();
    EXPECT_FALSE(entries.isEmpty());
    const qint64 self = QCoreApplication::applicationPid();
    bool foundSelf = false;
    for (const ProcessEntry &entry : entries) {
        if (entry.pid == static_cast<quint32>(self)) {
            foundSelf = true;
        }
    }
    EXPECT_TRUE(foundSelf);
}

// ---- Rust mod.rs: named_mutex_allows_only_one_gui_instance ----
TEST(PlatformSingleInstance, MutexNameIsTheFixedLocalSessionGuid)
{
    // Rust 的 SINGLE_INSTANCE_MUTEX_NAME 是写死的，不含 exe 路径：Rust 建的实例和本程序
    // 必须抢同一把锁，否则两个实例会同时监听 9980。
    EXPECT_EQ(singleInstanceMutexName(),
              QStringLiteral("Local\\VPNParser-7F61E55C-DB54-49C6-AB9E-986AEBFDD56B"));
}

TEST(PlatformSingleInstance, NamedMutexAllowsOnlyOneGuiInstance)
{
    const QString name = QStringLiteral("Local\\VpnParserTest-%1-%2")
                             .arg(QCoreApplication::applicationPid())
                             .arg(QDateTime::currentMSecsSinceEpoch());

    Result<std::optional<SingleInstanceGuard>> first = acquireSingleInstanceNamed(name);
    ASSERT_TRUE(first.isOk()) << first.error().toStdString();
    ASSERT_TRUE(first.value().has_value());

    Result<std::optional<SingleInstanceGuard>> second = acquireSingleInstanceNamed(name);
    ASSERT_TRUE(second.isOk()) << second.error().toStdString();
    EXPECT_FALSE(second.value().has_value());

    // 释放（guard 析构 → CloseHandle）之后可以重新拿到：提权交接就靠这一步。
    first->reset();
    Result<std::optional<SingleInstanceGuard>> third = acquireSingleInstanceNamed(name);
    ASSERT_TRUE(third.isOk()) << third.error().toStdString();
    EXPECT_TRUE(third.value().has_value());
}

// ---- Rust firewall.rs: rule_names_are_stable_and_prefixed ----
TEST(PlatformFirewall, RuleNamesAreStableAndPrefixed)
{
    const QString name = Firewall::ruleName(QStringLiteral("D:\\software\\VPN Parser\\vpn-parser.exe"));
    EXPECT_TRUE(name.startsWith(Firewall::rulePrefix())) << "规则名要能认出来";
    // 路径不变、exe 变了（每次重编译）→ 规则名也不变，这正是"按路径放行"的意义。
    EXPECT_EQ(name, Firewall::ruleName(QStringLiteral("E:\\other\\place\\vpn-parser.exe")));
    EXPECT_TRUE(Firewall::ruleName(QStringLiteral("/x/mihomo.exe")).contains(QStringLiteral("mihomo")));
    // "VPN Parser" + " · " + stem：分隔符是 U+00B7，两边各一个空格。
    EXPECT_EQ(name.size(), 23);
    EXPECT_EQ(name.at(11), QChar(0x00B7));
    EXPECT_EQ(Firewall::ruleName(QStringLiteral("no-suffix")), QStringLiteral("VPN Parser · no-suffix"));
    EXPECT_EQ(Firewall::ruleName(QStringLiteral("D:/x/")), QStringLiteral("VPN Parser · program"));
}

TEST(PlatformFirewall, NetshArgumentsKeepProgramPathAsOneToken)
{
    const QString path = QStringLiteral("D:\\software\\VPN Parser\\vpn-parser.exe");
    const QString name = Firewall::ruleName(path);

    EXPECT_EQ(Firewall::showRuleArguments(name),
              (QStringList { QStringLiteral("advfirewall"), QStringLiteral("firewall"),
                             QStringLiteral("show"), QStringLiteral("rule"),
                             QStringLiteral("name=%1").arg(name) }));

    const QStringList args = Firewall::addRuleArguments(name, path);
    ASSERT_EQ(args.size(), 10);
    EXPECT_EQ(args.at(4), QStringLiteral("name=%1").arg(name));
    // `program=<带空格的路径>` 是单个参数，不在这里手工加引号：Rust 交给 Command 拼装，
    // 这边交给 SysProxy::runHidden（QProcess）拼装。
    EXPECT_EQ(args.at(5), QStringLiteral("program=%1").arg(path));
    EXPECT_EQ(args.mid(6),
              (QStringList { QStringLiteral("dir=in"), QStringLiteral("action=allow"),
                             QStringLiteral("enable=yes"), QStringLiteral("profile=any") }));
}

TEST(PlatformFirewall, ExistsCheckUsesLossyUtf8SoNonAsciiSeparatorNeverMatches)
{
    const QString path = QStringLiteral("D:\\software\\VPN Parser\\vpn-parser.exe");
    const QString name = Firewall::ruleName(path);

    // netsh 真的用 UTF-8 输出时能认出来。
    EXPECT_TRUE(Firewall::ruleExistsFromOutput(name.toUtf8(), name));
    EXPECT_FALSE(Firewall::ruleExistsFromOutput(QByteArrayLiteral("Name : Not mine"), name));

    // 中文系统的 netsh 输出是 GBK：U+00B7 变成 A1 A4，按 from_utf8_lossy 解出 U+FFFD，
    // 于是"已存在就跳过"实际上认不出来 —— 这就是 Rust 的真实行为（规则每次都会被重新 add，
    // 包括 exe 路径换掉的那次）。这边逐字照抄，不顺手"修好"，否则判据就和 Rust 不一致了。
    QByteArray gbk;
    gbk += name.left(11).toUtf8();
    gbk.append('\xA1');
    gbk.append('\xA4');
    gbk += name.mid(12).toUtf8();
    EXPECT_FALSE(Firewall::ruleExistsFromOutput(gbk, name));
}

TEST(PlatformFirewall, FailureEntriesAndMissingPathsFollowRust)
{
    // Rust: format!("{:?}: {error}", program.file_name().unwrap_or_default())
    EXPECT_EQ(Firewall::failureEntry(QStringLiteral("D:\\kernels\\mihomo.exe"),
                                    QStringLiteral("boom")),
              QStringLiteral("\"mihomo.exe\": boom"));
    // 路径不存在不算失败，直接跳过（`if !program.exists() { continue; }`）。
    EXPECT_TRUE(Firewall::shouldSkipMissing(QStringLiteral("D:\\definitely\\not\\here.exe")));
    // 一个真的存在的路径不会被跳过。用当前工作目录而不是 applicationFilePath()：
    // gtest 里没有 QCoreApplication 实例，后者会是空串。
    EXPECT_FALSE(Firewall::shouldSkipMissing(QDir().absolutePath()));
}

// ---- Rust tray.rs: 常量与判据 ----
TEST(PlatformTray, WireConstantsMatchRust)
{
    EXPECT_EQ(trayWindowClass(), QStringLiteral("VPNParserTrayWindowClass"));
    EXPECT_EQ(mainWindowTitle(), QStringLiteral("VPN Parser"));
    EXPECT_EQ(wakeupMessageName(), QStringLiteral("VPN_PARSER_SHOW_WINDOW_MSG"));
    EXPECT_EQ(wakeupInstanceId(), 0x400u + 101u);   // WM_USER + 101
    EXPECT_EQ(trayCallbackMessage(), 0x400u + 100u); // WM_USER + 100
    EXPECT_EQ(trayIconId(), 1001u);
    EXPECT_EQ(cmdShow(), 2001u);
    EXPECT_EQ(cmdToggleProxy(), 2002u);
    EXPECT_EQ(cmdExit(), 2003u);
}

TEST(PlatformTray, MenuItemsFollowRustOrderAndLabels)
{
    const QVector<TrayMenuItem> stopped = menuItemsFor(false);
    ASSERT_EQ(stopped.size(), 4);
    EXPECT_EQ(stopped.at(0).id, cmdShow());
    EXPECT_EQ(stopped.at(0).text.size(), 5); // 「显示主界面」
    EXPECT_FALSE(stopped.at(0).separator);
    EXPECT_EQ(stopped.at(1).id, cmdToggleProxy());
    EXPECT_EQ(stopped.at(1).text.size(), 4); // 「启动代理」
    EXPECT_TRUE(stopped.at(2).separator);
    EXPECT_EQ(stopped.at(2).id, 0u);
    EXPECT_EQ(stopped.at(3).id, cmdExit());
    EXPECT_EQ(stopped.at(3).text.size(), 4); // 「退出程序」

    // 运行中只换第二条的文字（停止代理），顺序与 id 不变。
    const QVector<TrayMenuItem> running = menuItemsFor(true);
    ASSERT_EQ(running.size(), 4);
    EXPECT_EQ(running.at(1).id, cmdToggleProxy());
    EXPECT_EQ(running.at(1).text.size(), 4);
    EXPECT_NE(running.at(1).text, stopped.at(1).text);
    EXPECT_EQ(running.at(0).text, stopped.at(0).text);
    EXPECT_EQ(running.at(3).text, stopped.at(3).text);
}

TEST(PlatformTray, MenuCommandMapsToShowToggleQuit)
{
    EXPECT_EQ(trayEventForCommand(cmdShow()), TrayEvent::ShowWindow);
    EXPECT_EQ(trayEventForCommand(cmdToggleProxy()), TrayEvent::ToggleProxy);
    EXPECT_EQ(trayEventForCommand(cmdExit()), TrayEvent::Quit);
    // TrackPopupMenu 返回 0（点了外面）时 Rust 走 `_ => {}`。
    EXPECT_FALSE(trayEventForCommand(0).has_value());
    EXPECT_FALSE(trayEventForCommand(9999).has_value());
}

TEST(PlatformTray, ClickEventPredicatesFollowRust)
{
    EXPECT_TRUE(isTraySelectEvent(0x0202u));  // WM_LBUTTONUP
    EXPECT_TRUE(isTraySelectEvent(0x0203u));  // WM_LBUTTONDBLCLK
    EXPECT_TRUE(isTraySelectEvent(0x0201u));  // WM_LBUTTONDOWN
    EXPECT_TRUE(isTraySelectEvent(0x0400u));  // NIN_SELECT  == WM_USER + 0
    EXPECT_TRUE(isTraySelectEvent(0x0401u));  // NIN_KEYSELECT == WM_USER + 1
    EXPECT_FALSE(isTraySelectEvent(0x0204u)); // WM_RBUTTONUP 的按下：不算选中

    EXPECT_TRUE(isTrayContextEvent(0x0205u));  // WM_RBUTTONUP
    EXPECT_TRUE(isTrayContextEvent(0x007Bu));  // WM_CONTEXTMENU
    // 只认抬起：以前连 WM_RBUTTONDOWN(0x0204) 也弹一次，按下那层被抬起顶掉，
    // 看起来就是"点了没反应"。
    EXPECT_FALSE(isTrayContextEvent(0x0204u));
}

TEST(PlatformTray, TooltipTextAndCapacityFollowRust)
{
    // Rust 写死的默认提示 "VPN Parser - 网络代理控制台"：20 个字符，前 13 个是 ASCII。
    const QString tip = defaultTrayTooltip();
    EXPECT_EQ(tip.size(), 20);
    EXPECT_EQ(tip.left(13), QStringLiteral("VPN Parser - "));
    // szTip 在现代 SDK 是 WCHAR[128]，Rust 用 szTip.len() - 1 截断。
    EXPECT_EQ(trayTipCapacity(), 127);
    const QString long_ = QString(200, QLatin1Char('x'));
    EXPECT_EQ(truncateTrayTip(long_).size(), trayTipCapacity());
    EXPECT_EQ(truncateTrayTip(QStringLiteral("abc")), QStringLiteral("abc"));
}

TEST(PlatformTray, WindowEnumerationPredicates)
{
    EXPECT_TRUE(skipsWindowClass(QStringLiteral("Qt6QWindowWindow")));
    EXPECT_FALSE(skipsWindowClass(trayWindowClass()));
    // "本来就看得见，别碰"：可见且非最小化。
    EXPECT_TRUE(windowIsAlreadyShown(true, false));
    EXPECT_FALSE(windowIsAlreadyShown(true, true));   // 最小化的要掀
    EXPECT_FALSE(windowIsAlreadyShown(false, false)); // 隐藏的要掀
}

// ---- Rust tray.rs: tray_handle_lifecycle_and_updates ----
TEST(PlatformTray, TrayHandleLifecycleAndUpdatesWithoutShowingAnIcon)
{
    // Rust 的这条测试用 TrayHandle::new()，它会立刻起线程并 NIM_ADD —— 图标真的会闪一下。
    // 本项目的安全红线是"构建/测试期间不许往通知区域塞图标"，所以 start() 被拆成显式调用，
    // 这里断言的是同一个可观察契约：set_waker / set_proxy_running / set_tooltip 都不报错，
    // try_recv_event 在没有事件时返回 None，shutdown() 幂等。
    TrayHandle tray;
    tray.setWaker([] {});
    tray.setProxyRunning(true);
    tray.setTooltip(QStringLiteral("VPN Parser Test"));
    EXPECT_FALSE(tray.isStarted());
    EXPECT_FALSE(tray.tryRecvEvent().has_value());
    tray.shutdown();
    tray.shutdown();
    EXPECT_FALSE(tray.tryRecvEvent().has_value());
}

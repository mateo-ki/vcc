import fs from 'node:fs';
import path from 'node:path';

// 从 Rust 源里抽取 geo.rs / ip_risk.rs 的中文提示文本与 ASCII 字符串数组，生成纯 ASCII 的
// C++ 头（header-only，因为 vccGeo 这个 target 只编 Geo.cpp / IpRisk.cpp）：
//   node tools/gen-geo-ip-cjk-codepoints.mjs
//
// 规则与 gen-kernel-cjk-codepoints.mjs 一致，但更严：
//   * ASCII 锚点 + 精确行偏移定位到**唯一一行**，那一行必须有且只有一个候选字面量；
//   * 每条声明期望的 ASCII 骨架（骨架是字面量里非 ASCII 字符之外的那些字节，手打是安全的），
//     对不上就报错退出并把期望/实际都打出来 —— 锚点认错行会立刻暴露，而不是静默抄错文案。
//   * 生成时打印每条的长度、ASCII 骨架和 UTF-8 hex（测试里用 hex，不再抄一遍中文）。
const RUST = 'D:/project/rust/vs/vpn-parser/src';

const outputs = {
  geo: {
    header: null, // 另一个 agent 已经在写 src/geo/GeoMessages.*，这里只校对文案，不落文件。
    ns: 'Geo::Messages',
    title: '对应 Rust src/geo.rs 里的用户可见文本与源列表。',
  },
  ip: {
    header: 'D:/project/cpp/vcc/src/ip/IpRiskMessages.h',
    ns: 'IpRisk::Messages',
    title: '对应 Rust src/ip_risk.rs 里的用户可见文本与关键字表。',
  },
};

// [name, file, anchor, occurrence, offset, expectedAsciiSkeleton, target, mode]
const entries = [
  // ---- geo.rs：校验与下载报告 ----
  ['tooSmall', 'geo.rs', 'bytes.len()));', 0, 0, ' {} ', 'geo'],
  ['htmlBody', 'geo.rs', 'if bytes.starts_with(b"<")', 0, 1, ' HTML', 'geo'],
  ['badMagic', 'geo.rs', 'if bytes[0] != 0x0a', 0, 1, ' geo  {:#04x}', 'geo'],
  ['alreadyReady', 'geo.rs', 'human_mb(existing.unwrap_or(0))', 0, 0, '{name}  {}', 'geo'],
  ['updatedOk', 'geo.rs', 'report.push(format!(', 1, 1, '{name}  {} {}', 'geo'],
  ['allSourcesFailed', 'geo.rs', 'if !done {', 0, 2, ' {name} {}', 'geo'],
  ['failureJoiner', 'geo.rs', 'failures.join(', 0, 0, '', 'geo'],
  ['reportJoiner', 'geo.rs', 'report.join(', 0, 0, '', 'geo'],
  // ---- ip_risk.rs：摘要、判决与错误文案 ----
  ['kindDatacenter', 'ip_risk.rs', 'let kind = if self.datacenter {', 0, 1, '/', 'ip'],
  ['kindResidential', 'ip_risk.rs', 'let kind = if self.datacenter {', 0, 3, '/ISP?', 'ip'],
  ['hostnameArrow', 'ip_risk.rs', 'self.hostname, self.ip)', 0, 0, '{}  {}', 'ip'],
  ['unknownRegion', 'ip_risk.rs', 'if self.country.is_empty() {', 0, 1, '', 'ip'],
  ['noAsnInfo', 'ip_risk.rs', 'if self.org.is_empty() {', 0, 1, ' ASN ', 'ip'],
  ['headlineJoiner', 'ip_risk.rs', '.join("', 0, 0, '  ', 'ip'],
  ['parseFailed', 'ip_risk.rs', 'serde_json::from_str(body)', 0, 0, ' IP : {error}', 'ip'],
  ['verdictDatacenter', 'ip_risk.rs', 'let verdict = if datacenter {', 0, 1,
   '/AI OpenAI / Google / Claude ASN ', 'ip'],
  ['verdictPublicProxy', 'ip_risk.rs', '} else if public_proxy {', 0, 1, ' / VPN ', 'ip'],
  ['verdictResidential', 'ip_risk.rs', 'let verdict = if datacenter {', 0, 6, '/ISP ', 'ip'],
  ['resolveFailed', 'ip_risk.rs', 'let addrs = tokio::net::lookup_host', 0, 2,
   ' {host} : {error}', 'ip'],
  ['noRecords', 'ip_risk.rs', '.collect::<Vec<_>>();', 0, 7, ' {host} ', 'ip'],
  ['queryFailed', 'ip_risk.rs', '.error_for_status()', 0, -1, '{what} : {error:#}', 'ip'],
  ['readFailed', 'ip_risk.rs', '.text()', 0, 2, '{what} : {error:#}', 'ip'],
  ['invalidProxy', 'ip_risk.rs', 'reqwest::Proxy::all(proxy_url)', 0, 0, ': {error}', 'ip'],
  ['buildProxyClientFailed', 'ip_risk.rs', 'reqwest::Client::builder()', 0, 4, ': {error}', 'ip'],
  ['noEgressAddress', 'ip_risk.rs', 'let mut last_error = String::from(', 0, 0, '', 'ip'],
  ['egressLabel', 'ip_risk.rs', 'match fetch_text(&proxied, url, ', 0, 0, ' IP', 'ip'],
  ['notAnIp', 'ip_risk.rs', 'last_error = format!(', 0, 0, '{url}  IP : {candidate:?}', 'ip'],
  ['egressFailed', 'ip_risk.rs', 'last_error}', 0, 0, ' IP : {last_error}', 'ip'],
];

// [name, file, startAnchor, startOffset, endAnchor(正则字符串) | null 表示同一行, target, expectedCount]
const listEntries = [
  ['kGeoFiles', 'geo.rs', 'pub const GEO_FILES', 0, null, 'geo', 2],
  ['kGeoUpstreams', 'geo.rs', 'const UPSTREAMS: [&str; 3] = [', 1, '^];', 'geo', 3],
  ['kDatacenterHints', 'ip_risk.rs', 'const DATACENTER_HINTS: &[&str] = &[', 1, '^];', 'ip', 67],
  ['kEgressEchoUrls', 'ip_risk.rs', 'for url in [', 0, null, 'ip', 2],
];

const hasNonAscii = (text) => /[^\x00-\x7f]/.test(text);

// node tools/gen-geo-ip-cjk-codepoints.mjs --print  只报锚点落点与骨架，不写文件（用来核对期望值）
const printOnly = process.argv.includes('--print');

function literalsIn(line) {
  return [...line.matchAll(/r?"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
}

const skeletonOf = (text) => [...text].filter((c) => c.codePointAt(0) < 0x7f).join('');

function fail(message) {
  console.error('FAIL: ' + message);
  process.exit(1);
}

const cache = new Map();
function rustLines(file) {
  if (!cache.has(file)) {
    cache.set(file, fs.readFileSync(RUST + '/' + file, 'utf8').split(/\r?\n/));
  }
  return cache.get(file);
}

function checkLiteral(text, name, lineText, lineNumber) {
  if (text.includes('\\')) {
    fail(`${name}: 字面量里有反斜杠转义，本生成器不猜它的含义：${JSON.stringify(text)}`);
  }
  return text;
}

const buckets = new Map();
for (const target of Object.keys(outputs)) {
  buckets.set(target, { items: [] });
}

for (const [name, file, anchor, occurrence, offset, expectedSkeleton, target] of entries) {
  const lines = rustLines(file);
  const hits = [];
  lines.forEach((line, index) => {
    if (line.includes(anchor)) hits.push(index);
  });
  if (hits.length <= occurrence) {
    fail(`${name}: 锚点 "${anchor}" 只匹配到 ${hits.length} 行，要第 #${occurrence} 行`);
  }
  if (hits.length > occurrence + 1) {
    console.log(`note: ${name} 锚点 "${anchor}" 命中 ${hits.length} 行，取第 #${occurrence}`);
  }
  const index = hits[occurrence] + offset;
  if (index < 0 || index >= lines.length) fail(`${name}: 偏移越界（${index}）`);
  const line = lines[index];
  const found = literalsIn(line).filter(hasNonAscii);
  if (found.length !== 1) {
    fail(`${name}: 第 ${index + 1} 行有 ${found.length} 个非 ASCII 字面量：${JSON.stringify(found)}\n  行内容: ${line.trim()}`);
  }
  const text = checkLiteral(found[0], name, line, index + 1);
  const skeleton = skeletonOf(text);
  if (printOnly) {
    console.log(`${name} | line ${index + 1} | len ${[...text].length} | sk=${JSON.stringify(skeleton)}`);
    buckets.get(target).items.push({ kind: 'text', name, text, file, line: index + 1 });
    continue;
  }
  if (skeleton !== expectedSkeleton) {
    fail(`${name}: ASCII 骨架不符\n  期望 ${JSON.stringify(expectedSkeleton)}\n  实际 ${JSON.stringify(skeleton)}\n  第 ${index + 1} 行: ${line.trim()}\n  字面量: ${JSON.stringify(text)}`);
  }
  buckets.get(target).items.push({ kind: 'text', name, text, file, line: index + 1 });
}

for (const [name, file, anchor, offset, endPattern, target, expectedCount] of listEntries) {
  const lines = rustLines(file);
  const hits = [];
  lines.forEach((line, index) => {
    if (line.includes(anchor)) hits.push(index);
  });
  if (hits.length !== 1) {
    fail(`${name}: 锚点 "${anchor}" 命中 ${hits.length} 行，要恰好 1 行`);
  }
  const start = hits[0] + offset;
  let end = start;
  if (endPattern === null) {
    end = start;
  } else {
    const terminator = new RegExp(endPattern);
    end = -1;
    for (let index = start; index < lines.length; index += 1) {
      if (terminator.test(lines[index])) {
        end = index - 1;
        break;
      }
    }
    if (end < 0) fail(`${name}: 找不到结束标记 ${endPattern}`);
  }
  const collected = [];
  for (let index = start; index <= end; index += 1) {
    for (const literal of literalsIn(lines[index])) {
      collected.push(checkLiteral(literal, name, lines[index], index + 1));
    }
  }
  if (collected.length !== expectedCount) {
    fail(`${name}: 取到 ${collected.length} 个字面量，期望 ${expectedCount} 个\n  ${JSON.stringify(collected)}`);
  }
  for (const text of collected) {
    if (hasNonAscii(text)) fail(`${name}: 数组里有非 ASCII 成员 ${JSON.stringify(text)}`);
  }
  buckets.get(target).items.push({ kind: 'list', name, texts: collected, file, line: start + 1 });
}

const report = [];
for (const [target, config] of Object.entries(outputs)) {
  const items = buckets.get(target).items;
  const body = [
    '// 由 tools/gen-geo-ip-cjk-codepoints.mjs 从 Rust 源生成，请勿手改。',
    '// ' + config.title,
    '// 这些文本要与 vpn-parser 的 Rust 实现逐字一致（错误消息会进界面和日志）；',
    '// 模板里的 {} / {name} / {error} 等占位符原样保留，由调用方按 Rust 的 format! 语义替换。',
    '#pragma once',
    '',
    '#include <QString>',
    '#include <QStringList>',
    '',
    '#include <initializer_list>',
    '',
    `namespace ${config.ns} {`,
    '',
    'namespace detail {',
    'inline QString fromCodePoints(std::initializer_list<char32_t> points)',
    '{',
    '    return QString::fromUcs4(points.begin(), qsizetype(points.size()));',
    '}',
    '}',
    '',
  ];
  for (const item of items) {
    if (item.kind === 'list') {
      body.push(`// ${item.name} <- ${item.file}:${item.line}（${item.texts.length} 项，逐字照抄）`);
      body.push(`inline const QStringList ${item.name} = {`);
      for (let index = 0; index < item.texts.length; index += 8) {
        const chunk = item.texts.slice(index, index + 8).map((text) => `QStringLiteral("${text}")`);
        body.push('    ' + chunk.join(', ') + (index + 8 < item.texts.length ? ',' : ''));
      }
      body.push('};');
      body.push('');
      report.push({ target, name: item.name, chars: item.texts.length, asciiSkeleton: item.texts.join('|'), utf8Hex: '' });
      continue;
    }
    const chars = [...item.text];
    const skeleton = skeletonOf(item.text);
    body.push(`// ${item.name} <- ${item.file}:${item.line}`);
    body.push(`// 长度 ${chars.length}，ASCII 骨架: ${JSON.stringify(skeleton)}`);
    body.push(`// UTF-8 hex: ${Buffer.from(item.text, 'utf8').toString('hex')}`);
    body.push(`inline QString ${item.name}()`);
    body.push('{');
    if (hasNonAscii(item.text)) {
      const points = chars.map((ch) => '0x' + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'));
      body.push('    return detail::fromCodePoints({');
      for (let index = 0; index < points.length; index += 8) {
        body.push('        ' + points.slice(index, index + 8).join(', ') + ',');
      }
      body.push('    });');
    } else {
      const escaped = item.text.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
      body.push(`    return QStringLiteral("${escaped}");`);
    }
    body.push('}');
    body.push('');
    report.push({ target, name: item.name, chars: chars.length, asciiSkeleton: skeleton,
                  utf8Hex: Buffer.from(item.text, 'utf8').toString('hex') });
  }
  body.push('}');
  body.push('');
  if (config.header) {
    fs.mkdirSync(path.dirname(config.header), { recursive: true });
    fs.writeFileSync(config.header, body.join('\n'));
    console.log(`${target}: wrote ${items.length} entry item(s) -> ${config.header}`);
  } else {
    console.log(`${target}: ${items.length} item(s) 校验通过（不写文件，src/geo/GeoMessages.* 归另一个 agent）`);
  }
}

console.log(JSON.stringify(report, null, 1));

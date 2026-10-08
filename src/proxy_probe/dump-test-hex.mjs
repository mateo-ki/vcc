import fs from 'node:fs';

// Scratch helper used while writing the probe tests: prints the non-ASCII segments of
// every user-visible literal in proxy_probe.rs (plus the sing-box / xray probe texts) as
// UTF-8 hex, so the tests can spell expectations in pure ASCII.
//   node src/proxy_probe/dump-test-hex.mjs
const RUST = 'D:/project/rust/vs/vpn-parser/src';

const wanted = [
  ['delayOk', 'proxy_probe.rs', 'message: format!("{delay} ms', 0, 0],
  ['delayFailed', 'proxy_probe.rs', 'let result = match outcome {', 0, 11],
  ['delayNoValue', 'proxy_probe.rs', '_ => Err(format!(', 0, 0],
  ['protocolUnsupported', 'proxy_probe.rs', 'nodes_to_clash_proxies(std::slice::from_ref(node))', 0, 5],
  ['configUnsupported', 'proxy_probe.rs', 'message: format!("Mihomo', 1, 0],
  ['batchFailed', 'proxy_probe.rs', 'let (ids, outcome) = joined.map_err', 0, 0],
  ['startupFailed', 'proxy_probe.rs', 'return Err(anyhow!("Mihomo', 0, 0],
  ['spawnFailed', 'proxy_probe.rs', '.with_context(|| format!(', 0, 0],
  ['countMismatch', 'proxy_probe.rs', 'if results.len() != total || results.iter().any(Option::is_none) {', 0, 1],
  ['unknownConfigError', 'proxy_probe.rs', 'let text = diagnostic', 0, 5],
  ['notFound', 'proxy_probe.rs', 'let mihomo = find_mihomo_executable().ok_or_else', 0, 1],
  ['validateTaskFailed', 'proxy_probe.rs', 'let (mut batch, outcome) = joined.map_err', 0, 0],
  ['singBoxRealProxyOk', 'proxy/sing_box.rs', 'message: "Sing-box', 0, 0],
  ['singBoxRealProxyFailed', 'proxy/sing_box.rs', 'message: format!("Sing-box', 2, 0],
  ['singBoxStartupFailed', 'proxy/sing_box.rs', 'return Err(anyhow!("', 0, 0],
  ['singBoxBatchFailed', 'proxy/sing_box.rs', 'Err(error) => results.extend(batch.iter().map(|node| ProxyProbeResult {', 0, 4],
  ['singBoxConfigUnsupported', 'proxy/sing_box.rs', 'Err(error) => immediate.push(ProxyProbeResult {', 0, 4],
  ['xrayRealProxyOk', 'proxy/xray.rs', 'message: "Xray', 0, 0],
  ['xrayRealProxyFailed', 'proxy/xray.rs', 'message: format!("Xray', 2, 0],
  ['xrayBatchStartupFailed', 'proxy/xray.rs', 'let _ = tokio::fs::remove_dir_all(&directory).await;', 0, 2],
  ['xrayBatchFailed', 'proxy/xray.rs', 'Err(error) => results.extend(batch.iter().map(|node| ProxyProbeResult {', 0, 4],
  ['unsupportedShort', 'proxy_probe.rs', 'message.contains(', 0, 0],
];

const literalsIn = (line) => [...line.matchAll(/r?"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
const hex = (text) => Buffer.from(text, 'utf8').toString('hex').toUpperCase();

const cache = new Map();
const linesOf = (file) => {
  if (!cache.has(file)) cache.set(file, fs.readFileSync(RUST + '/' + file, 'utf8').split(/\r?\n/));
  return cache.get(file);
};

for (const [name, file, anchor, occurrence, offset] of wanted) {
  const lines = linesOf(file);
  const hits = [];
  lines.forEach((line, index) => {
    if (line.includes(anchor)) hits.push(index);
  });
  const line = lines[hits[occurrence] + offset];
  const found = literalsIn(line ?? '').filter((t) => /[^\x00-\x7f]/.test(t));
  const text = found[0] ?? '';
  const segments = text.match(/[^\x00-\x7f]+/g) ?? [];
  console.log(`// ${name} (${file}:${hits[occurrence] + offset + 1}) = ${JSON.stringify(text)}`);
  for (const segment of segments) {
    console.log(`//   ${hex(segment)}`);
  }
}

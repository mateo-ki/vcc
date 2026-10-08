import fs from 'node:fs';

const path = 'D:/project/rust/vs/vpn-parser/src/proxy/mod.rs';
const lines = fs.readFileSync(path, 'utf8').split(/\r?\n/);

function literalsOf(text) {
  return [...text.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
}

function describe(label, text) {
  const points = Array.from(text).map(
    (c) => '0x' + c.codePointAt(0).toString(16).toUpperCase().padStart(4, '0'),
  );
  console.log(label + ': len=' + points.length + ' -> ' + points.join(', '));
  console.log('  utf8 hex: ' + Buffer.from(text, 'utf8').toString('hex'));
}

for (let i = 0; i < lines.length; i += 1) {
  const line = lines[i];
  if (line.includes('URL_TEST_GROUP: &str')) {
    describe('URL_TEST_GROUP', literalsOf(line)[0]);
  }
  if (line.includes('FINAL_GROUP: &str')) {
    describe('FINAL_GROUP', literalsOf(line)[0]);
  }
  if (line.includes('fn is_node_connectivity_failure')) {
    const body = lines.slice(i + 1, i + 3).join('\n');
    const found = literalsOf(body);
    if (found.length) describe('CONNECTIVITY_TEXT', found[0]);
  }
  if (line.includes('message: "')) {
    describe('STOPPED_MESSAGE', literalsOf(line)[0]);
  }
}

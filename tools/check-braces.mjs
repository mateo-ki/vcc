import fs from 'node:fs';

const file = process.argv[2];
const lines = fs.readFileSync(file, 'utf8').split(/\r?\n/);
let depth = 0;
let inBlock = false;
lines.forEach((line, index) => {
  const before = depth;
  let i = 0;
  let inString = false;
  while (i < line.length) {
    const c = line[i];
    if (!inBlock && !inString && c === '/' && line[i + 1] === '/') {
      break; // 行注释
    }
    if (!inBlock && !inString && c === '/' && line[i + 1] === '*') {
      inBlock = true;
      i += 2;
      continue;
    }
    if (inBlock && c === '*' && line[i + 1] === '/') {
      inBlock = false;
      i += 2;
      continue;
    }
    if (inBlock) {
      i += 1;
      continue;
    }
    if (c === '"') {
      inString = !inString;
      i += 1;
      continue;
    }
    if (inString) {
      if (c === '\\') i += 1;
      i += 1;
      continue;
    }
    if (c === '{') depth += 1;
    if (c === '}') depth -= 1;
    i += 1;
  }
  if (index < 6 || depth !== before) {
    console.log(String(index + 1).padStart(4) + ' depth=' + depth + ' | ' + line.trim().slice(0, 70));
  }
});
console.log('final depth (0 = balanced):', depth, 'blockCommentOpen:', inBlock);

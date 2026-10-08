import { DatabaseSync } from 'node:sqlite';
import fs from 'node:fs';

const dir = 'D:/project/rust/vs/vpn-parser/data/subscriptions';
const files = fs.readdirSync(dir).filter((f) => f.endsWith('.db'));
console.log('shard dbs:', files.length);
const picked = files.slice(0, 2);
for (const f of picked) {
  const db = new DatabaseSync(dir + '/' + f, { readOnly: true });
  console.log('\n===== ' + f);
  const tables = db.prepare("SELECT name FROM sqlite_master WHERE type='table'").all();
  console.log('tables:', tables.map((r) => r.name).join(', '));
  const cols = db.prepare("SELECT name FROM pragma_table_info('nodes')").all();
  console.log('nodes cols:', cols.map((r) => r.name).join(', '));
  console.dir(db.prepare('SELECT * FROM nodes LIMIT 2').all(), { depth: 3 });
  const cnt = db.prepare('SELECT COUNT(*) AS n FROM nodes').get();
  console.log('node count:', cnt.n);
  db.close();
}

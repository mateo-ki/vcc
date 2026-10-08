import { DatabaseSync } from 'node:sqlite';

const path = process.argv[2];
const db = new DatabaseSync(path, { readOnly: true });
const rows = db
  .prepare("SELECT type, name, sql FROM sqlite_master WHERE sql IS NOT NULL ORDER BY type DESC, name")
  .all();
for (const r of rows) {
  console.log('### ' + r.type + ' ' + r.name);
  console.log(r.sql + '\n');
}
if (process.argv[3] === '--counts') {
  for (const r of rows.filter((x) => x.type === 'table')) {
    try {
      console.log(r.name + ': ' + db.prepare('SELECT COUNT(*) AS n FROM "' + r.name + '"').get().n);
    } catch (e) {
      console.log(r.name + ': ERR ' + e.message);
    }
  }
}
db.close();

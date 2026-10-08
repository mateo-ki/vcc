import { DatabaseSync } from 'node:sqlite';

const root = 'D:/project/rust/vs/vpn-parser/data';
const db = new DatabaseSync(root + '/vpn-parser.db', { readOnly: true });

const tables = db.prepare("SELECT name FROM sqlite_master WHERE type='table'").all();
console.log('tables:', tables.map((r) => r.name).join(', '));

const queries = [
  'SELECT id, created_at, updated_at, last_fetch_at, last_resolved_url, content_sha256 FROM sources LIMIT 2',
  'SELECT id, at, kind, ok, latency_ms, node_name FROM egress_health ORDER BY at DESC LIMIT 3',
  'SELECT at, reason, from_name, to_name, pinned FROM egress_switches ORDER BY at DESC LIMIT 3',
  'SELECT day, rule, target, hits, first_at, last_at FROM rule_hits LIMIT 3',
  'SELECT source_id, at, ok, node_count, status FROM source_fetch_history ORDER BY at DESC LIMIT 3',
  'SELECT * FROM settings LIMIT 1',
  'SELECT length(config_json) AS len, substr(config_json,1,300) AS head FROM proxy_settings',
  'SELECT gist_id, first_seen_at FROM seen_gists LIMIT 2',
];
for (const q of queries) {
  console.log('\n--- ' + q);
  try {
    console.dir(db.prepare(q).all(), { depth: 3 });
  } catch (e) {
    console.log('ERR ' + e.message);
  }
}
db.close();

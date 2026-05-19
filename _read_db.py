import sqlite3, os, json

db_path = r"C:\Users\15756\.cc-switch\cc-switch.db"
print("Exists:", os.path.exists(db_path))

conn = sqlite3.connect(db_path)
cur = conn.cursor()

cur.execute("SELECT name FROM sqlite_master WHERE type='table'")
tables = cur.fetchall()
print("=== 表列表 ===")
for t in tables:
    print(f"  {t[0]}")

for tname in [r[0] for r in tables]:
    try:
        cur.execute(f'SELECT * FROM "{tname}"')
        rows = cur.fetchall()
        if not rows:
            print(f"\n=== {tname} === (空)")
            continue
        cols = [d[0] for d in cur.description]
        print(f"\n=== {tname} ({len(cols)} 列, {len(rows)} 行) ===")
        print(f"列: {cols}")
        for r in rows[:5]:
            rd = dict(zip(cols, r))
            print(json.dumps(rd, ensure_ascii=False, default=str)[:300])
    except Exception as e:
        print(f"\n=== {tname} === 错误: {e}")

conn.close()

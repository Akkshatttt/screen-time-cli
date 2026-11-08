"""Synthetic usage DB for CLI timing: python tools/gen_db.py OUT.db YEARS"""
import random, sqlite3, sys, time

out, years = sys.argv[1], float(sys.argv[2])
apps = ["chrome.exe", "Code.exe", "WindowsTerminal.exe", "explorer.exe", "vlc.exe", "Discord.exe",
        "Spotify.exe", "notepad.exe", "WINWORD.EXE", "steam.exe", "slack.exe", "mpv.exe"]
db = sqlite3.connect(out)
db.executescript(open("tools/schema.sql").read())
db.executemany("INSERT INTO exe(path,name) VALUES(?,?)", [("C:\\x\\" + a, a) for a in apps])
db.executemany("INSERT INTO title(text) VALUES(?)", [("title %d" % i,) for i in range(2000)])
rng = random.Random(1)
now = int(time.time())
t = now - int(years * 365 * 86400)
rows = []
while t < now:
    day_h = (t % 86400) // 3600
    if 1 <= day_h < 7 and rng.random() < 0.9:          # asleep: jump ahead
        t += 3600
        continue
    dur = int(rng.expovariate(1 / 180)) + 2            # ~3 min mean span
    state = rng.choices([0, 1, 2], [70, 20, 10])[0]
    rows.append((t, t + dur, rng.randrange(1, len(apps) + 1), rng.randrange(1, 2001), state))
    t += dur
db.executemany("INSERT INTO span(start_ts,end_ts,exe_id,title_id,state) VALUES(?,?,?,?,?)", rows)
db.commit()
print(len(rows), "spans")

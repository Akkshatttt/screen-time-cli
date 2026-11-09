-- Schema as shipped in 0.1.x/0.2.0, before PRAGMA user_version existed (version 0).
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE exe(id INTEGER PRIMARY KEY, path TEXT UNIQUE, name TEXT);
CREATE TABLE title(id INTEGER PRIMARY KEY, text TEXT UNIQUE);
CREATE TABLE span(id INTEGER PRIMARY KEY, start_ts INTEGER NOT NULL, end_ts INTEGER NOT NULL, exe_id INTEGER NOT NULL, title_id INTEGER NOT NULL, state INTEGER NOT NULL);
CREATE INDEX span_start ON span(start_ts);
INSERT INTO exe VALUES(1, 'C:\\a.exe', 'a.exe');
INSERT INTO title VALUES(1, 't');
INSERT INTO span VALUES(1, 1000, 1100, 1, 1, 0);
INSERT INTO span VALUES(2, 1100, 1300, 1, 1, 1);

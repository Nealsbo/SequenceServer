### Multithreaded sequence server

3rd party lisb:
- utlist from https://github.com/troydhanson/uthash

---
### Build

1. `git clone`
2. Go to downloaded folder and run `make`
3. Run `./make`
---
#### Commands:
- `seq# x y` - to generate subsequence
- - `#` for subsequence
- - `x` for start value
- - `y` for step of subsequence

- `export seq` or `e` - to send sequence to all clients

- `exit` or `quit` or `q` - to shutdown server (brocken!)

---

Port of server is hardcoded as `12345`.
Logging with printf macro can be disabled by changing `#if 1` to `#if 0`.
Telnet was used for clients with `telnet localhost 12345` command.

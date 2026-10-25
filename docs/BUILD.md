<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer -->
# Building `qcstats` — step by step

> [!IMPORTANT]
> You need the board connected to the internet.

---

## Before you start

- These steps run **on the Ventuno Q itself**.
- Nothing here changes how your board boots or behaves.
- `qcstats` only *reads* sensors. It never changes clocks, fans or settings.

---

## Step 1 — Install the build tools

To build `qcstats` You'll need `gcc` and `make`, the board doesn't come with these pre-installed.

```
sudo apt update && sudo apt install -y build-essential git
```

Verify

```
gcc --version && make --version
```

---

## Step 2 — Download `qcstats`

```
git clone https://github.com/Hans-Kramer/qcstats
```

Change directory:

```
cd qcstats
```

---

## Step 3 — Build it

```
make
```

---

## Step 4 — Try it

```
./qcstats --count 3
```

You should see three lines of numbers, about one per second, like this:

```
RAM 1924/15284MB CPU [A78C.0:5%,6%@1017MHz ...] GPU 479MHz ... VDD_IN 8080mW ...
```

That is it working. Press **Ctrl+C** at any time to stop it.

---

## Tests (optional)

```
make test
```

241 checks: the readers, the three output formats, every sampler against a captured board's
sysfs, the arithmetic, the command line, and `make install` into a staging directory. They
need no board and no root — a fixture in `tests/fixtures/` stands in for the hardware, and
`QCSTATS_SYSROOT` is what points the program at it.

```
sudo sh tests/board-check.sh
```

45 more that only a board can answer, and **only on a Ventuno Q**: `--gpu-busy` enables a
tracepoint in tracefs, which is root-only, and the metric exists only while the GPU is
powered. Nothing a captured tree holds can stand in for that, which is why these are
separate and run by hand. It prints a verdict per check and puts the board back on the way
out. `sh tests/board-check.sh --list` shows the sections.

---

## Step 5 — Install it properly (recommended)

To be able to run `qcstats` from anywhere:

```
sudo make install
```

Check it:

```
qcstats --count 1
```

## Step 6 — Create a Systemd Service (Optional)

If you would want `qcstats` to run continuously in the background, auto-start on boot, and restart after a crash:

Create a service file:

```
sudo nano /etc/systemd/system/qcstats.service
```

Add the following config:

```ini
[Unit]
Description=qcstats, a telemetry sampler for the Arduino Ventuno Q

[Service]
ExecStart=/usr/local/bin/qcstats --interval 1000
User=arduino
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

> [!IMPORTANT]
> Keep `User=arduino`. Without it the service runs as **root**, which `qcstats` does not need.

Reload, start & enable it:

```
sudo systemctl daemon-reload
sudo systemctl start qcstats
sudo systemctl enable qcstats
```

Check it:

```
systemctl status qcstats
```

View the readings:

```
journalctl -u qcstats -f
```

> [!NOTE]
> Do not add `--logfile` here. It empties the file on every start, so each restart would
> wipe your history. Leave the output to `journalctl`.

---

## Using it

| I want to… | Type this |
|---|---|
| Watch it live | `qcstats` — then Ctrl+C to stop |
| Take 10 readings and stop | `qcstats --count 10` |
| Read faster (every half second) | `qcstats --interval 500` |
| Save readings to a file | `qcstats --csv --logfile readings.csv` |
| See every temperature sensor | `qcstats --all-zones` |
| See all the options | `qcstats --help` |

---

## Removing it

If you created the service in Step 6, stop it first:

```
sudo systemctl disable --now qcstats
sudo rm /etc/systemd/system/qcstats.service
sudo systemctl daemon-reload
```

To uninstall `qcstats`:

```
sudo make uninstall
```

To delete the downloaded folder as well:

```
cd ~ && rm -rf qcstats
```

To remove the build tools:

```
sudo apt remove --purge build-essential && sudo apt autoremove --purge
```

> [!NOTE]
> `apt remove build-essential` on its own only removes one package and leaves the other
> 44 behind. The `autoremove` is what actually cleans up.

# int3bench: what one breakpoint exception costs

`int3bench.exe` is a small x86-64 Windows console program that measures how long a Windows program waits for one
breakpoint exception: an `int3` in its own code, caught by a vectored exception handler that continues execution.
It also measures a software exception (`RaiseException`, the same dispatch without a CPU trap) and a system call
(`VirtualQuery`), each on 1 to 16 threads at once.

We wrote it while looking into AION 2's fight stutter on the Mac. The game executes `int3` in its own code at about a
dozen addresses and continues from its own vectored handler: about 15,000 breakpoint exceptions a second while
standing in the world, two thirds of them on the game thread. On Windows that costs about 2 % of one core. Under a
translator it can cost far more, and it showed up as a game thread stuck in exception delivery.

The program touches nothing outside its own process: no files, no registry, no network.

## Files

| file | what |
|---|---|
| `int3bench.c` | the source |
| `int3bench.exe` | the build we measured with (SHA-256 `86e09ceb1063aa090947f1a4425caae551c6d7dde116e140eacfb642ca1b11c3`) |
| `results/` | the program's own output from each run in the tables below |
| `LICENSE` | MPL-2.0, the licence of `int3bench.c` |

Build it yourself with MinGW-w64 (we used GCC 13, `-win32` thread model):

```
x86_64-w64-mingw32-gcc -O1 -static -o int3bench.exe int3bench.c
```

## Running it

On Windows, in a Command Prompt:

```
int3bench.exe 20000
```

With Wine:

```
WINEDEBUG=-all wine int3bench.exe 20000
```

With Proton, outside Steam (change the Proton folder to the one you have):

```
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/steam"
export STEAM_COMPAT_DATA_PATH="$HOME/int3bench-prefix"
mkdir -p "$STEAM_COMPAT_DATA_PATH"
"$HOME/.steam/steam/steamapps/common/Proton - Experimental/proton" run ./int3bench.exe 20000
```

The argument is how many events each thread raises (20,000 if left out). Our numbers all use 20,000. A run takes a
few seconds on Windows, and up to about 25 seconds on a slow path.

To run one case alone, for a profiler: `int3bench.exe KIND THREADS N`, where KIND is `int3`, `raise` or `syscall`,
for example `int3bench.exe int3 16 20000`.

### Reading the output

```
int3       threads= 1 n=20000 wall_ms=24.8 us_per_event_on_a_thread=1.24 events_per_s_all=805837
```

- `threads`: how many threads raise events at the same time; they all start together.
- `us_per_event_on_a_thread`: wall time divided by the events of one thread. This is how long each thread waits
  for one event while the others do the same. With one thread it is the plain cost of one event.
- `events_per_s_all`: events of all threads together per second.

`int3` is the breakpoint case, `raise` the `RaiseException` case, `syscall` a `VirtualQuery` call (5x as many),
`nothing` an interlocked increment as a floor. The first line, `warmup`, is not a measurement. Numbers move between
passes by 10-50 % on the many-thread cases, so run it two or three times. Lines that Wine prints itself (`msync: ...`)
can be ignored.

## Our numbers

Machines: a Windows 11 PC (build 10.0.26300, Intel Core i7-10700K) as the reference, and a Mac with an M5 Max on
macOS 27.2.1 running Nagl's two engines (Wine 11.21 based):

- **Rosetta engine**: x86-64 Wine under Apple's Rosetta 2.
- **ARM64 engine**: ARM64 Wine with FEX translating the program's x86-64 code (ARM64EC). "Before" is the engine of
  v0.2.0-preview.8, "after" the same engine with Nagl's Wine patch 0052 (below) added. The patch shipped in
  v0.2.0-preview.9.

One `int3` caught by a vectored handler that continues, µs each thread waits per event (20,000 events per thread):

| threads at once | Windows PC | Rosetta engine | ARM64 engine before | ARM64 engine after (2 passes) |
|---|---|---|---|---|
| 1 | 1.24 | 16.8 | 6.52 | 1.16 / 0.58 |
| 2 | 3.33 | 23.3 | 12.0 | 1.50 / 0.81 |
| 4 | 3.11 | 30.3 | 34.3 | 2.72 / 2.12 |
| 8 | 3.09 | 47.7 | 162 | 6.37 / 6.85 |
| 16 | 4.22 | 94.9 | 662 | 19.4 / 22.4 |
| all threads together, per second (16 threads) | 3,790,000 | 169,000 | 24,000 | 827,000 / 714,000 |

`RaiseException` caught by the same handler, µs per event:

| threads at once | Windows PC | Rosetta engine | ARM64 engine before | ARM64 engine after (2 passes) |
|---|---|---|---|---|
| 1 | 0.47 | 0.64 | 3.05 | 3.02 / 2.68 |
| 4 | 2.35 | 2.85 | 18.5 | 18.5 / 18.1 |
| 16 | 4.05 | 19.5 | 342 | 384 / 381 |

A system call (`VirtualQuery`), µs per call on 1 / 4 / 16 threads: Windows PC 0.54 / 0.57 / 1.44, Rosetta engine
0.80 / 1.05 / 5.82, ARM64 engine 0.51 / 1.58 / 7.58 before and 0.60 / 1.61 / 7.26 after.

The raw output of each column is in `results/`. The Windows PC, Rosetta engine and "before" columns came from the
first build of this program; the build here added only the one-case mode (`int3bench.exe KIND THREADS N`) and
measures with the same loops. The "after" columns are from the build here.

## What the numbers say

**Rosetta engine.** Rosetta delivers every CPU trap of translated code through one thread of the process. One
breakpoint costs about 15 µs there, Wine adds about 2 µs, and all threads together reach no more than about 165,000
a second, so threads that hit breakpoints at the same moment queue for that one thread. Nothing in Wine can change
that.

**ARM64 engine, before.** 71-83 % of an exception's time was spent in Wine's ARM64 return path to x86-64 code: a
system call such as `NtContinue` that returns to emulated code left Wine's syscall dispatcher by sending `SIGUSR2`
to its own thread, so a signal handler could set up the return. An `int3` paid that twice, and macOS delivers one
process's signals one after another, hence 662 µs with 16 threads.

**ARM64 engine, after.** Nagl's Wine patch 0052 sets the return up on the syscall frame and returns the ordinary
way, with no signal. A breakpoint now costs about 1 µs on one thread, as on the Windows PC. `RaiseException`
continued in native ARM64EC code still takes the signal, and its numbers did not change.

**On x86-64 Linux (Wine or Proton)** neither slow path exists: the CPU traps the `int3` itself, the kernel delivers
`SIGTRAP` to the thread that hit it, and the return to the program needs no signal to itself. We have not measured
it. If you run it there, we would like to see the output.

## Licence and source

`int3bench.c` is our own work under the Mozilla Public License 2.0 (`LICENSE`). The executable is statically linked
with the MinGW-w64 runtime, which is under permissive licences (its own `COPYING.MinGW-w64-runtime` lists them), and with GCC's runtime
library, under the GCC Runtime Library Exception.
Wine patch 0052 is part of Nagl's Wine series and is LGPL-2.1-or-later, like Wine. Its source comes with each
release's sources, under the written offer in this repository's main README.

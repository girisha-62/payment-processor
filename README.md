# Multithreaded Payment Transaction Processing System

A C++17 prototype of a concurrent payment processor: a fixed thread pool
pulls transaction requests off a shared queue, validates and routes each one
(debit / credit / refund / reversal / transfer), and mutates account balances
through per-account locks — with idempotency-key based duplicate detection
and full transaction logging.

This is an in-memory prototype (no real database yet, no network yet) —
that's intentional. It's built so the concurrency core is solid and testable
in isolation, and the next steps (persistence, an HTTP API, Docker) bolt on
cleanly. See **"What to extend first"** below.

## Stack

- **C++17**, header-only core (`include/`), single compiled binary
- `std::thread`, `std::mutex`, `std::condition_variable`, `std::packaged_task` — no external concurrency libraries
- CMake build (a plain `g++` one-liner also works, see below)
- Docker / docker-compose scaffold for containerizing it and pairing it with PostgreSQL

## Project layout

```
payment-processor/
├── CMakeLists.txt
├── include/
│   ├── ThreadPool.hpp          # fixed worker pool + task queue
│   ├── AccountService.hpp      # owns account balances, per-account locking
│   ├── Transaction.hpp         # request/result types, enums
│   ├── TransactionProcessor.hpp# validate -> dedupe -> route -> log pipeline
│   └── Logger.hpp              # thread-safe console + file logger
├── src/
│   └── main.cpp                # demo: spins up accounts, fires concurrent load
├── docker/
│   ├── Dockerfile
│   ├── docker-compose.yml      # app + Postgres, scaffold for persistence
│   └── schema.sql              # matching relational schema (not wired up yet)
└── transactions.log            # written on each run
```

## How to run it

**Option A — plain g++ (fastest, no CMake needed):**
```bash
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude src/main.cpp -o payment_processor -pthread
./payment_processor            # defaults: 4 worker threads, 6 clients x 20 txns
./payment_processor 8 10 50    # custom: 8 threads, 10 clients, 50 txns each
```

**Option B — CMake:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/payment_processor 4 6 20
```

**Option C — Docker:**
```bash
cd docker
docker compose up --build
```

Each run prints per-transaction log lines (with the processing thread id so
you can see the concurrency happening), a final balance sheet, and a
success/failed/duplicate summary. The same output is appended to
`transactions.log`.

## What it actually demonstrates

- **Thread pool**: a fixed number of worker threads pull tasks from one
  `std::queue` guarded by a `std::mutex` + `std::condition_variable` — the
  classic producer/consumer pattern, with graceful shutdown (workers drain
  the queue before exiting).
- **Fine-grained locking, not one global lock**: every `Account` has its own
  `std::mutex`. Debit/credit only ever lock the one account involved, so
  transactions on *different* accounts run in true parallel — they don't
  serialize against each other just because they share an `AccountService`.
- **Deadlock avoidance on transfers**: a transfer touches two accounts at
  once. It locks them in a fixed order (lower id first) using `std::lock`,
  the standard way to acquire multiple mutexes without risking the
  "thread A locks 1-then-2 while thread B locks 2-then-1" deadlock.
- **A concurrency bug I hit and fixed while building this**: transfers where
  `fromAccount == toAccount` (self-transfers) originally locked the *same*
  mutex twice on one thread — undefined behavior that manifests as a silent
  hang under load. It only showed up once the stress test happened to
  generate a self-transfer, which is exactly the kind of bug that's easy to
  miss in a small manual test and easy to catch with a randomized load test.
  Fixed in `AccountService::transfer` with an explicit same-account guard.
  This is a great thing to be able to talk through in an interview — it's a
  real deadlock, not a hypothetical one.
- **Idempotency / duplicate detection**: each request carries a client
  idempotency key; a mutex-guarded `unordered_set` insert-and-check makes
  "have I seen this key" atomic across threads — the same pattern real
  payment APIs (Stripe, etc.) use so a retried request doesn't double-charge.
- **Exception-safe futures**: `submit()` returns a `std::future`, so any
  exception thrown while processing a transaction surfaces at `.get()`
  instead of silently killing a worker thread.

## What to extend first

Roughly in the order I'd tackle them for a resume project / to keep
learning from it:

1. **Real persistence (highest value).** Swap `AccountService`'s
   `unordered_map` for PostgreSQL via `libpqxx`. `docker/schema.sql` already
   matches the domain model. The interesting part: your in-memory locking
   strategy (per-account mutex, ordered locking for transfers) needs a
   database equivalent — `SELECT ... FOR UPDATE` with accounts locked in a
   consistent id order, inside a transaction. That's a direct, explainable
   translation of what this prototype already does in memory.
2. **An HTTP API.** Wrap `TransactionProcessor::submit()` behind a small
   REST layer (cpp-httplib is the least-friction option) so transactions
   come from real HTTP requests instead of the in-process load generator.
   Gives you a `curl`-able demo, which is worth a lot in an interview.
3. **Persistent duplicate detection.** Right now `seenKeys_` is an in-memory
   set that resets on restart — fine for a demo, wrong for production. Move
   it to a `UNIQUE` constraint on `idempotency_key` in Postgres (already in
   `schema.sql`) and catch the constraint violation as your duplicate signal.
4. **Metrics.** Track p50/p99 processing latency and queue depth over time
   (a simple ring buffer + periodic print, or wire up Prometheus). Lets you
   talk about throughput under load, not just correctness.
5. **Structured logging.** Swap the plain-text logger for JSON lines, so the
   log is machine-parseable — pairs naturally with a metrics dashboard.
6. **Bounded queue + backpressure.** Right now the task queue is unbounded —
   under a big enough burst it grows without limit. Cap it and have
   `submit()` block or reject when full; demonstrates you understand
   backpressure, not just "add more threads."
7. **Unit tests.** Add GoogleTest for `AccountService` (concurrent
   debit/credit races, transfer deadlock-freedom under TSan) and
   `TransactionProcessor` (duplicate detection, insufficient funds path).
   Bonus: build once with `-fsanitize=thread` and run the stress test — a
   clean TSan report on a payment system is a strong resume line by itself.

## Talking points for interviews

- "I designed a multithreaded C++ payment transaction processor with
  fine-grained per-account locking instead of a single global lock, and
  ordered lock acquisition to avoid deadlock on transfers."
- "I caught and fixed a real deadlock during load testing — a self-transfer
  case that locked the same mutex twice — which is the kind of bug that
  only surfaces under concurrent load, not manual testing."
- "Idempotency keys give exactly-once processing semantics even when a
  client retries a request, the same pattern production payment APIs use."

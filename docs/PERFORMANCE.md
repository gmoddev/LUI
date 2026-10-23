# Workload and performance

The [synthetic conversation](../examples/conversation/README.md) exercises 10,000 stable messages, variable-height recycled rows, inserts/edits/deletes, append/follow-end, resize, font/theme changes, multiline editing, secret input and declared images. Budgets were established before qualification; they are experimental acceptance limits, not SDK service levels.

## Admission budgets

| Resource | Bound |
| --- | --- |
| Qualification model / API model ceiling | 10,000 / 20,000 items |
| Realized rows, including overscan | 48 |
| Conversation live Instances / row connections | 256 / 192 |
| VM memory / native host working set | 128 MiB / 512 MiB |
| Image decodes / admitted requests | 2 / 32 |
| Pinned-inclusive image cache | 16 entries / 32 MiB |
| Encoded image / decoded pixels | 1 MiB / 1,048,576 |
| Backend queued payload | 8 MiB / 4,096 events; GTK admission is tighter |

## Latency budgets

| Measurement | Limit |
| --- | --- |
| Captured application startup to ready | 10 seconds |
| Native input admission to owner response | p95 100 ms / p99 250 ms |
| Canonical layout/presentation | p95 50 ms / p99 100 ms |
| Native render submission | p95 50 ms |

Exercise at least 200 mixed transitions. Record monotonic clock boundaries, sample counts, median/p95/p99/max, build configuration, platform, baseline/peak memory and startup separately. Numeric aggregates retain at most 256 samples and must omit text, keys, paths, preedit and secrets.

The selected [Windows and GTK/X11 profiles](PLATFORMS.md) passed these budgets. Windows conservatively measures submission from the first native mutation, including remaining owner work; GTK starts after the complete owner update and retains the earlier stamp when a frame is pending. Submission is not physical display latency and these boundaries do not justify direct toolkit comparisons.

## Experimental browser workload

The selected Linux/X11 software profile uses separate browser ceilings: warm
captured startup at most 10 seconds and DOM input admission through acknowledged
canonical DOM at p95 50 ms / p99 250 ms, with at least 200 transitions per engine.
The final optimized Release/LTO conversation run passed: Chromium p95/p99 37.5/39.2 ms,
Firefox 44/46 ms and Playwright WebKit 44/49 ms. Startup was 293.9–496 ms; after
200 insert/edit transitions the model held 10,100 items, five resident rows and
46–47 Instances. Each engine also passed 100 fresh-worker lifecycle cycles.

These measurements run inside the page and exclude per-sample automation
transport. They do not measure physical paint. Individual core WASM calls still
miss the 8 ms owner target (201–202 calls per recorded run, maximum 53–57.8 ms);
there is no hard-preemption guarantee. Debug measurements do not qualify this
Release budget. See the [browser guide](../backends/browser/README.md) for pins,
repeatable commands, resource ceilings and separate selected IME/Orca sessions.

Conversation linear-memory capacity remains 33,554,432 bytes in each engine.
Separate long compiler input and successful heap-growth regressions reach
40,304,640 bytes, below the 268,435,456-byte maximum. These capacities do not
measure browser-process or DOM heap usage.

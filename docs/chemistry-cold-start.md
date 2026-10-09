# Chemistry cold start — measurement and diagnosis

Originating issue: #97. Host: macOS 26.5 arm64
(Apple silicon), staged product from `packaging/stage-product-macos.sh`.
Reproduce with `scripts/measure-chemistry-cold-start.sh <staged-product-dir> [report.json]`;
the machine-readable baseline is `out/local/issue-97/cold-start-baseline.json` on this host
(`out/` is not tracked; the same JSON is attached to #97). Evidence label `automated`, this host.

"Cold" means the payload has never been executed from its current inodes: each probe runs
in a pristine copy of the staged payload with extended attributes cleared. That is what a
fresh install looks like to the kernel's first-execution code evaluation. "Warm" is the
second run in the same copy.

## Phase 1 — per-stage breakdown

`iupac-cli analyze --mode name --text caffeine`, the owner's reported case. `caffeine` is
**not** in the 256-record offline snapshot, so this request takes the full OPSIN/JVM path.

| stage | cold ms | warm ms | share of cold |
| --- | ---: | ---: | ---: |
| frozen helper first execution (PyInstaller bootloader → CPython 3.11 → `rdkit`/`numpy` import) | 12 400–24 800 | 250 | **84–95 %** |
| private JVM start (jlink java.base image, no CDS archive) | 900 | 50 | 3–6 % |
| OPSIN jar load and first parse (on top of the JVM above) | 830 | 240 | 3–5 % |
| discovery SQLite open + first `names`/`records` query | 0.3 | 0.01 | < 0.01 % |
| helper protocol round trip (one-shot spawn, one request, one reply) | included above | included above | — |
| reading every payload byte with no execution | 155 | 42 | (control) |

Whole-command totals on the same host, same harness run:

| command | cold s | warm s |
| --- | ---: | ---: |
| `analyze --mode smiles` (helper only; never touches the index, OPSIN or the JVM) | 12.4–24.8 | 0.25 |
| `analyze --mode name --text caffeine` (helper + JVM + OPSIN) | 15.2–29.6 | 0.51 |

The first-ever execution of a never-before-seen payload on this host measured **29.6 s**.
Subsequent pristine copies of the *same bytes* settle at **13–25 s**: the evaluation recurs
per new inode, at a reduced rate once the content has been seen. A user installing a *new*
build always gets the first-ever figure.

## Diagnosis

**The dominant contributor is macOS first-execution code evaluation of the frozen helper's
Mach-O files, not our data, our protocol or the JVM.**

The evidence is three-fold and mutually confirming:

1. **It is not I/O.** Reading all 125 MB of the helper payload takes **0.155 s** cold. The
   same tree costs 12–25 s to *execute* cold.
2. **It is not our process's own work.** During a 13.1 s cold run the helper accumulates
   1.7 s user and 1.4 s system time; the remaining ~10 s is blocked. Sampling during the
   run shows `XprotectService` at **~90 % CPU**, with `syspolicyd` and `amfid` also active.
3. **It is per file, not per byte.** Mapping the 95 Mach-O files that one analysis actually
   loads, one at a time in a pristine copy, costs **12.37 s total, median 121 ms per file**.
   A 92 KB `libRDKitAlignment.1.dylib` costs **489 ms**; a 22.5 MB `libopenblas64_.0.dylib`
   costs **307 ms**. Size is close to irrelevant; file count is the cost driver.

So the cold path is `95 mapped Mach-O files × ~130 ms of XProtect/AMFI first-execution
evaluation`. Everything the issue suspected at the data/access level is measurably innocent:

- **Discovery SQLite is not a contributor.** The snapshot is 495 616 bytes with 256 records
  and 774 names. It is already opened lazily, read-only and immutable, per request, and is
  never read in full: open 0.3 ms, first query 0.5 ms, second query 0.01 ms. That is four
  orders of magnitude below the budget.
- **The JVM is not started for SMILES requests**, and not for names that the offline index
  resolves. `chemistry/helper.py` `_resolve_name` consults the index first and only falls
  through to `_opsin` on a miss. JVM + OPSIN together are 1.7 s cold, ~0.29 s warm — real,
  but 6–11 % of the problem, and below any plausible UI budget once the helper cost is gone.
- **There is no helper handshake to optimise.** The helper is one-shot: the plugin spawns it,
  writes one JSON request, reads one JSON reply, and the process exits. There is no protocol
  negotiation and no synchronous work on the message thread — `ExtensionCoordinator` owns a
  worker `std::jthread` and the popup polls status at 10 Hz (`inspected`,
  `src/chemistry/Extension.cpp`, `src/plugin/ChemistryPopup.cpp`).

### Payload inventory

| quantity | value |
| --- | ---: |
| payload files shipped under `resources/chemistry` | 520 |
| Mach-O files shipped under `resources/chemistry/helper` | 214 |
| Mach-O files actually mapped by one analysis | 95 |
| bytes of those mapped files | 80.7 MB |
| of the 95: `rdkit` | 48 |
| of the 95: CPython stdlib extensions + `Python` framework | 26 |
| of the 95: `numpy` (including all ten `numpy.random` extensions) | 17 |
| of the 95: `PIL` | 1 |
| of the 95: `libcrypto`, `libsqlite3`, the frozen helper itself | 3 |

The 119 shipped-but-never-mapped Mach-O files cost **nothing** on the cold path — they are
never executed. Stripping them shrinks the install; it does not make the first analysis
faster. That is the most important negative result here, because it is the fix the issue's
example list suggests first.

### The reducible share

Only the *mapped* file count is reducible. `numpy` and `PIL` are the only mapped groups the
helper does not genuinely need: `from rdkit import Chem` imports neither. They are pulled in
exclusively by `rdkit.Chem.Crippen`, `rdkit.Chem.Descriptors` and `rdkit.Chem.Lipinski`,
whose four uses in `chemistry/helper.py` are thin aliases over `rdkit.Chem.rdMolDescriptors`
entry points that the helper already imports. Dropping those three modules removes 18 of the
95 mapped files (`numpy` 17 + `PIL` 1) with bit-identical analysis output.

`rdkit` itself cannot be reduced below the 48 files a single canonicalisation plus descriptor
pass maps, without repackaging RDKit's own shared-library split — outside this work's boundary
and outside architecture-controlled analysis semantics.

One consequence had to be handled rather than shipped: RDKit's Boost.Python extension modules
probe for numpy as they load and print the bare `ModuleNotFoundError` to stderr when it is
absent, and `src/chemistry/Extension.cpp` appends helper stderr to a *failed* request's
diagnostic — so that probe would have reached the user as a fake "damaged install" message.
`chemistry/helper.py` therefore silences fd 2 for the duration of the RDKit import only; a
genuine failure still raises and its traceback reaches the restored stderr, and
`packaging/verify-runtime.py` now gates an empty frozen stderr and a working pre-warm action
at packaging time.

## The floor, and what it means

After the reduction above, ~77 mapped files remain at ~130 ms each. **The residual cold cost
is therefore ~10 s and is a macOS platform floor, not a property of this product.** It is
imposed by XProtect/AMFI first-execution evaluation of a self-contained, ad-hoc-signed native
payload; every Mach-O in the payload is already ad-hoc signed (`codesign -dv` reports
`flags=0x2(adhoc)` on the helper, its dylibs and the private `java`), so there is no missing
signature to add. Notarisation would not help either: the evaluation is per new inode on this
host and recurs for every fresh install.

This is the same category as the issue's JVM/JAR clause: the target of ≤ 5 s for a genuinely
cold first analysis **is not reachable by payload or data/access changes**, and the floor is
quantified above. It is reported as such rather than worked around silently.

What *is* fully fixable is that the user meets this cost at all. The helper is pre-warmed off
the message thread when the editor opens, so the cost is paid in the background while the user
is still looking at the synth, and the first Apply runs on the warm path (≤ 1 s). Whatever
remains is shown in the popup as a live, cancellable pending state rather than as a freeze.

## Results after the #97 fixes

Same harness, same host, rebuilt payload (`packaging/build-runtime-macos.sh`) staged with
`packaging/stage-product-macos.sh`; machine-readable reports
`out/local/issue-97/cold-start-{baseline,fixed,staged}.json`.

| quantity | before | after | change |
| --- | ---: | ---: | ---: |
| Mach-O files mapped by one analysis | 95 | **71** | −24 |
| bytes of those mapped files | 80.7 MB | **39.3 MB** | −51 % |
| per-file mapping, total | 12.37 s | **9.13 s** | −26 % |
| per-file mapping, median per file | 121 ms | 121 ms | unchanged (it is a platform constant) |
| payload files shipped | 648 | **585** | −63 |
| payload bytes shipped | 285 MB | **201 MB** | −29 % |
| staged product, unpacked | 1 162 MB | **827 MB** | −29 % |
| cold `analyze --mode smiles` | 12.4–24.8 s | **9.8–11.3 s** | |
| cold `analyze --mode name caffeine` | 15.2–29.6 s | **11.4–12.6 s** | |
| warm `analyze --mode name caffeine` | 0.51 s | **0.44–0.46 s** | |

Cold figures vary by several seconds between otherwise identical runs, because the
evaluation is scheduled by `XprotectService` against whatever else the machine is doing.
The per-file total is the stable comparison; ranges above are min/max across the recorded
runs, not error bars.

### Cross-check: the same payload on Linux

The Linux arm64 payload built from the same spec, verified in the minimal Debian 12 image
with networking disabled (`out/local/<sha>/linux-delivery/`), reports first-request times of
**0.054 s (smiles), 0.341 s (name, OPSIN/JVM) and 0.051 s (discovery)**. Linux has no
XProtect, and the same frozen closure starts two orders of magnitude faster. That is the
cleanest confirmation that the cost diagnosed here is a macOS first-execution policy and not
anything about the payload's size, layout or contents.

The median per-file cost did not move, which is the point: the reduction is exactly the 24
files that no longer have to be evaluated. Nothing was hidden, and no analysis value changed
— all 106 V1 parity records are byte-identical.

### Pre-warm, end to end in the staged Standalone

A fresh copy of the staged `IUPAC Synth 2.app`, quarantine cleared, launched with `open -n`:

| observation | value |
| --- | ---: |
| pre-warm helper first seen after launch | **1.2 s** |
| a real `--mode name caffeine` analysis against that bundle's payload afterwards | **0.40 s** |

So the editor opens, the payload's first-execution cost is paid on the coordinator's worker
while the user is still looking at the synth, and the first Apply runs the warm path. On this
host the bytes had already been evaluated, so that background warm-up finished in 0.6 s; on a
genuinely first-ever install it is the ~10 s measured above, still in the background. The
payload's very first execution on this host, in place under `verify-runtime.py`, took 18.5 s.

## Not measured here

The AU variant inside `AUHostingServiceXPC` (the HOST-01 clause) is **not** covered by these
numbers. `auval` from a non-console account cannot open the component
(`OpenAComponent: result: -1`; components resolve through the console session's
registrar), so the sandboxed AU cold path needs a console-session measurement. Nothing in this document may be read as a claim
about it.

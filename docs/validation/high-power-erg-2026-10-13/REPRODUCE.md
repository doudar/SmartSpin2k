# Reproducing the synthetic validation

**Complete synthetic validation bundle; hardware verification remains outstanding.**
No simulation has been run by these packaging instructions. Commands below reproduce the existing diagnostics; they do not change model, thresholds or acceptance budgets.

## Environment and layout

Use Python ≥3.10, Git and a C++ compiler supporting the native adapter. Install the simulator's declared dependencies in a virtual environment. To make plots, also install its `reports` extra. The long runner imports POSIX `resource`, so use a Linux/macOS host. Allocate several GB for source clones, native executables and raw traces. Short CLI clones its clean baseline locally, which adds temporary disk use.

Create a working directory outside any Git repository. Copy the contents of `reproduction/` directly into it, preserving the structure. Required layout:

```text
work/
  simulator/                  # clone at commit below, clean
  baseline/                   # clean firmware clone at baseline below
  adapter/                    # separate clean firmware clone at same baseline
  trial-near-flat-anchored/    # same baseline plus final frozen candidate patch
  archive_fixture/full.json   # supplied original earned fixture, byte-exact
  baseline-selection.json     # supplied original selection, byte-exact
  extended_high_power.py
  candidate_cadence_stress.py
  candidate_timing_stress.py
  cadence_stress.py
  timing_stress.py
  continue_extended_high_power.py
  results/                    # new outputs, outside all checkouts
```

Firmware repository: https://github.com/doudar/SmartSpin2k.git
Baseline commit: `615fce5599f01fbe9459f59e924e7bb65c81a22b`
Simulator repository: https://github.com/doudar/smartspin2k-simulator.git
Simulator commit: `809a7c9980a812874e03fe4b935f55bd560412c1`
Required simulator tree: `362ec247af1067b3d6ea5972c4550380b9c44b70`

Do not check out the final firmware PR commit directly for these existing stress runners: their preflight intentionally requires HEAD at the pinned baseline plus a frozen source patch. Apply `candidate-production.patch` after checking out the baseline. This matches the four-production-file matrix source. The reviewed test changes are a separate `candidate-tests.patch`; apply those in a separate validation checkout for host/native tests. Candidate directory must begin with `trial-` for the timing helper. Do not silently rewrite the helper preflight, baseline-selection timestamp, profile, fixture or hashes.

All helper copies and fixture bytes are listed in `reproduction_manifest.json`. The helper layout is a dependency: `ROOT` is the helper script's containing directory, `ROOT/simulator` is imported, and `ROOT/baseline`, `ROOT/adapter`, `ROOT/archive_fixture/full.json` and `ROOT/baseline-selection.json` are checked. The fixture retains the original archived training provenance; its older training commit is distinct from the paired firmware baseline. It is not regenerated or calibrated at the target powers.

## Existing commands

From `work/` after creating and activating the Python environment:

```sh
python -m pip install -e './simulator[reports]'
python -m ss2k_sim.high_power \
  --firmware-root adapter --baseline-root baseline \
  --candidate-root trial-near-flat-anchored \
  --baseline-selection baseline-selection.json \
  --fixtures archive_fixture \
  --targets 500 700 1000 --states empty full \
  --sensitivities 3 5 10 --seeds 7 17 29 41 83 \
  --output results/short

python extended_high_power.py \
  --candidate-root trial-near-flat-anchored \
  --additional-candidate-path src/PowerTable_Helpers.cpp \
  --targets 500 700 1000 --states empty full \
  --sensitivities 3 5 10 --seeds 7 --output results/long

python candidate_cadence_stress.py \
  --candidate-root trial-near-flat-anchored \
  --additional-candidate-path src/PowerTable_Helpers.cpp \
  --targets 500 700 1000 --states empty full \
  --sensitivities 3 5 10 --seeds 7 --cadences 65 105 \
  --output results/cadence

python candidate_timing_stress.py \
  --candidate-root trial-near-flat-anchored \
  --additional-candidate-path src/PowerTable_Helpers.cpp \
  --output results/timing
```

Add further `--additional-candidate-path` values only for the exact reviewed test changes listed in the final source manifest, if they are included in the applied patch. The production patch changes only the four listed production files. The helpers whitelist test paths explicitly; unexpected changes must fail preflight rather than be hidden.

The unchanged short command may return status 1 when existing absolute low-power budgets fail even if every ride completes. Inspect `status`, requested/completed counts, each failure and both signals; do not equate a nonzero policy result with a missing ride, or suppress it. The stress helpers similarly retain fixed budget results.

For planning, the long and cadence helpers support `--dry-run`; the timing helper's `--dry-run` also executes its numerical self-tests. All three normal commands compile host-native controller executables. They do not build/flash ESP32 firmware.

The local investigation used `screen_frozen.py` to avoid the short CLI's temporary full baseline clone. That wrapper depends on archived local O metadata and is deliberately not presented as portable. The module command above uses the same production simulator implementation and supplied baseline selection through its supported CLI.

## Evidence, audits and selected trace access

The build configuration identifies completed cohort manifests, exact SHA-256 values and verified audit files. `build_evidence.py` runs offline against those results; it does not execute a simulator or write a repository. It retains literal `null` CSV values and refuses incomplete or source-mismatched inputs.

If raw candidate traces are archived, use their verified member index and SHA-256 values. Stream only selected trace members for plots and verify each full member before using its phase slices. Do not bulk extract archives onto a constrained disk. Baseline traces may be shared by verified symlink to the same frozen input family; preserve the original provenance checks.

All four synthetic cohorts and their independent audits are complete and pinned in `build_config.json` and `evidence_build_audit.json`. This is evidence for a draft hardware-validation candidate; the universal settling goal and a no-regression guarantee are not established.

## Reproducing the reviewed host and native checks

Create another firmware checkout named `validation` at the same baseline. Apply `candidate-production.patch`, then `candidate-tests.patch`. The exact four reviewed test-file hashes are in `test_identity.json`; the production fingerprint intentionally covers source/header/library files, not test files. Applying only a new incremental near-flat test to old tests does not reproduce the reviewed suite.

Use the repository's pinned pioarduino Core and native dependencies, Python simulator dependencies, GCC/G++ and Node. Full host preflight also requires ArduinoJson headers under `validation/.pio/libdeps/<environment>/ArduinoJson/src`; use the repository's documented dependency setup rather than disabling this check. Install the same Python requirements used by `native-tests.yml` (including `dbus_next`, which the simulator reports extra does not provide), then run the recorded suite entry points:

```sh
python -m pip install -r validation/requirements-ci.txt -r validation/scripts/erg_sim/requirements.txt -r validation/scripts/bike_sim/requirements-ble.txt
(
  cd validation
  spec=$(python -c 'import configparser; c=configparser.ConfigParser(); c.read("platformio.ini"); print(next(x.strip() for x in c["common"]["lib_deps"].splitlines() if "ArduinoJson" in x))')
  mkdir -p .pio/libdeps/ci
  platformio pkg install --global --storage-dir .pio/libdeps/ci --library "$spec"
)
python -B validation/scripts/ci_host_tests.py run --suite full
pio test --project-dir validation -e native
python -B validation/scripts/check_sdkconfig.py
```

The native entry point invokes the required ERG regression hook, which also needs the simulator dependencies installed in pioarduino's Python environment. The historical results are 133 host tests with two unchanged expected CSC failures and no skips/unexpected failures, 87 native passes plus 19 hook passes, and five SDK-policy configurations. These host-native commands do not establish ESP32 firmware compilation or real-bike behavior.

## Reproduction scope and archived-input dependencies

The bundle provides the exact fixture, patches, unchanged runners and their fixed-layout dependencies for a fresh synthetic run. Full historical raw matrices are not bundled. The offline evidence renderer uses `build_config.json` to require the separately retained original manifests and independent audit files at their listed relative paths and hashes, plus either raw traces or the verified archive/index pairs. Generated CSVs and PNGs remain usable without those large originals.

A new run will have its own manifest paths, timestamps and resource/build records, so its manifest hash need not equal the historical hash. Recheck source/model/fixture identities, complete all requested cases and independently recompute the same metrics before creating a new evidence configuration. Do not weaken the original pinned configuration or interpret source-equal inputs alone as a completed new audit. Exact source, inputs and thresholds are the reproducible contract; process timing and serialized machine paths are not performance observations.

## Rebuilding the delivered evidence from the archived audited inputs

With the archived input tree laid out at the relative paths in `build_config.json`, run:

```sh
python -B build_evidence.py --root /path/to/audited-input-root
python -B render_report.py
python -B -m unittest discover -s . -p test_evidence.py
python -B verify_exports.py --root /path/to/audited-input-root
```

The root argument is a user-selected local directory, not a required original machine path. The renderer runs no simulations or builds. It reads every selected raw member byte for hashing while retaining only the plotted interval. `evidence_build_audit.json` records selected whole-trace digests and the completed all-case/audit checks. `final_review_metadata.json` contains the separate reviewed interpretation and build-status wording used by the report renderer. It must describe the same production fingerprint.

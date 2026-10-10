---
name: add-dependency
description: Add or upgrade a third-party library in illusion with an exact version, SHA-256 verification and licence record. Use before introducing any external code.
---

# Adding or upgrading a dependency

Policy: `docs/adr/0002-dependency-management.md`.

1. **Licence check.** Only MIT, BSD, Zlib, Apache-2.0, Boost or public-domain style licences. No GPL,
   LGPL (static linking), or non-commercial licences. If unsure, stop and ask.
2. **Pick an exact release tag.** List tags with
   `git ls-remote --tags --refs https://github.com/<owner>/<repo>`.
3. **Compute the hash:**

```bash
curl -sSL -o dep.tar.gz https://github.com/<owner>/<repo>/archive/refs/tags/<tag>.tar.gz
sha256sum dep.tar.gz
```

4. **Declare it** in `cmake/Dependencies.cmake` with `URL`, `URL_HASH SHA256=...`,
   `DOWNLOAD_EXTRACT_TIMESTAMP TRUE` and `SYSTEM`. If the project's CMake builds things we do not want
   (tests, examples, install rules), set its options before `FetchContent_MakeAvailable`, or use
   `SOURCE_SUBDIR _illusion_no_cmake` and define a minimal target yourself.
   Repositories that need git submodules: `GIT_REPOSITORY` + full commit hash in `GIT_TAG`.
5. **Integrate** in `illusion_require_dependency()`: add a branch that makes it available and exposes a
   namespaced target (`illusion::<name>` or the project's own alias). Document the target in the comment
   above the function.
6. **Use it** from the module's `CMakeLists.txt`: `illusion_require_dependency(<name>)` then
   `target_link_libraries(... PRIVATE <target>)`. Do not expose third-party headers from public engine
   headers unless the type is part of the engine API (e.g. glm math types).
7. **Record it** in `THIRD_PARTY_NOTICES.md` (version, licence, use, shipped?).
8. Build with a `ci-*` preset on at least one platform and let CI cover the rest.
9. Upgrades: change version + hash together, read the upstream changelog, run the full test and
   benchmark suite.

Never commit SDKs that require a separate agreement (Steamworks, console SDKs). They are located through
a user-supplied path variable (e.g. `ILLUSION_STEAMWORKS_SDK`).

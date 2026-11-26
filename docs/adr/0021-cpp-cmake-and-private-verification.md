# Maintain Isoduo in C++ and CMake and keep real-machine verification tools in a private repository

The project author confirmed this decision on 2026-11-25 in interview Q1–Q20, recorded in isoduo/Isoduo-issues#156.

**Use C++ for runtime behavior and CMake for build, test and release automation.** Parallel PowerShell, shell and Python implementations made the same behavior costly to maintain across platforms. CI keeps its YAML workflows. Each run step invokes one CMake or CTest command, with multi-step logic in CMake scripts. A CTest check rejects tracked shell, PowerShell, Python and batch scripts. Retire the one-time settings migration and worktree repair tools.

**Use CMake presets locally and in CI.** Configure, build, test and workflow presets cover Windows clang-cl and MSVC in a Visual Studio developer environment, and Linux Clang and GCC. Debug, Release and core-only variants keep separate build outputs. Both Linux compilers test the core and compile the desktop targets. CMake records the source revision when Git is available and records it as unknown when building from a source archive.

**Keep automated product checks in Isoduo.** Naming, text encoding, package installation and release decisions can be checked with CMake. The demo rename, save and reopen test uses the C++ UI Automation client. CTest also uses the native host verifier, so that executable stays with the framework.

**Keep author-machine verification tools and their self-tests in the private repository `isoduo/Isoduo-verification`.** These tools need an idle desktop, input devices or the author's judgment. They may use PowerShell and Python and take the Isoduo build directory as an argument. The project author must complete real-machine verification before publishing each release. The public release notes record that completion. Detailed procedures belong with the tools.

**Keep verification steps reproducible.** Keep run results and details of the local development environment outside all GitHub repositories.

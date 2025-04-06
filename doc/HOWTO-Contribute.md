# Developer Contribution Guide
Thanks for considering to contribute to the project! In this guide you will find all the information you need to get started with contributing to the project.

## Formatting
* The project uses CamelCase for all variable- and function names (Note the initial Capitalization of function names and initial lower-case of variable names).
* We use the cpplint tool using the CPPLINT.CFG configuration file to check for code style violations. Please make sure to run cpplint on your code before submitting a pull request.
* We also use the clang-format tool using the .clang-format configuration file to format the code. Please make sure to run clang-format on your code before submitting a pull request.

## Compilation test
* The project uses CMake as the build system. Please make sure to run the CMake build system (release build) on your code before submitting a pull request. There must be no errors or warnings during compilation (warnings are treated as errors).

## SAST (Static Application Security Testing)
* The project uses codeql and semgrep for static application security testing. If possible, please run the codeql and semgrep tools on your code before submitting a pull request. Codeql (as well as additional SAST tools will be run on pull-request though so it's not strictly necessary to run these tools locally).

## Fuzzing
* The project uses the libFuzzer tool for fuzz testing. When using clang as your compiler and adding the BUILD_FUZZER option, a fuzzing binary (fuzz_handle_packet) will automatically be built. Consider running it for a good while before submitting a pull request.

## System tests / Integration tests
The system_test/ directory contains a set of system tests that can be run to veryify the functionality of the project. Please make sure to run the system tests (cd system_test ; ./run_all_tests.sh) before submitting a pull request. Ideally, run on Linux, OpenBSD, NetBSD and FreeBSD.

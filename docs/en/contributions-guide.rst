Contributions Guide
===================

:link_to_translation:`zh_CN:[中文]`

ESP-GMF is an open-source project. Contributions of code, documentation, examples, and bug reports are welcome.

How to Contribute
-----------------

Submit contributions to `espressif/esp-gmf <https://github.com/espressif/esp-gmf>`_ via GitHub Pull Requests. Common contribution types include:

- Bug fixes
- New elements, IO components, or packages
- Performance or interface improvements to existing components
- Improvements to READMEs, documentation, and code comments
- Adding unit tests and integration examples
- Reporting issues or documentation errors encountered during use

Prerequisites
-------------

Before submitting a Pull Request, check the following points:

- **License compatibility**: Contributed content must be your own work, or licensed under a license compatible with ESP-GMF. The repository is governed by the Espressif Modified MIT and Apache License 2.0 dual licenses; see `LICENSE <https://github.com/espressif/esp-gmf/blob/main/LICENSE>`_.
- **Coding style**: C code follows the `ESP-IDF Coding Style Guide <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/contribute/style-guide.html>`_, consistent with the existing codebase.
- **Pre-commit hooks**: Installing `ESP-IDF pre-commit hooks <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/contribute/install-pre-commit-hook.html>`_ is recommended for automatic checks of code formatting, copyright headers, and spelling.
- **Copyright headers**: Source files use SPDX identifiers (see the ESP-IDF `Copyright Headers Guide <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/contribute/copyright-guide.html>`_).
- **Code comments**: Public APIs use Doxygen-style comments; ``@brief`` / ``@param`` / ``@return`` must match the function signature; internal implementation should comment key steps as needed.
- **Examples and tests**: New components should include examples or test_apps; update examples when interfaces change.
- **Documentation sync**: Keep the component root README in sync with the ``docs/`` documentation center; when adding new sections, update both language versions.
- **Commit records**: Group commits by change type; each Pull Request should correspond to one primary change. Use `squash <https://eli.thegreenplace.net/2014/02/19/squashing-github-pull-requests-into-a-single-commit/>`_ to merge trivial fixes into the main commit.
- **When uncertain**: Submit a Pull Request for maintainer review first, then refine based on feedback.

Pull Request Submission Process
-------------------------------

1. Fork the repository and complete development on your own branch.
2. Submit a Pull Request to the upstream ``main`` branch; describe the motivation, scope, and testing method in the description.
3. Maintainers and the community will discuss in the comments. When ready, the PR is first merged into Espressif's internal git system for automated testing.
4. After tests pass, the changes are merged back to the public GitHub repository.
5. For external contributors, a Contributor Agreement must be signed during the PR process; subsequent submissions reuse it automatically.

Reporting Issues
----------------

Report bugs or feature requests via `GitHub Issues <https://github.com/espressif/esp-gmf/issues>`_. Search for existing issues before submitting to avoid duplicates. When filing a new issue, include:

- ESP-GMF version and ESP-IDF version
- Target chip model (ESP32 / ESP32-S3 / ESP32-P4, etc.)
- Minimum reproducible example code or logs
- Difference between expected and actual behavior

Support and Discussion
----------------------

For technical questions and community discussion, use the following channels:

- `Espressif ESP32 Forum <https://esp32.com>`_: General ESP32 development questions
- `Espressif Component Registry <https://components.espressif.com>`_: Publishing and finding ESP-IDF components

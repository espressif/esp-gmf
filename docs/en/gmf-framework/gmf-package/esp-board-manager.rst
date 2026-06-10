ESP Board Manager
=================

:link_to_translation:`zh_CN:[中文]`

.. note::

   ESP Board Manager has moved to a standalone repository. Visit `esp-board-manager <https://github.com/espressif/esp-board-manager>`_ for the latest version and documentation.

ESP Board Manager (BMGR) is the foundational infrastructure component for the Espressif board ecosystem, building a standardized link from board hardware to application software on Espressif chip platforms. BMGR describes hardware peripherals and functional devices in declarative YAML; a code generator produces standardized initialization code and provides unified runtime interfaces for device management at the application layer. Boards adapted with BMGR can be used directly in any BMGR project and shared across the community.

Key Features
------------

- Adapt once, reuse across projects: boards adapted with BMGR can be directly included in any BMGR project without rewriting board initialization logic
- Board and project decoupling: application projects obtain device handles and configurations via a unified API; switching the target board requires no changes to business logic
- Eliminates board boilerplate: declarative YAML configuration combined with a code generator produces standardized initialization code, replacing repetitive driver writing
- Board asset sharing and reuse: board configurations are published as standard components, shareable and iterable in the community; new projects reference existing assets directly
- Extensible architecture: supports adding new peripherals, devices, and custom boards at low integration cost
- Low memory footprint: only pointers are retained at runtime; configuration data remains as flash read-only

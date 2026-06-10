贡献指南
===================

:link_to_translation:`en:[English]`

ESP-GMF 是开源项目，欢迎贡献代码、文档、示例与问题反馈。

如何贡献
-------------------

通过 GitHub Pull Requests 向 `espressif/esp-gmf <https://github.com/espressif/esp-gmf>`_ 提交贡献内容。常见的贡献类型包括：

- 修复缺陷
- 新增处理单元（element）、外部接口（IO）或应用组件（package）
- 改进既有组件的性能或接口
- 完善 README、文档与代码注释
- 增加单元测试与集成示例
- 反馈使用过程中遇到的问题或文档错误

准备工作
-------------------

提交 Pull Request 前，请按下列要点检查：

- **许可证兼容**\ ：贡献内容必须是你自己的成果，或已获得与 ESP-GMF 许可证兼容的开源许可。仓库基于 Espressif Modified MIT 与 Apache License 2.0 双协议管理，详情见 `LICENSE <https://github.com/espressif/esp-gmf/blob/main/LICENSE>`_。
- **代码风格**\ ：C 代码遵循 `ESP-IDF 编码风格指南 <https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/contribute/style-guide.html>`_，与本仓库已有代码保持一致。
- **pre-commit 钩子**\ ：建议安装 `ESP-IDF pre-commit hooks <https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/contribute/install-pre-commit-hook.html>`_，自动检查代码格式、版权标头与拼写。
- **版权标头**\ ：源文件采用 SPDX 标识（参考 ESP-IDF `版权标头指南 <https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/contribute/copyright-guide.html>`_\ ）。
- **代码注释**\ ：公开 API 使用 Doxygen 风格注释，\ ``@brief`` / ``@param`` / ``@return`` 必须与签名一致；内部实现按需注释关键步骤。
- **示例与测试**\ ：新增组件时附带示例或 test_apps；接口变更时同步更新示例。
- **文档同步**\ ：组件根目录的 README 与 ``docs/zh_CN/`` 文档中心同步更新；新增章节时中英文双份保持一致。
- **提交记录**\ ：按改动类型分组提交，每个 Pull Request 对应一项主要改动；琐碎修正使用 `squash <https://eli.thegreenplace.net/2014/02/19/squashing-github-pull-requests-into-a-single-commit/>`_ 合并到主提交。
- **不确定时**\ ：先提 Pull Request 让维护者评审，再按反馈完善。

Pull Request 提交流程
---------------------------

1. Fork 仓库并在自己的分支上完成开发。
2. 提交 Pull Request 至上游 ``main`` 分支，在描述中说明改动动机、影响范围与测试方法。
3. 评论区会有维护者与社区的讨论。准备就绪后，PR 会先合入乐鑫内部 git 系统执行自动化测试。
4. 测试通过后，改动合并回公开 GitHub 仓库。
5. 涉及外部贡献者时，需在 PR 流程中签署 Contributor Agreement，后续提交自动复用。

提交问题
-------------------

通过 `GitHub Issues <https://github.com/espressif/esp-gmf/issues>`_ 反馈缺陷或提需求。提交前请先搜索是否已有相同问题，避免重复。提交新问题时附上：

- ESP-GMF 版本号与 ESP-IDF 版本号
- 目标芯片型号（ESP32 / ESP32-S3 / ESP32-P4 等）
- 最小可复现示例代码或日志
- 期望行为与实际行为的差异

支持与讨论
-------------------

技术问题与社区讨论可以通过下列渠道：

- `Espressif ESP32 论坛 <https://esp32.com>`_\ ：通用 ESP32 开发问题
- `Espressif Component Registry <https://components.espressif.com>`_\ ：发布与查找 ESP-IDF 组件

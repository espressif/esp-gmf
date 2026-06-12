* [English Version](./README.md)

# 文档源文件夹

本文件夹包含 **ESP-GMF 文档**的源文件，提供[英文版](https://docs.espressif.com/projects/esp-gmf/en/latest/)和[中文版](https://docs.espressif.com/projects/esp-gmf/zh_CN/latest/)。

这些源文件在 GitHub 上的渲染效果不佳，某些信息只有在构建文档后才能显示。

请使用每次提交后约 20 分钟内生成的实际文档：

# 在线文档

* 英文: <https://docs.espressif.com/projects/esp-gmf/en/latest/>
* 中文: <https://docs.espressif.com/projects/esp-gmf/zh_CN/latest/>

上述 URL 均对应 `main` 分支的最新版本。如需离线查阅，可点击文档站右下角版本切换栏中的 **Download HTML**，下载整站 HTML 压缩包（单个 `.zip`）；目前主要面向在线 HTML 浏览，暂未提供 PDF 版本。

# 构建文档

文档使用 Python 包 `esp-docs` 构建。请先安装锁定版本的构建依赖（包含 `esp-docs` 本身以及部分图表使用的 `sphinxcontrib-mermaid` 扩展）：

```bash
pip install -r requirements.txt
```

要查看可用选项的摘要，请运行：

```bash
build-docs --help
```

本地构建英文 HTML（默认 builder 为 `html`）：

```bash
build-docs -l en build
```

构建输出位于 `_build/<lang>/generic/html/` 目录。该目录里还会有
`esp_docs.esp_extensions.add_html_zip` 扩展生成的 `esp-gmf-<lang>-<version>.zip`，
即文档站 **Download HTML** 链接所指向的离线 HTML 压缩包。

更多信息请参阅 `esp-docs` 文档：<https://github.com/espressif/esp-docs/blob/master/README.md>。

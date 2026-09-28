# Gm Resource Modifier

**作者**: [TheRedDaemon](https://github.com/TheRedDaemon/ucp_gmResourceModifier)

此模块允许修改游戏已经加载的 GM1 资源。Crusader 对 GM 与 TGX 文件的处理不同：TGX 按需从磁盘读取，而 GM 在启动时加载。因此，仅拦截文件访问并修改路径还不够。开发者识别了内存中 GM 文件的管理结构，以便按需替换。

可以替换整个文件或单张图片，但类型必须相同，因为 SHC 在 GM1 文件中使用不同格式。此外，此模块可将图片转换为“interface”类型的单图资源。转换器调用 Windows 函数，因此支持情况取决于操作系统和 Wine 的能力。

此模块本身不提供直接的游戏功能，而是作为底层支持。更多信息请参阅仓库中的 README。

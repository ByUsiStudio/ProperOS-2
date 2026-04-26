# ProperOS 2

一款基于移动端应用 `iApp` 开发的模拟操作系统环境，可直接调用系统浏览器内核运行，并开放调用库。  
[历代对比](db.md)

## 核心特性

- **移动优先**：基于 iApp 框架开发，专为移动端优化
- **轻量高效**：直接调用系统浏览器内核，资源占用小
- **开放扩展**：提供完整的调用库，支持二次开发
- **双协议开源**：采用 MIT 与 AGPL 协议，**beta2.2.8** 版本后生效

## 快速开始

### 获取源码

<details>
<summary><b>国内镜像（推荐）</b></summary>

```bash
git clone https://gitee.com/properos/properos2.git
```
</details>

<details>
<summary><b>GitHub 源</b></summary>

```bash
git clone https://github.com/ByUsiTeam/ProperOS-2.git
```
</details>

### 项目打包

```bash
# 进入项目目录
cd properos2

# 打包为 iApp 可识别格式
zip -r ProperOS.iApp .
```

### 安装运行

1. 将生成的 `ProperOS.iApp` 文件传输至移动设备
2. 使用 **MT管理器** 打开文件
3. 选择打开方式：**类型** → **全部** → **iApp**
4. 完成导入即可运行或二次开发

> **小贴士**：`JiYU.mtsx` 是 MT管理器 的语法高亮文件，建议一并导入

## 更新计划

查看我们的[详细开发路线图](更新计划.md)，了解最新功能和未来规划。

## 分发须知

如果您对项目进行修改或二次分发，**必须**在项目中标注原始仓库地址：

- Gitee：https://gitee.com/properos/properos2
- GitHub：https://github.com/ByUsiTeam/ProperOS-2

## 开源协议

本项目采用 **MIT** 与 **AGPL** 双协议开源（从 beta2.2.8 版本后）。

## 贡献指南

欢迎提交 **Issue** 和 **Pull Request** 来帮助改进项目！

---

**ProperOS 2** - 在移动设备上体验操作系统般的感觉
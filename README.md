# ProperOS 2

## 基础特性
1. 基于移动端应用 `iApp` 开发的模拟操作系统环境
2. **ProperOS 2** 应用可直接调用系统浏览器内核运行，并开放调用库
3. 采用 MIT 与 AGPL 双协议进行开源，分发时需注明原项目地址，从 **beta2.2.8** 版本后

## [**更新计划**](更新计划.md)

## 部署指南
### 获取源码
- Gitee
  ```bash
  # Gitee 仓库
  git clone https://gitee.com/properos/properos2.git
  ```

- GitHub
  ```bash
  # GitHub 镜像
  git clone https://github.com/ByUsiTeam/ProperOS-2.git
  ```

### 项目打包
```bash
# 进入目录
cd properos2
# 打包
zip -r ProperOS.iApp .
```

### 安装运行
1. 将生成的 `ProperOS.iApp` 文件传输至移动设备
2. 使用 **MT管理器** 打开文件
3. 选择打开方式：**类型** → **全部** → **iApp**
4. 完成导入后即可运行或二次开发

> JiYU.mtsx 为 MT管理器 的语法高亮文件

### 分发须知
修改或二次分发时，必须在项目中标注原始仓库地址：  
[https://gitee.com/properos/properos2](https://gitee.com/properos/properos2)  
或者  
[https://github.com/ByUsiTeam/ProperOS-2](https://github.com/ByUsiTeam/ProperOS-2)
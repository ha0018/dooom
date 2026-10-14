# 性能实测

日期：2026-10-13。环境：Windows 11、Intel Core Ultra 7 265K、约 16 GB 内存、Qt 6.7.3 MSVC x64、MSVC 19.44、Release 构建。

使用 `LogScopeBench` 生成确定的 UTF-8 文本，每 100 行有一个 timeout。文件生成完成后立即在同一进程中建立索引并进行关键词搜索，属于文件缓存可能已经较热的单次测量。

| 指标 | 10 万行 | 650 万行 |
|---|---:|---:|
| 文件字节数 | 16,886,890 | 1,110,258,890 |
| 文件大小（MiB） | 16.1 | 1,058.8 |
| 索引时间 | 44 ms | 3,234 ms |
| timeout 搜索时间 | 57 ms | 5,374 ms |
| 匹配行数 | 1,000 | 65,000 |
| 行索引容器容量 | 4,194,288 B | 268,435,440 B |
| 进程峰值工作集 | 16,670,720 B | 280,055,808 B（约 267.1 MiB） |

复现命令：

```powershell
$env:PATH = 'C:\Qt\6.7.3\msvc2019_64\bin;' + $env:PATH
.\build\Release\LogScopeBench.exe --generate benchmark-data\small.log --lines 100000
.\build\Release\LogScopeBench.exe --generate benchmark-data\large.log --lines 6500000
```

## 如何解释这些数字

- 测量覆盖核心索引与搜索，未覆盖 GUI 首屏、滚动、事件循环响应或导出耗时。
- Windows 峰值工作集包含 Qt、分配器和进程其他内存，不是索引独占内存。
- 行索引容器有增长预留空间，容量可能大于实际行数乘以单个索引大小。
- 生成日志行长度较均匀；短行更多、超长行或复杂正则可能有明显不同的耗时与内存。
- 这些是本机单次测量；正式简历或优化对比应保存多轮结果，并区分冷缓存、热缓存。

数据文件保存在被 Git 忽略的 `benchmark-data/`，约占 1.05 GB 磁盘空间，可以按需要自行清理。

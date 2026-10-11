# 固定源码回归：三分钟目标验收

2026-10-11，生产语义基线a85ee780/rules311；本次只改构建/回归入口，不改恢复算法。
用户要求将当前约10分钟的固定回归降至3分钟以内，原范围和负例保持。

## 瓶颈与选择

原构建为AppleClang 21、arm64、Debug，仅有-g，没有优化。原65项的逐项耗时总和
约1206.42s，CTest固定两个工作槽，整体655.79s。绑定初始化、声明恢复和死定义
三组占621.23s；独立JS行为比较不是主要成本。对既有--value-state的3秒采样显示
共同图分析中的容器/variant、比较、预算检查细粒度调用；没有通过删检查改变算法。

采用独立source-regression配置：Debug + -O2 -g -UNDEBUG。保留普通Debug用于单步
排错，优化配置供日常固定回归。收益来自编译器消除细粒度调用开销，不宣称生产算法
的工作量/复杂度下降。没有使用NDEBUG、fast-math、跨轮生成缓存或增加并发。

## 同一名单实测

| 范围 | 原Debug | 优化Debug |
|---|---:|---:|
| 完整65项、CTest -j2 | 655.79s | **97.30s** |
| binding_initializer_test | 344.23s | 26.44s |
| declaration_value_test | 143.48s | 10.94s |
| definition_elision_test | 133.52s | 10.16s |
| nullish_selection_test | 84.96s | 7.15s |
| snapshot_optional_test | 67.94s | 5.07s |
| binding_interference_test | 37.26s | 3.54s |
| recovery_flow_test | 22.71s | 3.09s |

新配置32个目标及断言检查目标的首次冷构建285.82s，单独报告；未将该成本算作
三分钟内完成。无源码变化的增量构建3.09s，增量构建加本轮回归为**100.40s**。
更改源码后的实际编译成本取决于改动范围，不承诺任意冷构建/重编译都在3分钟内。
这是本机本次测量，不是其他机器的时限保证。

## 覆盖和配置核对

- tests/source-regression.json与已验收C的32目标/65测试逐项一致；执行前后无缺项、
  重复或跳过。CMake在缺测试/目标时失败，manifest变化触发重新配置。
- 783条编译命令具有-O2及-UNDEBUG；专用编译检查拒绝NDEBUG和未启用优化。
  手动关闭断言/优化两个反例失败；缺测试/缺目标两个配置反例也按预期失败。
- 十组共享fixture仍在本轮完整生成，8796行源码记录的内容摘要与普通Debug完全
  一致；不是读取旧构建的stdout替代测试。生产者身份和内容摘要校验保持。
- 独立JS执行、错误变异、200/201及几何增长、精确/差一预算照常通过。工作槽仍为2，
  干涉生成的两个固定线程继续计入CTest PROCESSORS=2；所有原截止/输出预算不变。
- 同一pasteboard输入和显式数值/构造合同，10份完整源码与C冻结逐字节一致；
  输入SHA保持854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e。
  这次不增加阶段一功能覆盖，不改变共同变量里程碑语义。

## 日常入口与证据

配置方法见README固定源码回归节；原构建的依赖路径不自动猜测或下载。
首次配置后使用：

```sh
cmake --build --preset source-regression
ctest --preset source-regression
```

preset仅运行固定名单，不恢复暂停的全套、扩展语料、RSS/benchmark或DFX。
普通build目录仍为原未优化Debug；直接在那里运行原名单仍可能需要约10分钟。

本地证据：build/stage-one/regression-latency/result.json、junit.xml、tests.log、
configuration-checks.json、selection-checks.json、debug-sample.txt、cold-build.log、
warm-build.log。采样探针的12.37s包含采样影响，不被用作独立无扰动加速比。
三分钟目标以完整65项97.30s为据；阶段一Goal仍active，后续回到独立行为/表示及结构gate。

## 加强实例身份检查后的当前复验

同日，富文本和ProgressBar加入最终联合对象身份图，ProgressBar另增两个有效JS
原型/可扩展性反例后，重新运行上述固定preset：**65/65通过，86.53s**。
JUnit测试名称与manifest逐项相同，failure/disabled/skipped均为0；两个工作槽保持。
这是当前一次完整回归的实测值，低于180s；与此前97.30s的差异不作为额外算法加速声明。
证据build/stage-one/instance-identities/{fixed65.log,fixed65-lasttest.log}及
build/regression/regression-current.xml。另行执行的六项图专项组合231.41s范围不同，
不计入或代替固定65项三分钟验收。

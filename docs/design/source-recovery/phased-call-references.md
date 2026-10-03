# 异常相位中的嵌套成员引用

2026-10-03：共同 PhaseSelectionCall 增加拥有的 ancestor callee/call 链，
EvaluationPlan.phase_references 按原 OpId 建立到实际 callee UseId 的索引。
CrossLogicalInline 以该索引判断引用边界，原 local forest 仍在原始位置独立检查。
同一个 MemberExpr / CallExpr 和 printer 输出嵌套 native 调用，不读 .call 辅助。

链从已验证条件参数调用向其原 kInline argument 消费者上行。父调用须有显式
receiver，父 callee 为唯一使用的原 named/computed property，getter 在子 getter
之前；两次调用与 getter 的异常签名一致、无 catch 暂存。原 commit 到子 getter
之间的路径必须 single-entry/single-exit、无 phi/catch 复制；其中只能包含纯操作
或子 member reference 本来拥有的效果依赖。其他 roots 和早先有副作用的参数
仍是边界。前后 reference 不会重新读，receiver 继续由原 CallPlan/TreeOperands 校验。

一次反向索引取得原 local tree 的最终 owner。每个 owner 若有多项 selection，
不逐个猜测共同前缀，也不重复扫描长路径，待联合顺序证明。单项 selection 的
ancestor 前缀按严格递减 getter OpId 分段，彼此不重叠。索引、使用普查和存储
均计费，链向量几何增长；提供错误或丢失 reference/ancestor 声明会独立拒绝。

实际 Push method 3613 中 inner Invoke 66 已是 outer Invoke 69 的合法 argument
子树，callee 29 仍在条件前保存，因此旧局部 corridor 无法证明它跨完整条件。
新链使用原 graph、callee/call/use/commit 与异常身份，未识别 hilog/JSON/record
名字或复制配对源码。该拒绝现已越过，record 3444 可输出方法/回调正文，但
派生 constructor 的 PC 8 缺口仍显式标记 incomplete，不授予整个类可执行。

低层三 profile 测试包括 1/2/200/201 连续调用与单链深度 1/2/200/201；独立效果
前缀、prefix 逃逸、坏 ancestor/call/use/index 均拒绝。native 对照包含外层 getter
先执行、callee 改写、两级 receiver、条件短路及各处异常。配对 Push 验收仅检查
registration methods 和 callbacks，以 prototype 调用隔离未恢复 constructor；
这不能代替整个 record 或平台运行验证。

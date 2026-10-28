# 私有名（`#x`）恢复（2026-10-29，设计稿，未实现）

## 目标

覆盖 ISA 台账中仍为 Unmeasured 的五个私有名 opcode：`LDPRIVATEPROPERTY`、
`STPRIVATEPROPERTY`、`TESTIN`、`CALLRUNTIME_CREATEPRIVATEPROPERTY`、
`CALLRUNTIME_DEFINEPRIVATEPROPERTY`。语料中出现得很少（共约 25 次），主要价值在于
ISA 覆盖本身。

## es2abc 的编译协议（pinned es2abc 构建，`P { #x = 1; static #s = 2; … }`）

- 类所在的词法环境由 `newlexenv N` 创建。`callruntime.createprivateproperty count, literal`
  依次填充环境中从第一个 Hole 开始的 count 个槽，每个槽是一个新的 private symbol
  （runtime_stubs-inl.h `RuntimeCreatePrivateProperty`）；class literal 的最后一项记录
  实例私有成员的数量。
- 访问指令以 `(层级, 槽)` 指定私有名：
  - `ldprivateproperty _, depth, slot` 读 acc 中对象的私有成员；
  - `stprivateproperty _, depth, slot, vObj` 写；
  - `testin _, depth, slot` 对应 `#x in o`。
- 字段由 `#~P>#instance_initializer` / `#static_initializer` 中的
  `callruntime.defineprivateproperty _, depth, slot, vObj` 定义（值在 acc 中）。
- 名字文本不进入 ABC（symbol 没有描述），因此输出名需要合成（`#p0`、`#p1`……）。
  私有名在类外不可观察，合成名不改变语义；但改名会影响错误信息文本，这一点需要用户
  确认能否接受。

## 方案（扩展现有 owner）

1. **语义层**：已有的 `LiftPrivateProperty` 接入 `LiftSemanticInstruction`，声明
   effects（可能抛 TypeError：成员缺失、或重复定义）与环境读取。
   `CREATEPRIVATEPROPERTY` / `DEFINEPRIVATEPROPERTY` 的 lifting 同步接入。
2. **私有名绑定**：把环境槽解析为私有名，复用捕获单元（capture cell）与环境分析，
   不新增环境模型。前提是能证明该槽只由 `createprivateproperty` 写入一次，且在类
   作用域内不再被改写。
3. **类语法**：在现有的类投影里，把 `defineprivateproperty` 识别为字段定义，
   实例字段走现有的 `ClassInstanceFields` 路径，静态字段走
   `ClassStaticFields`。成员体中的访问打印为 `o.#pN` / `#pN in o`。
4. **验收**：Node 差分覆盖读、写、`in`、缺失时抛 TypeError、静态字段、子类实例上
   的品牌检查；cohort 零变化。

## 依赖

静态私有字段与类的静态初始化器共用同一条路径。当类位于带环境清理 handler 的模块
入口时，这条路径受 [phased-class-members.md](phased-class-members.md) 中的缺口影响。
record 路线（cleanup-entry 擦除）不受影响，因此可以先在 record 路线上交付。

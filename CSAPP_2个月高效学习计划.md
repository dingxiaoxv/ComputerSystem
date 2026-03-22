# CSAPP 3e 两个月高效学习计划

适用对象：非计算机专业、对 Linux 和编程有一定经验、希望系统补齐计算机基础的工程师。

学习目标：

- 在 2 个月内掌握《Computer System: A Programmer's Perspective, Third Edition》的核心脉络
- 建立系统级心智模型：数据表示、汇编与栈、链接与进程、虚拟内存、性能与缓存、并发
- 能把书中概念和 Linux 工程实践关联起来，而不是只停留在术语层面

---

## 一、整体策略

这本书不适合在当前阶段按“从头到尾精读”学习。更高效的方式是抓主线、做验证、用少量高价值实验建立直觉。

主攻章节：

- 第 1 章：计算机系统漫游
- 第 2 章：信息的表示和处理
- 第 3 章：程序的机器级表示
- 第 5 章：优化程序性能
- 第 6 章：存储器层次结构
- 第 7 章：链接
- 第 8 章：异常控制流
- 第 9 章：虚拟内存
- 第 10 章：系统级 I/O
- 第 12 章：并发编程

略读章节：

- 第 4 章：处理器体系结构，只理解执行模型和流水线大意即可

按需阅读：

- 第 11 章：网络编程。如果工作中经常写网络服务，可单独补充

---

## 二、学习方法

每次学习固定做 3 步：

1. 读一个小节，20 到 30 分钟
2. 做一个小实验，10 到 20 分钟
3. 用自己的话总结，5 分钟

每章只保留 1 页笔记，固定写四类内容：

- 核心概念
- 3 个易错点
- 1 个 Linux 命令
- 1 个真实工程场景

建议的时间配置：

- 每天 25 到 40 分钟
- 每周 1 次 60 到 90 分钟的整块时间，用于实验、调试、复盘
- 如果一周特别忙，最低配也至少保留 4 天学习 + 1 天复盘

学习原则：

- 不追求全覆盖，追求主线清晰
- 不在难题上死磕，优先建立直觉
- 每学到一个概念，都尽量在 Linux 上验证
- 如果一个概念无法解释真实程序中的现象，说明理解还不够扎实

---

## 三、每天固定模板

每天只做三件事：

- 读 1 个小节或 4 到 8 页
- 跑 1 个命令或写 10 到 30 行小代码
- 写 3 句话总结

每天总结固定回答：

- 今天最重要的概念是什么
- 今天最容易错的点是什么
- 今天对应哪个真实工程现象

---

## 四、按目录细化的 56 天打卡计划

说明：

- 下面的安排按《CSAPP 3e》的章节目录细化到每天
- 每天默认抓 1 个主节或 2 到 6 个连续小节；条目较长的日子只精读主节和关键子节，其余扫读标题、例子和总结
- 如果当天时间特别碎，只完成前一半，后一半顺延到周末
- 重载日的执行优先级是：主节标题 > 关键子节 > 例子 > 选读内容
- 标了“选读”的内容不影响主线，可以在时间不够时跳过
- 每天执行顺序固定：先看“今日目标”，再读对应英文章节，最后做 5 分钟复述

建议你每天只盯住一个中文问题：

- 今天我要真正搞懂什么
- 这些小节在回答哪个系统问题
- 这个概念在 Linux 程序里会体现在哪里

## 第 1 周：第 1 章 + 第 2 章入门

目标：先建立全书地图，再进入数据表示。

- **Day 1**
  今日目标：建立“程序只是位 + 上下文”的总视角，知道编译系统为什么值得理解。
  阅读：`1.1 Information Is Bits + Context`、`1.2 Programs Are Translated by Other Programs into Different Forms`、`1.3 It Pays to Understand How Compilation Systems Work`
- **Day 2**
  今日目标：看懂一个最简单程序从源码到运行时，硬件和指令层面发生了什么。
  阅读：`1.4 Processors Read and Interpret Instructions Stored in Memory`、`1.4.1 Hardware Organization of a System`、`1.4.2 Running the hello Program`
- **Day 3**
  今日目标：先建立缓存、存储层次、操作系统的粗框架，不追细节。
  阅读：`1.5 Caches Matter`、`1.6 Storage Devices Form a Hierarchy`、`1.7 The Operating System Manages the Hardware`
- **Day 4**
  今日目标：知道进程、线程、虚拟内存、文件是操作系统给程序员的四个关键抽象。
  阅读：`1.7.1 Processes`、`1.7.2 Threads`、`1.7.3 Virtual Memory`、`1.7.4 Files`
- **Day 5**
  今日目标：收束第 1 章，建立全书主线，知道后面每章分别在补哪一层理解。
  阅读：`1.8 Systems Communicate with Other Systems Using Networks`、`1.9 Important Themes`、`1.9.1 Amdahl's Law`、`1.9.2 Concurrency and Parallelism`、`1.9.3 The Importance of Abstractions in Computer Systems`、`1.10 Summary`
- **Day 6**
  今日目标：进入数据表示，搞清楚十六进制、数据大小、字节序这些基础约定。
  阅读：`2.1 Information Storage`、`2.1.1 Hexadecimal Notation`、`2.1.2 Data Sizes`、`2.1.3 Addressing and Byte Ordering`
- **Day 7**
  今日目标：建立位运算直觉，知道字符串、代码、布尔代数和移位操作如何落到字节层面。
  阅读：`2.1.4 Representing Strings`、`2.1.5 Representing Code`、`2.1.6 Introduction to Boolean Algebra`、`2.1.7 Bit-Level Operations in C`、`2.1.8 Logical Operations in C`、`2.1.9 Shift Operations in C`

## 第 2 周：第 2 章核心

目标：把整数、溢出、浮点这条线真正吃透。

- **Day 8**
  今日目标：搞懂无符号数和补码，知道机器为什么这样表示整数。
  阅读：`2.2 Integer Representations`、`2.2.1 Integral Data Types`、`2.2.2 Unsigned Encodings`、`2.2.3 Two's-Complement Encodings`
- **Day 9**
  今日目标：理解有符号和无符号转换的坑，避免 C 里最常见的比较错误。
  阅读：`2.2.4 Conversions between Signed and Unsigned`、`2.2.5 Signed versus Unsigned in C`
- **Day 10**
  今日目标：搞懂扩展和截断，理解“位数变化为什么会改变值”。
  阅读：`2.2.6 Expanding the Bit Representation of a Number`、`2.2.7 Truncating Numbers`、`2.2.8 Advice on Signed versus Unsigned`
- **Day 11**
  今日目标：掌握加法、溢出、取负的规则，建立整数算术的机器视角。
  阅读：`2.3 Integer Arithmetic`、`2.3.1 Unsigned Addition`、`2.3.2 Two's-Complement Addition`、`2.3.3 Two's-Complement Negation`
- **Day 12**
  今日目标：理解乘法在机器里的结果为什么会“看起来奇怪”。
  阅读：`2.3.4 Unsigned Multiplication`、`2.3.5 Two's-Complement Multiplication`
- **Day 13**
  今日目标：知道编译器为什么喜欢把乘除法改写成移位和加法。
  阅读：`2.3.6 Multiplying by Constants`、`2.3.7 Dividing by Powers of 2`、`2.3.8 Final Thoughts on Integer Arithmetic`
- **Day 14**
  今日目标：进入浮点数表示，先搞清浮点格式的结构，而不是死记标准。
  阅读：`2.4 Floating Point`、`2.4.1 Fractional Binary Numbers`、`2.4.2 IEEE Floating-Point Representation`、`2.4.3 Example Numbers`

## 第 3 周：第 2 章收尾 + 第 3 章前半

目标：从数据表示过渡到机器级程序表示。

- **Day 15**
  今日目标：收尾浮点数，理解舍入误差和 C 里浮点行为为何不直观。
  阅读：`2.4.4 Rounding`、`2.4.5 Floating-Point Operations`、`2.4.6 Floating Point in C`、`2.5 Summary`
- **Day 16**
  今日目标：正式进入汇编，知道机器级代码和高级语言之间的关系。
  阅读：`3.1 A Historical Perspective`、`3.2 Program Encodings`、`3.2.1 Machine-Level Code`
- **Day 17**
  今日目标：学会最基础的反汇编阅读方式，知道汇编文本该怎么看。
  阅读：`3.2.2 Code Examples`、`3.2.3 Notes on Formatting`、`3.3 Data Formats`
- **Day 18**
  今日目标：掌握寄存器、操作数和数据搬运，这是后面读汇编的底座。
  阅读：`3.4 Accessing Information`、`3.4.1 Operand Specifiers`、`3.4.2 Data Movement Instructions`
- **Day 19**
  今日目标：理解 push/pop 和普通数据传送的区别，建立栈直觉。
  阅读：`3.4.3 Data Movement Example`、`3.4.4 Pushing and Popping Stack Data`、`3.5 Arithmetic and Logical Operations`
- **Day 20**
  今日目标：理解 `lea`、移位、算术和特殊算术指令的常见用途。
  阅读：`3.5.1 Load Effective Address`、`3.5.2 Unary and Binary Operations`、`3.5.3 Shift Operations`、`3.5.4 Discussion`、`3.5.5 Special Arithmetic Operations`
- **Day 21**
  今日目标：进入控制流，知道条件码和跳转是如何实现 `if` 的。
  阅读：`3.6 Control`、`3.6.1 Condition Codes`、`3.6.2 Accessing the Condition Codes`、`3.6.3 Jump Instructions`、`3.6.4 Jump Instruction Encodings`

## 第 4 周：第 3 章后半

目标：看懂控制流、过程调用、数组/结构体、缓冲区溢出。

- **Day 22**
  今日目标：看懂条件分支的两种实现思路，理解分支和条件移动的差异。
  阅读：`3.6.5 Implementing Conditional Branches with Conditional Control`、`3.6.6 Implementing Conditional Branches with Conditional Moves`
- **Day 23**
  今日目标：把循环和 `switch` 语句映射到汇编层面。
  阅读：`3.6.7 Loops`、`3.6.8 Switch Statements`
- **Day 24**
  今日目标：进入过程调用，理解运行时栈为什么是函数调用的核心。
  阅读：`3.7 Procedures`、`3.7.1 The Run-Time Stack`、`3.7.2 Control Transfer`、`3.7.3 Data Transfer`
- **Day 25**
  今日目标：搞清局部变量、寄存器保存和递归调用到底落在哪里。
  阅读：`3.7.4 Local Storage on the Stack`、`3.7.5 Local Storage in Registers`、`3.7.6 Recursive Procedures`
- **Day 26**
  今日目标：看懂数组和指针的地址计算，不再把“数组退化成指针”当口号记。
  阅读：`3.8 Array Allocation and Access`、`3.8.1 Basic Principles`、`3.8.2 Pointer Arithmetic`、`3.8.3 Nested Arrays`、`3.8.4 Fixed-Size Arrays`、`3.8.5 Variable-Size Arrays`
- **Day 27**
  今日目标：理解结构体、联合体和对齐规则，知道内存布局为什么会有 padding。
  阅读：`3.9 Heterogeneous Data Structures`、`3.9.1 Structures`、`3.9.2 Unions`、`3.9.3 Data Alignment`
- **Day 28**
  今日目标：把指针、调试器和越界访问串起来，开始建立安全漏洞直觉。
  阅读：`3.10 Combining Control and Data in Machine-Level Programs`、`3.10.1 Understanding Pointers`、`3.10.2 Life in the Real World: Using the GDB Debugger`、`3.10.3 Out-of-Bounds Memory References and Buffer Overflow`

## 第 5 周：第 3 章收尾 + 第 7 章 + 第 8 章前半

目标：串起汇编、链接、装载、进程和系统调用入口。

- **Day 29**
  今日目标：收尾缓冲区溢出与栈帧变化，顺带补齐浮点汇编代码的最小认识。
  阅读：`3.10.4 Thwarting Buffer Overflow Attacks`、`3.10.5 Supporting Variable-Size Stack Frames`、`3.11 Floating-Point Code`、`3.11.1 Floating-Point Movement and Conversion Operations`
- **Day 30**
  今日目标：知道浮点代码在汇编里长什么样，然后结束第 3 章。
  阅读：`3.11.2 Floating-Point Code in Procedures`、`3.11.3 Floating-Point Arithmetic Operations`、`3.11.4 Defining and Using Floating-Point Constants`、`3.11.5 Using Bitwise Operations in Floating-Point Code`、`3.11.6 Floating-Point Comparison Operations`、`3.12 Summary`
- **Day 31**
  今日目标：开始链接，理解编译器驱动、静态链接和目标文件分别扮演什么角色。
  阅读：`7.1 Compiler Drivers`、`7.2 Static Linking`、`7.3 Object Files`、`7.4 Relocatable Object Files`
- **Day 32**
  今日目标：搞懂符号表和符号解析，理解“重复定义”“库顺序”这类经典链接问题。
  阅读：`7.5 Symbols and Symbol Tables`、`7.6 Symbol Resolution`、`7.6.1 How Linkers Resolve Duplicate Symbol Names`、`7.6.2 Linking with Static Libraries`、`7.6.3 How Linkers Use Static Libraries to Resolve References`
- **Day 33**
  今日目标：理解重定位和可执行文件生成，知道地址为什么可以在链接后才确定。
  阅读：`7.7 Relocation`、`7.7.1 Relocation Entries`、`7.7.2 Relocating Symbol References`、`7.8 Executable Object Files`、`7.9 Loading Executable Object Files`
- **Day 34**
  今日目标：掌握动态链接、共享库、PIC 和对象文件分析工具的基本用途。
  阅读：`7.10 Dynamic Linking with Shared Libraries`、`7.11 Loading and Linking Shared Libraries from Applications`、`7.12 Position-Independent Code (PIC)`、`7.13 Library Interpositioning`、`7.13.1 Compile-Time Interpositioning`、`7.13.2 Link-Time Interpositioning`、`7.13.3 Run-Time Interpositioning`、`7.14 Tools for Manipulating Object Files`、`7.15 Summary`
- **Day 35**
  今日目标：开始异常控制流，理解 CPU 为什么会突然把控制权交给内核。
  阅读：`8.1 Exceptions`、`8.1.1 Exception Handling`、`8.1.2 Classes of Exceptions`、`8.1.3 Exceptions in Linux/x86-64 Systems`

## 第 6 周：第 8 章 + 第 9 章前半

目标：看懂异常控制流、进程控制、信号和地址翻译。

- **Day 36**
  今日目标：理解进程抽象的意义，知道逻辑控制流、并发流和上下文切换之间的关系。
  阅读：`8.2 Processes`、`8.2.1 Logical Control Flow`、`8.2.2 Concurrent Flows`、`8.2.3 Private Address Space`、`8.2.4 User and Kernel Modes`、`8.2.5 Context Switches`
- **Day 37**
  今日目标：掌握最基础的进程控制接口，知道进程 ID、创建、退出和回收怎么运作。
  阅读：`8.3 System Call Error Handling`、`8.4 Process Control`、`8.4.1 Obtaining Process IDs`、`8.4.2 Creating and Terminating Processes`、`8.4.3 Reaping Child Processes`
- **Day 38**
  今日目标：理解程序睡眠、程序装载以及 `fork + execve` 的经典执行模型。
  阅读：`8.4.4 Putting Processes to Sleep`、`8.4.5 Loading and Running Programs`、`8.4.6 Using fork and execve to Run Programs`
- **Day 39**
  今日目标：进入信号机制，先搞清楚信号是什么、怎么发、怎么收。
  阅读：`8.5 Signals`、`8.5.1 Signal Terminology`、`8.5.2 Sending Signals`、`8.5.3 Receiving Signals`
- **Day 40**
  今日目标：掌握信号处理中的危险点，理解为什么异步信号容易制造并发 bug。
  阅读：`8.5.4 Blocking and Unblocking Signals`、`8.5.5 Writing Signal Handlers`、`8.5.6 Synchronizing Flows to Avoid Nasty Concurrency Bugs`、`8.5.7 Explicitly Waiting for Signals`
- **Day 41**
  今日目标：收尾异常控制流，知道非局部跳转和进程工具在真实调试中怎么用。
  阅读：`8.6 Nonlocal Jumps`、`8.7 Tools for Manipulating Processes`、`8.8 Summary`
- **Day 42**
  今日目标：进入虚拟内存，先建立“物理地址、虚拟地址、页表”的大图景。
  阅读：`9.1 Physical and Virtual Addressing`、`9.2 Address Spaces`、`9.3 VM as a Tool for Caching`、`9.3.1 DRAM Cache Organization`、`9.3.2 Page Tables`

## 第 7 周：第 9 章后半 + 第 5 章前半

目标：理解虚拟内存的工程含义，再进入性能优化。

- **Day 43**
  今日目标：理解缺页、分配和内存保护，知道为什么虚拟内存既影响正确性也影响性能。
  阅读：`9.3.3 Page Hits`、`9.3.4 Page Faults`、`9.3.5 Allocating Pages`、`9.3.6 Locality to the Rescue Again`、`9.4 VM as a Tool for Memory Management`、`9.5 VM as a Tool for Memory Protection`
- **Day 44**
  今日目标：掌握地址翻译过程，知道缓存、TLB 和页表是怎么配合的。
  阅读：`9.6 Address Translation`、`9.6.1 Integrating Caches and VM`、`9.6.2 Speeding Up Address Translation with a TLB`
- **Day 45**
  今日目标：理解多级页表和一次完整的地址翻译链路。
  阅读：`9.6.3 Multi-Level Page Tables`、`9.6.4 Putting It Together: End-to-End Address Translation`、`9.7 Case Study: The Intel Core i7/Linux Memory System`
- **Day 46**
  今日目标：搞懂内存映射，理解共享库、写时复制和 `mmap` 为什么重要。
  阅读：`9.8 Memory Mapping`、`9.8.1 Shared Objects Revisited`、`9.8.2 Private Copy-on-Write Objects`、`9.8.3 Shared Objects`、`9.8.4 User-Level Memory Mapping with the mmap Function`
- **Day 47**
  今日目标：掌握动态内存分配的核心问题，并能识别 C 程序中的常见内存错误。
  阅读：`9.9 Dynamic Memory Allocation`、`9.9.1 The malloc and free Functions`、`9.9.2 Why Dynamic Memory Allocation?`、`9.9.3 Allocator Requirements and Goals`、`9.9.4 Fragmentation`、`9.10 Garbage Collection（选读）`、`9.10.1 Garbage Collector Basics（选读）`、`9.10.2 Mark&Sweep Garbage Collectors（选读）`、`9.10.3 Conservative Mark&Sweep for C Programs（选读）`、`9.11 Common Memory-Related Bugs in C Programs`、`9.12 Summary`
- **Day 48**
  今日目标：进入性能优化，先建立“如何定义和衡量程序性能”的标准视角。
  阅读：`5.1 Capabilities and Limitations of Optimizing Compilers`、`5.2 Expressing Program Performance`、`5.3 Program Example`
- **Day 49**
  今日目标：理解基础优化手段为何有效，并知道现代处理器不是“按代码顺序傻跑”。
  阅读：`5.4 Eliminating Loop Inefficiencies`、`5.5 Reducing Procedure Calls`、`5.6 Eliminating Unneeded Memory References`、`5.7 Understanding Modern Processors`、`5.7.1 Overall Operation`

## 第 8 周：第 5 章后半 + 第 6、10、12 章收尾

目标：用性能、缓存、I/O、并发把全书主线闭环。

建议：这一周条目偏密，最好每天拆成两个 15 到 20 分钟微时段，第一段看概念，第二段看实验或代码。

- **Day 50**
  今日目标：继续性能优化，理解循环展开和并行性提升为什么能提速。
  阅读：`5.7.2 Functional Unit Performance`、`5.7.3 An Abstract Model of Processor Operation`、`5.8 Loop Unrolling`、`5.9 Enhancing Parallelism`、`5.9.1 Multiple Accumulators`、`5.9.2 Reassociation Transformation`、`5.10 Summary of Results for Optimizing Combining Code`
- **Day 51**
  今日目标：知道优化为什么会失效，并学会用 profiling 找真正的瓶颈。
  阅读：`5.11 Some Limiting Factors`、`5.11.1 Register Spilling`、`5.11.2 Branch Prediction and Misprediction Penalties`、`5.12 Understanding Memory Performance`、`5.12.1 Load Performance`、`5.12.2 Store Performance`、`5.13 Life in the Real World: Performance Improvement Techniques`、`5.14 Identifying and Eliminating Performance Bottlenecks`、`5.14.1 Program Profiling`、`5.14.2 Using a Profiler to Guide Optimization`、`5.15 Summary`
- **Day 52**
  今日目标：建立存储技术、局部性和存储层次的整体图景。
  阅读：`6.1 Storage Technologies`、`6.1.1 Random Access Memory`、`6.1.2 Disk Storage`、`6.1.3 Solid State Disks`、`6.1.4 Storage Technology Trends`、`6.2 Locality`、`6.2.1 Locality of References to Program Data`、`6.2.2 Locality of Instruction Fetches`、`6.2.3 Summary of Locality`、`6.3 The Memory Hierarchy`、`6.3.1 Caching in the Memory Hierarchy`、`6.3.2 Summary of Memory Hierarchy Concepts`
- **Day 53**
  今日目标：看懂缓存组织和缓存友好代码，建立“为什么程序慢”的缓存视角。
  阅读：`6.4 Cache Memories`、`6.4.1 Generic Cache Memory Organization`、`6.4.2 Direct-Mapped Caches`、`6.4.3 Set Associative Caches`、`6.4.4 Fully Associative Caches`、`6.4.5 Issues with Writes`、`6.4.6 Anatomy of a Real Cache Hierarchy`、`6.4.7 Performance Impact of Cache Parameters`、`6.5 Writing Cache-Friendly Code`、`6.6 Putting It Together: The Impact of Caches on Program Performance`、`6.6.1 The Memory Mountain`、`6.6.2 Rearranging Loops to Increase Spatial Locality`、`6.6.3 Exploiting Locality in Your Programs`、`6.7 Summary`
- **Day 54**
  今日目标：进入系统级 I/O，理解文件描述符、读写接口和稳健 I/O 包。
  阅读：`10.1 Unix I/O`、`10.2 Files`、`10.3 Opening and Closing Files`、`10.4 Reading and Writing Files`、`10.5 Robust Reading and Writing with the RIO Package`、`10.5.1 RIO Unbuffered Input and Output Functions`、`10.5.2 RIO Buffered Input Functions`
- **Day 55**
  今日目标：补齐文件元数据、目录、共享文件、重定向和标准 I/O 的使用边界。
  阅读：`10.6 Reading File Metadata`、`10.7 Reading Directory Contents`、`10.8 Sharing Files`、`10.9 I/O Redirection`、`10.10 Standard I/O`、`10.11 Putting It Together: Which I/O Functions Should I Use?`、`10.12 Summary`
- **Day 56**
  今日目标：用并发收尾，理解进程并发、I/O 多路复用、线程、同步、竞态和死锁的主线差异。
  阅读：`12.1 Concurrent Programming with Processes`、`12.1.1 A Concurrent Server Based on Processes`、`12.1.2 Pros and Cons of Processes`、`12.2 Concurrent Programming with I/O Multiplexing`、`12.2.1 A Concurrent Event-Driven Server Based on I/O Multiplexing`、`12.2.2 Pros and Cons of I/O Multiplexing`、`12.3 Concurrent Programming with Threads`、`12.3.1 Thread Execution Model`、`12.3.2 Posix Threads`、`12.3.3 Creating Threads`、`12.3.4 Terminating Threads`、`12.3.5 Reaping Terminated Threads`、`12.3.6 Detaching Threads`、`12.3.7 Initializing Threads`、`12.3.8 A Concurrent Server Based on Threads`、`12.4 Shared Variables in Threaded Programs`、`12.4.1 Threads Memory Model`、`12.4.2 Mapping Variables to Memory`、`12.4.3 Shared Variables`、`12.5 Synchronizing Threads with Semaphores`、`12.5.1 Progress Graphs`、`12.5.2 Semaphores`、`12.5.3 Using Semaphores for Mutual Exclusion`、`12.5.4 Using Semaphores to Schedule Shared Resources`、`12.5.5 Putting It Together: A Concurrent Server Based on Prethreading`、`12.6 Using Threads for Parallelism`、`12.7 Other Concurrency Issues`、`12.7.1 Thread Safety`、`12.7.2 Reentrancy`、`12.7.3 Using Existing Library Functions in Threaded Programs`、`12.7.4 Races`、`12.7.5 Deadlocks`、`12.8 Summary`

---

## 五、每周最值得做的小实验

- 第 1 周：写整数溢出、符号位、字节序例子
- 第 2 周：验证浮点精度丢失和类型转换陷阱
- 第 3 周：把一个 C 函数编译成汇编并逐行标注
- 第 4 周：用 `readelf` 看 ELF，用工具观察程序如何被装载
- 第 5 周：用 `strace` 观察系统调用，用程序体验 `fork/exec`
- 第 6 周：查看 `/proc/self/maps`，理解堆、栈、共享库布局
- 第 7 周：做一次矩阵访问性能对比
- 第 8 周：写一个线程竞态例子并用锁修复

---

## 六、建议绑定学习的 Linux 工具

- `gcc -S`：查看 C 如何变成汇编
- `objdump -d`：反汇编可执行文件
- `gdb`：查看栈、寄存器、内存
- `readelf`：查看 ELF、节区、符号
- `ldd`：查看动态库依赖
- `strace`：查看系统调用
- `perf stat`：做基础性能对比
- `/proc/<pid>/maps`：观察进程地址空间

---

## 七、2 个月后的验收标准

学习结束后，至少应该能够回答清楚以下问题：

- 一个 C 程序从源码到运行，经历了哪些阶段
- 为什么某些整数比较会出错
- 一个函数调用时，栈和寄存器发生了什么
- 为什么程序会出现段错误
- 为什么两段逻辑相同的代码性能差很多
- 为什么多线程程序会出现竞态和死锁

如果这些问题能够用自己的语言讲清楚，并且能结合 Linux 工具做基本验证，就说明已经掌握了这本书最核心的部分。

---

## 八、最低配版本

如果这两个月特别忙，可以使用精简版：

- 每天只学 20 分钟
- 每周只保留 4 天学习 + 1 天复盘
- 只主攻这 6 章：第 2、3、7、8、9、12 章

即使采用最低配版本，也能抓住整本书的主干框架。

---

## 九、执行建议

- 每周结束时，把本周内容讲给一个完全不懂的人听
- 每章都问自己一句：它和 Linux 程序员的日常问题有什么关系
- 不要在习题中投入过多时间，卡住时优先看思路，再回头总结
- 学习过程中尽量多做“观察程序行为”的实验，而不是只看定义

这份计划的重点不是“读完”，而是“建立系统级理解并能迁移到实际工作中”。

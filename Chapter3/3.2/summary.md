# §3.2–3.3 汇编入门 + 数据格式

## 核心概念

### 编译工具链

| 命令 | 产物 | 作用 |
|------|------|------|
| `gcc -S mstore.c` | `mstore.s` | 停在汇编阶段，看 C → 汇编映射 |
| `gcc -c mstore.c` | `mstore.o` | 产生目标文件（机器码，未链接） |
| `gcc mstore.c main.c -o prog` | `prog` | 完整链接，产生可执行文件 |
| `objdump -d mstore.o` | 终端输出 | 反汇编，机器码 → 助记符 |

### `.s` 与 `objdump` 的区别

- `gcc -S` 输出带大量**伪指令**（`.globl`、`.cfi_*`、`.type`），供汇编器和调试器使用，不是真实指令
- `objdump -d` 输出**实际执行的机器码 + 助记符**，更接近 CPU 真正执行的内容

实验里 `mstore.s` 有 20+ 行，`objdump` 只有 6 条真实指令（`endbr64` 到 `ret`）。

### x86-64 数据宽度后缀

| 后缀 | 大小 | C 类型 |
|------|------|--------|
| `b` | 1 字节 | `char` |
| `w` | 2 字节 | `short` |
| `l` | 4 字节 | `int` |
| `q` | 8 字节 | `long`、指针 |

`mstore.s` 里全是 `movq`/`pushq`/`popq`，因为 `long` 和指针在 x86-64 上都是 64 位。

---

## 实验：multsore 汇编逐行解读

**源码：**

```c
long mult2(long, long);

void multsore(long x, long y, long *dest) {
  long t = mult2(x, y);
  *dest = t;
}
```

**反汇编输出：**

```asm
multsore:
    endbr64              ; CET 安全指令（现代 GCC 默认插入，可忽略）
    pushq  %rbx          ; 保存 callee-saved 寄存器 rbx
    movq   %rdx, %rbx   ; 把第 3 个参数 dest 暂存进 rbx
    call   mult2@PLT     ; 调用 mult2(x, y)，结果存入 %rax
    movq   %rax, (%rbx) ; *dest = t，间接寻址
    popq   %rbx          ; 恢复 rbx
    ret
```

**三个关键点：**

1. **Linux x86-64 调用约定**：前 6 个整数参数依次使用 `rdi rsi rdx rcx r8 r9`；返回值放 `rax`
2. **`%rbx` 是 callee-saved**：`call mult2` 可能破坏 caller-saved 寄存器（含 `%rdx`），所以必须提前把 `dest` 存进 callee-saved 的 `%rbx`，并在函数末尾 `pop` 恢复
3. **`(%rbx)` 是间接寻址**：`movq %rax, (%rbx)` = 把 rax 的值写到 rbx 所指向的地址，对应 `*dest = t`

**`call` 地址全零的原因：**
`mstore.o` 是单独编译的目标文件，`mult2` 的地址要等链接时才能填上，这就是**重定位**（§7 详讲）。

---

## 三句话总结

- **最重要的概念**：`gcc -S` 产物含大量元信息，真正执行的指令用 `objdump -d` 才看得最清楚
- **最容易错的点**：`%rdx`（dest）要存进 `%rbx` 是因为 `call` 之后 caller-saved 寄存器可能被破坏，`%rbx` 是 callee-saved 才安全
- **工程对应**：ABI 调用约定是跨模块编译正确链接的基础；`objdump -d` 是 crash/core dump 分析的常用工具

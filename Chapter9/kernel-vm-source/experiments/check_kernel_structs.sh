#!/usr/bin/env bash
# check_kernel_structs.sh —— 免 root 核对本机内核头文件里 mm 子系统的真实结构
#
# 目的：把 CSAPP 第 9 章 / 旧教程里的「教学模型」拿到本机内核头文件树里对照，
#       亲眼确认 6.x 的几处关键变化（maple tree 取代红黑树、vm_next 已删、
#       5 级页表下钻宏、folio/page union）。全程不需要 root。
#
# 用法：bash check_kernel_structs.sh
set -u

# 定位本机内核头文件树（优先 /lib/modules/.../build，回退 /usr/src）
KREL="$(uname -r)"
HDR="/lib/modules/${KREL}/build"
[ -d "$HDR/include/linux" ] || HDR="/usr/src/linux-headers-${KREL}"
if [ ! -d "$HDR/include/linux" ]; then
  echo "找不到内核头文件树，请先安装 linux-headers-${KREL}" >&2
  exit 1
fi
echo "内核版本：${KREL}"
echo "头文件树：${HDR}"
echo

MMT="$HDR/include/linux/mm_types.h"
PGT="$HDR/include/linux/pgtable.h"
RMAP="$HDR/include/linux/rmap.h"

# 小工具：打印某文件里第一处命中的「行号: 行内容」，没命中给出醒目提示
show() { # $1=描述 $2=正则 $3=文件
  local hit
  hit="$(grep -nE "$2" "$3" 2>/dev/null | head -1)"
  if [ -n "$hit" ]; then
    printf '  [命中] %-34s %s\n' "$1" "$hit"
  else
    printf '  [缺失] %-34s (在 %s 未找到 /%s/)\n' "$1" "${3##*/}" "$2"
  fi
}

echo "== ① VMA 组织：maple tree 取代红黑树 =="
show "mm_struct.mm_mt (maple tree)" 'struct[[:space:]]+maple_tree[[:space:]]+mm_mt' "$MMT"
show "旧红黑树 mm_rb 是否还在"      '\bstruct[[:space:]]+rb_root[[:space:]]+mm_rb' "$MMT"
echo

echo "== ② vm_area_struct：旧链表字段 vm_next/vm_prev 应已删除 =="
# 这里期望「缺失」才是对的——旧教程画的链表在 6.x 不存在
show "vm_area_struct.vm_next" '\*[[:space:]]*vm_next' "$MMT"
show "vm_area_struct.vm_start" 'unsigned long[[:space:]]+vm_start' "$MMT"
show "vm_area_struct.anon_vma" 'struct[[:space:]]+anon_vma[[:space:]]*\*[[:space:]]*anon_vma' "$MMT"
echo

echo "== ③ 页表根 pgd 仍在 mm_struct =="
show "mm_struct.pgd" 'pgd_t[[:space:]]*\*[[:space:]]*pgd' "$MMT"
echo

echo "== ④ 5 级页表逐级下钻宏 =="
show "pgd_offset"        '\bpgd_offset\b'        "$PGT"
show "pte_offset_kernel" '\bpte_offset_kernel\b' "$PGT"
echo

echo "== ⑤ folio 与 page 的 union/overlay 关系 =="
show "struct folio 定义" 'struct[[:space:]]+folio[[:space:]]*\{' "$MMT"
echo

echo "== ⑥ rmap 反向映射结构 =="
show "struct anon_vma"       'struct[[:space:]]+anon_vma[[:space:]]*\{'       "$RMAP"
show "struct anon_vma_chain" 'struct[[:space:]]+anon_vma_chain[[:space:]]*\{' "$RMAP"
echo

echo "结论速记："
echo "  · mm_mt 命中、mm_rb 缺失、vm_next 缺失  →  6.x 已用 maple tree 单结构组织 VMA"
echo "  · pgd_offset/pte_offset_kernel 命中     →  §9.6 多级页表在代码里就是逐级 *_offset"
echo "  · struct folio 命中                     →  §9 书本的 struct page 已被 folio 包裹"

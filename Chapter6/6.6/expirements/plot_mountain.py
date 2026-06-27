#!/usr/bin/env python3
"""把 mountain.csv 渲染成存储器山：3D 曲面图 + 2D 热力图。
用法: python3 plot_mountain.py mountain.csv
输出: mountain_3d.png, mountain_heatmap.png（存到上级目录）
"""
import sys, os, csv
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import cm, font_manager

# 注册系统里的 Noto Sans CJK，让中文标签正常渲染
for fp in ["/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"]:
    if os.path.exists(fp):
        font_manager.fontManager.addfont(fp)
        plt.rcParams["font.family"] = "Noto Sans CJK JP"
plt.rcParams["axes.unicode_minus"] = False

CSV = sys.argv[1] if len(sys.argv) > 1 else "mountain.csv"
OUTDIR = os.path.dirname(os.path.dirname(os.path.abspath(CSV)))  # Chapter6/6.6

# ---- 读数据 ----
rows = list(csv.reader(open(CSV)))
strides = [int(h[1:]) for h in rows[0][1:]]          # 1..16
size_labels = [r[0] for r in rows[1:]]               # 16K..128M
Z = np.array([[float(x) for x in r[1:]] for r in rows[1:]]) / 1000.0  # → GB/s

# 工作集字节数（用于对数 X 轴）
def to_bytes(s):
    return int(s[:-1]) * (1024 if s[-1] == "K" else 1024 * 1024)
size_bytes = np.array([to_bytes(s) for s in size_labels])

# ---- 1) 3D 曲面 ----
fig = plt.figure(figsize=(13, 8.5))
ax = fig.add_subplot(111, projection="3d")
X, Y = np.meshgrid(strides, np.log2(size_bytes))     # 工作集取 log2 让台阶均匀
surf = ax.plot_surface(X, Y, Z, cmap=cm.jet, rstride=1, cstride=1,
                       linewidth=0.15, edgecolor=(0, 0, 0, 0.25),
                       antialiased=True)
# 让三面背景板透明、网格线变浅，减少视觉杂讯
for pane in (ax.xaxis, ax.yaxis, ax.zaxis):
    pane.pane.set_facecolor((1, 1, 1, 0))
    pane.pane.set_edgecolor((0.8, 0.8, 0.8, 1))
ax.grid(True, color=(0.9, 0.9, 0.9))

ax.set_xlabel("stride（步长，×8字节）", labelpad=16)
ax.set_ylabel("工作集大小", labelpad=24)
ax.set_zlabel("")                                    # 吞吐量由右侧色条标注，避免 z 轴标签与前方刻度撞在一起
ax.set_yticks(np.log2(size_bytes)[::2])
ax.set_yticklabels(size_labels[::2])
ax.set_zticks([0, 20, 40, 60, 80])                   # 精简 z 刻度
ax.tick_params(axis="x", pad=4)
ax.tick_params(axis="y", pad=10)
ax.tick_params(axis="z", pad=6)
ax.set_zlim(0, 90)
ax.set_box_aspect((1.3, 1.3, 0.62))                  # 压扁高度、拉开底面 → 山形更舒展
ax.set_title("存储器山 — Intel Core Ultra 7 255H (L1d=48K / L2=3M / L3=24M)",
             pad=20, fontsize=13)
# 对齐 CSAPP 图 6.41 朝向：小工作集(L1 山顶)在后方、大工作集(主存 谷底)朝观察者；
# stride 沿右侧递增，从山顶往右下滑是空间局部性坡，往左前滑是时间局部性坡
ax.invert_yaxis()
ax.view_init(elev=24, azim=-122)
fig.colorbar(surf, shrink=0.45, aspect=14, pad=0.02, label="读吞吐量 GB/s")
plt.subplots_adjust(left=0.02, right=0.92, top=0.95, bottom=0.05)
p3d = os.path.join(OUTDIR, "mountain_3d.png")
plt.savefig(p3d, dpi=140)
print("wrote", p3d)

# ---- 2) 2D 热力图（带 cache 边界标注）----
fig, ax = plt.subplots(figsize=(9, 6))
im = ax.imshow(Z, aspect="auto", cmap="jet", origin="upper")
ax.set_xticks(range(len(strides)))
ax.set_xticklabels(strides)
ax.set_yticks(range(len(size_labels)))
ax.set_yticklabels(size_labels)
ax.set_xlabel("stride（步长，×8字节）")
ax.set_ylabel("工作集大小")
ax.set_title("存储器山俯视热力图（越红越快）")
# 标注 cache 容量断崖：48K 在 32K/64K 之间，3M 在 2M/4M 之间，24M 在 16M/32M 之间
for label, lo, hi in [("L1d 48K", "32K", "64K"),
                      ("L2 3M", "2M", "4M"),
                      ("L3 24M", "16M", "32M")]:
    y = (size_labels.index(lo) + size_labels.index(hi)) / 2
    ax.axhline(y, color="white", ls="--", lw=1.2)
    ax.text(len(strides) - 0.4, y - 0.15, label, color="white",
            ha="right", va="bottom", fontsize=9, weight="bold")
fig.colorbar(im, label="GB/s")
plt.tight_layout()
phm = os.path.join(OUTDIR, "mountain_heatmap.png")
plt.savefig(phm, dpi=130)
print("wrote", phm)

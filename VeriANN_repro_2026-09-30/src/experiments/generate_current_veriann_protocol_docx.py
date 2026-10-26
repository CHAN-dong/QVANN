#!/usr/bin/env python3
"""Generate the current implemented VeriANN construction/query/verification DOCX."""

from __future__ import annotations

import csv
import zipfile
from datetime import datetime, timezone
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

from generate_adaptive_tree_docx import (
    DocBuilder,
    app_xml,
    arrow,
    box,
    content_types_xml,
    rels_xml,
    root_rels_xml,
    settings_xml,
    setup_ax,
    styles_xml,
)


BASE = Path(__file__).resolve().parent
ASSET_DIR = BASE / "figures_current_protocol"
OUT = BASE / "Current_Verifiable_ANN_Index_Query_Verification_CN.docx"


def fig_system_pipeline(path: Path):
    fig, ax = setup_ax((14, 8.2))
    ax.text(0.5, 0.96, "Current VeriANN: Offline Construction and Online Protocol",
            ha="center", fontsize=18, weight="bold", color="#17365D")

    ax.text(0.04, 0.82, "OFFLINE", fontsize=12, weight="bold", color="#7D3C98")
    offline = [
        (0.08, "Base vectors", "X, object IDs"),
        (0.25, "Candidate residual trees", "depth / fanout / codecs"),
        (0.44, "VO-constrained selection", "Recall + latency + build"),
        (0.63, "Selected tree", "codes + lists + Merkle roots"),
        (0.81, "Authenticated tier table", "efVO -> (p0,...,pL-1)"),
    ]
    for i, (x, title, sub) in enumerate(offline):
        box(ax, (x - 0.065, 0.68), (0.13, 0.16), f"{title}\n\n{sub}",
            fc=["#FDF2E9", "#EBF5FB", "#E8F8F5", "#F4ECF7", "#FEF9E7"][i],
            ec=["#CA6F1E", "#2874A6", "#148F77", "#7D3C98", "#B7950B"][i],
            fontsize=8.7)
        if i < len(offline) - 1:
            arrow(ax, (x + 0.067, 0.76), (offline[i + 1][0] - 0.069, 0.76))

    ax.plot([0.03, 0.97], [0.58, 0.58], color="#D5D8DC", linewidth=1.5)
    ax.text(0.04, 0.49, "ONLINE", fontsize=12, weight="bold", color="#21618C")
    online = [
        (0.08, "Request", "q, k, efVO tier"),
        (0.25, "Derive probes", "policy[e] -> p"),
        (0.43, "Stagewise top-p", "root -> residual levels"),
        (0.61, "Exact leaf search", "top-k in visited leaves"),
        (0.79, "Generate result + VO", "routing + leaf proofs"),
        (0.93, "Client verify", "replay fixed top-p"),
    ]
    for i, (x, title, sub) in enumerate(online):
        w = 0.115 if i < 5 else 0.10
        box(ax, (x - w / 2, 0.32), (w, 0.14), f"{title}\n\n{sub}",
            fc="#EBF5FB" if i != 5 else "#D5F5E3",
            ec="#2874A6" if i != 5 else "#1E8449", fontsize=8.1)
        if i < len(online) - 1:
            next_w = 0.115 if i + 1 < 5 else 0.10
            arrow(ax, (x + w / 2 + 0.002, 0.39),
                  (online[i + 1][0] - next_w / 2 - 0.003, 0.39))

    arrow(ax, (0.81, 0.675), (0.25, 0.465), color="#B7950B", lw=1.8, rad=0.12)
    arrow(ax, (0.63, 0.675), (0.43, 0.465), color="#7D3C98", lw=1.8, rad=-0.08)
    ax.text(0.50, 0.13,
            "No dynamic best-first queue.  One tier deterministically selects one per-level probe tuple.",
            ha="center", fontsize=10.5, color="#566573")
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def fig_residual_tree(path: Path):
    fig, ax = setup_ax((13.5, 8.0))
    ax.text(0.5, 0.96, "Balanced Residual Hierarchy (schematic)",
            ha="center", fontsize=18, weight="bold", color="#17365D")

    nodes = {
        "C0": (0.28, 0.78), "C1": (0.72, 0.78),
        "M0": (0.14, 0.57), "M1": (0.34, 0.57),
        "M2": (0.62, 0.57), "M3": (0.82, 0.57),
        "F0": (0.08, 0.33), "F1": (0.20, 0.33),
        "F2": (0.30, 0.33), "F3": (0.40, 0.33),
        "F4": (0.56, 0.33), "F5": (0.68, 0.33),
        "F6": (0.78, 0.33), "F7": (0.90, 0.33),
    }
    edges = [
        ("C0", "M0"), ("C0", "M1"), ("C1", "M2"), ("C1", "M3"),
        ("M0", "F0"), ("M0", "F1"), ("M1", "F2"), ("M1", "F3"),
        ("M2", "F4"), ("M2", "F5"), ("M3", "F6"), ("M3", "F7"),
    ]
    for a, b in edges:
        x1, y1 = nodes[a]
        x2, y2 = nodes[b]
        arrow(ax, (x1, y1 - 0.038), (x2, y2 + 0.038), color="#5D6D7E", lw=1.3)
    for name, (x, y) in nodes.items():
        if name.startswith("C"):
            fc, ec = "#F4ECF7", "#7D3C98"
        elif name.startswith("M"):
            fc, ec = "#D6EAF8", "#2874A6"
        else:
            fc, ec = "#D5F5E3", "#1E8449"
        box(ax, (x - 0.045, y - 0.035), (0.09, 0.07), name, fc=fc, ec=ec,
            fontsize=9, radius=0.008)

    for i, x in enumerate([0.08, 0.20, 0.30, 0.40, 0.56, 0.68, 0.78, 0.90]):
        box(ax, (x - 0.04, 0.13), (0.08, 0.07), f"objects\nL{i}",
            fc="#FEF9E7", ec="#B7950B", fontsize=7.5, radius=0.006)
        arrow(ax, (x, 0.292), (x, 0.203), color="#AAB7B8", lw=1.0)

    ax.text(0.03, 0.80, "absolute root code:\nQ(C)", fontsize=9.5, color="#7D3C98")
    ax.text(0.03, 0.54, "middle residual:\nQ(M - parent(M))", fontsize=9.5, color="#2874A6")
    ax.text(0.03, 0.29, "fine residual:\nQ(F - parent(F))", fontsize=9.5, color="#1E8449")
    box(ax, (0.38, 0.845), (0.24, 0.075),
        "Example selected by AutoVO\nSIFT100K: 256 -> 4096 -> 65536",
        fc="#F8F9F9", ec="#5D6D7E", fontsize=8.2)
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def fig_query_example(path: Path):
    fig, ax = setup_ax((14, 8.6))
    ax.text(0.5, 0.96, "Toy Stagewise Query: k=3, policy tier e=2 gives p=(2,3)",
            ha="center", fontsize=17, weight="bold", color="#17365D")

    roots = [("C0", 0.12, True), ("C1", 0.20, True), ("C2", 0.78, False), ("C3", 1.05, False)]
    xs = [0.18, 0.39, 0.61, 0.82]
    for (name, score, selected), x in zip(roots, xs):
        box(ax, (x - 0.065, 0.72), (0.13, 0.105), f"{name}\nscore={score:.2f}",
            fc="#D5F5E3" if selected else "#F2F3F4",
            ec="#1E8449" if selected else "#AAB7B8", fontsize=9)
    ax.text(0.04, 0.77, "Level 0\ntop-2", fontsize=10, weight="bold", color="#2874A6")

    fine = [
        ("F0", 0.08, 0.13, True), ("F1", 0.31, 0.24, True),
        ("F2", 0.66, 0.35, False), ("F3", 0.90, 0.46, False),
        ("F4", 0.16, 0.57, True), ("F5", 0.42, 0.68, False),
        ("F6", 0.84, 0.79, False), ("F7", 0.99, 0.90, False),
    ]
    for name, score, x, selected in fine:
        parent_x = 0.18 if x < 0.52 else 0.39
        arrow(ax, (parent_x, 0.715), (x, 0.555), color="#AAB7B8", lw=0.9)
        box(ax, (x - 0.046, 0.50), (0.092, 0.078), f"{name}\n{score:.2f}",
            fc="#D5F5E3" if selected else "#F2F3F4",
            ec="#1E8449" if selected else "#AAB7B8", fontsize=7.8, radius=0.006)
    ax.text(0.04, 0.525, "Level 1\nglobal top-3", fontsize=10, weight="bold", color="#2874A6")

    selected_leaves = [(0.23, "F0", "id11: 0.10\nid12: 0.40"),
                       (0.49, "F1", "id21: 0.23\nid22: 0.62"),
                       (0.75, "F4", "id41: 0.14\nid42: 0.51")]
    for x, name, objects in selected_leaves:
        box(ax, (x - 0.09, 0.25), (0.18, 0.115), f"{name} exact scan\n{objects}",
            fc="#EBF5FB", ec="#2874A6", fontsize=8.2)
    for x0, x1 in [(0.13, 0.23), (0.24, 0.49), (0.57, 0.75)]:
        arrow(ax, (x0, 0.495), (x1, 0.37), color="#2874A6", lw=1.2)

    box(ax, (0.28, 0.06), (0.44, 0.115),
        "Exact top-3 in visited leaves\n{id11:0.10, id41:0.14, id21:0.23}, tau=0.23",
        fc="#D5F5E3", ec="#1E8449", fontsize=10)
    ax.text(0.5, 0.205,
            "Green nodes are selected by authenticated quantized scores; gray nodes are rejected by the fixed top-p rule.",
            ha="center", fontsize=9, color="#566573")
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def fig_verification_flow(path: Path):
    fig, ax = setup_ax((12.5, 8.7))
    ax.text(0.5, 0.96, "Client Verification Flow",
            ha="center", fontsize=18, weight="bold", color="#17365D")
    steps = [
        (0.84, "1. Authenticate index version, codec parameters and tier table"),
        (0.69, "2. Derive p from efVO tier; check transcript dimensions"),
        (0.54, "3. Recompute quantized routing scores and verify every stagewise top-p"),
        (0.39, "4. Verify leaf commitments, progressive bounds and exact result objects"),
        (0.24, "5. Recompute top-k and tau inside visited leaves"),
        (0.09, "6. Accept result or reject transcript"),
    ]
    colors = [
        ("#F4ECF7", "#7D3C98"), ("#FEF9E7", "#B7950B"),
        ("#D6EAF8", "#2874A6"), ("#E8F8F5", "#148F77"),
        ("#EBF5FB", "#21618C"), ("#D5F5E3", "#1E8449"),
    ]
    for i, ((y, label), (fc, ec)) in enumerate(zip(steps, colors)):
        box(ax, (0.18, y - 0.052), (0.64, 0.104), label, fc=fc, ec=ec, fontsize=10)
        if i < len(steps) - 1:
            arrow(ax, (0.5, y - 0.055), (0.5, steps[i + 1][0] + 0.055))

    box(ax, (0.835, 0.48), (0.145, 0.17),
        "Routing proof\ncenter/residual blocks\nmetadata + multiproofs",
        fc="#F8F9F9", ec="#5D6D7E", fontsize=8.2)
    arrow(ax, (0.835, 0.565), (0.82, 0.565), color="#5D6D7E")
    box(ax, (0.835, 0.26), (0.145, 0.17),
        "Leaf proof\nsummary/refine/exact\nobject Merkle proofs",
        fc="#F8F9F9", ec="#5D6D7E", fontsize=8.2)
    arrow(ax, (0.835, 0.345), (0.82, 0.405), color="#5D6D7E")
    ax.text(0.04, 0.04,
            "Verified claim: correct execution of the committed ANN policy, not global exact top-k over unprobed leaves.",
            fontsize=9.5, color="#C0392B")
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def read_table_rows(csv_path: Path):
    with csv_path.open(newline="", encoding="utf-8") as f:
        return [r for r in csv.DictReader(f) if r["kind"] == "table"]


def fig_tradeoff(path: Path):
    datasets = [
        ("SIFT20K", BASE / "compact_allocator_table_sift20k.csv"),
        ("SIFT50K", BASE / "compact_allocator_table_sift50k.csv"),
        ("SIFT100K", BASE / "compact_allocator_table_sift100k.csv"),
        ("HierGauss", BASE / "compact_allocator_table_hiergauss.csv"),
    ]
    fig, axes = plt.subplots(1, 2, figsize=(13.5, 5.5), dpi=180)
    fig.patch.set_facecolor("white")
    colors = ["#2874A6", "#148F77", "#7D3C98", "#CA6F1E"]
    for (name, csv_path), color in zip(datasets, colors):
        rows = read_table_rows(csv_path)
        recall = [float(r["test_recall"]) for r in rows]
        vo = [float(r["actual_total_kib"]) for r in rows]
        latency = [float(r["ann_query_ms"]) for r in rows]
        default_vo = vo[2]
        default_latency = latency[2]
        axes[0].plot([x / default_vo for x in vo], recall, marker="o", label=name,
                     color=color, linewidth=2)
        axes[1].plot([x / default_latency for x in latency], recall, marker="o", label=name,
                     color=color, linewidth=2)
    axes[0].set_title("Recall vs normalized complete VO")
    axes[0].set_xlabel("VO / default-tier VO")
    axes[0].set_ylabel("Recall@100")
    axes[1].set_title("Recall vs normalized ANN latency")
    axes[1].set_xlabel("Latency / default-tier latency")
    axes[1].set_ylabel("Recall@100")
    for ax in axes:
        ax.grid(True, alpha=0.25)
        ax.set_ylim(0.91, 1.002)
        ax.legend(fontsize=8)
    fig.suptitle("Authenticated efVO Tier Trade-off", fontsize=16, weight="bold",
                 color="#17365D")
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def add_cover(d: DocBuilder):
    d.paragraph("当前可验证 ANN 协议", style="Title")
    d.paragraph("自适应 Residual 索引构建、efVO 分档查询与客户端验证", style="Subtitle")
    d.paragraph("实现与实验说明 · 2026-09-01", align="center",
                color="5B6573", size=11, after=280)
    d.paragraph("文档范围", bold=True, color="1F4E79", size=12,
                border="2E75B6", after=70)
    d.paragraph(
        "本文只描述当前已经实现和实验的方案：有限候选空间中的 AutoVO residual hierarchy、"
        "五档认证 probe policy、普通逐层 top-p 查询，以及对应的 routing/leaf VO。"
        "此前讨论的任意 dendrogram、逐节点 Keep/Collapse 和 authenticated best-first 队列不属于当前实现。",
        color="17365D", border="2E75B6", after=170,
    )
    d.paragraph("一句话概括", style="Heading2")
    d.paragraph(
        "离线自动选择一棵 VO 较小且 Recall 达标的 residual tree，并给它附加一个极小的"
        " efVO→逐层 probe 策略表；在线查询和验证都执行相同的固定 stagewise top-p。",
        bold=True, color="1F4E79",
    )
    d.paragraph("阅读导航", style="Heading2")
    d.table(
        ["章节", "内容"],
        [
            ["第 1 节", "系统组件、信任边界和端到端数据流"],
            ["第 2 节", "当前索引如何自动选择细簇数、层数、分支数和 codec"],
            ["第 3 节", "用户如何用一个 efVO 档位控制逐层 probe"],
            ["第 4 节", "客户端如何验证 routing top-p 和 leaf/object proof"],
            ["第 5–6 节", "完整构建例子、查询例子与验证例子"],
            ["第 7–8 节", "正式实验结果、适用性和尚未完成的保证"],
        ],
        widths=[1600, 7400],
        font_size=9,
    )


def add_overview(d: DocBuilder, figures):
    d.paragraph("1. 系统总览与保证目标", style="Heading1")
    d.image(figures["system"], 6.75, "图 1　当前方案的离线构建、在线查询和验证关系。")
    d.paragraph("1.1 四个核心组件", style="Heading2")
    d.table(
        ["组件", "职责", "输出"],
        [
            ["AutoVO 构建器", "比较 1–4 层 residual 候选结构", "选中的层数、各层簇数、codec"],
            ["Probe policy 构建器", "在独立 calibration queries 上选择五档 probe tuple", "efVO tier→(p₀,…,pL−1)"],
            ["ANN 查询器", "按档位执行逐层 top-p，再精确扫描所选叶簇", "top-k、τ、routing/leaf trace"],
            ["VO 生成与验证", "认证索引材料，证明服务器遵循所承诺的查询策略", "result + routing VO + leaf VO"],
        ],
        widths=[1800, 4300, 2900],
        font_size=8.5,
    )
    d.paragraph("1.2 信任与承诺", style="Heading2")
    d.bullet("数据拥有者构建索引，发布或签名 index root、版本、codec 参数和 efVO 策略表 commitment。")
    d.bullet("服务器可以不可信；它负责查询、结果和 VO 生成。")
    d.bullet("客户端信任签名/根哈希，不信任服务器返回的 center codes、对象或选择路径。")
    d.bullet("验证目标是“服务器正确执行已承诺的 ANN policy”，不是证明未访问叶中的真实对象一定比结果更远。")
    d.paragraph("1.3 三类性质", style="Heading2")
    d.table(
        ["性质", "当前状态", "含义"],
        [
            ["数据完整性", "协议设计支持", "center/residual/object bytes 与索引 commitment 一致"],
            ["查询执行完整性", "stagewise top-p 可重算", "每层选择数量和量化排序遵守 policy[e]"],
            ["叶内结果完整性", "progressive certificate", "访问叶内未返回对象不能击败 τ"],
            ["全局 Recall", "统计校准", "未访问叶由 ANN routing 排除，因此 Recall 不是密码学确定保证"],
            ["独立恶意 verifier", "尚未完成", "当前有完整字节核算和 bound containment 检查，但缺少独立 parser/adversarial tests"],
        ],
        widths=[2000, 2100, 4900],
        font_size=8.3,
    )


def add_index_construction(d: DocBuilder, figures):
    d.paragraph("2. 当前索引构建算法", style="Heading1")
    d.paragraph("2.1 与早期 dendrogram 设想的区别", style="Heading2")
    d.paragraph(
        "当前实现确实先得到足够细的叶簇，再把 fine centers 自底向上分组为 residual hierarchy；"
        "但它不是任意二叉 dendrogram，也没有逐节点 Keep/Collapse。树在每个候选中是层级平衡的，"
        "自适应性来自对不同 fine 数、深度、fanout、codec 和 probes 的系统搜索。"
    )
    d.image(figures["tree"], 6.5, "图 2　三层 balanced residual hierarchy 示意。真实树只存量化根中心和相对父节点的 residual code。")
    d.paragraph("2.2 输入参数", style="Heading2")
    d.table(
        ["参数", "当前含义"],
        [
            ["X, N, d", "base vectors、数据规模和维数"],
            ["k / Recall target", "例如 k=100、Recall@100≥0.99"],
            ["max_fine", "允许的最大 fine leaf 数；当前不是按 B_leaf 字节递归停止"],
            ["max_depth", "搜索 1–4 个 routing tiers"],
            ["fanout candidates", "2 层：32/64/128；3 层：8/16/32；4 层：4/8"],
            ["codec candidates", "root SQ4；中间 residual PQ24；末级 residual PQ16/PQ24"],
            ["budgets", "查询延迟和所选索引构建时间上限"],
        ],
        widths=[2400, 6600],
        font_size=8.5,
    )
    d.paragraph("2.3 生成 fine leaves", style="Heading2")
    d.paragraph(
        "令 Kmax 为不超过 min(N,max_fine) 的最大 2 的幂。当前只比较 Kmax 和 Kmax/2（足够大时）。"
        "对原始向量做 balanced/hierarchical k-means 得到 fine inverted lists L₀…LK−1 和 fine centers。"
    )
    d.code(
        "Kmax = largest_power_of_two( min(N, max_fine) )\n"
        "fine_candidates = {Kmax/2, Kmax}\n"
        "lists, fine_centers = HKMEANS_BALANCED(X, Kfine)"
    )
    d.paragraph("2.4 自底向上形成 residual parents", style="Heading2")
    d.paragraph(
        "对 fine centers 做 balanced grouping 得到上一层 parents；再对 parent centers 重复，直到候选深度。"
        "父中心是其孩子中心的均值。根层存绝对量化码，其他层存相对父中心的 residual："
    )
    d.code(
        "center(parent) = mean{ center(child) }\n"
        "residual(child) = center(child) - center(parent)\n"
        "decoded(child) = decoded(parent) + decode_Q(residual(child))"
    )
    d.paragraph(
        "每个 fine leaf 保留对象倒排表；routing tree 只负责决定访问哪些 lists，最后距离仍对访问对象做精确计算。"
    )
    d.paragraph("2.5 候选结构和 probe 搜索", style="Heading2")
    d.paragraph(
        "每个结构在小 probe 网格上运行 calibration queries。目标优先满足 Recall、延迟和构建预算，"
        "再最小化 VO cost。当前选择门槛通常为 target+margin，例如 0.99+0.002。"
    )
    d.code(
        "minimize   C_route(structure, p) + C_leaf_proxy(structure, p)\n"
        "subject to Recall_cal@k >= target + margin\n"
        "           query_ms <= latency_budget\n"
        "           selected_index_build_s <= build_budget"
    )
    d.paragraph("2.6 VO cost model", style="Heading2")
    d.code(
        "C_route = codec parameters/models + root codes\n"
        "        + selected-parent residual child blocks\n"
        "        + selected-node metadata + 32 * MerkleSiblingCount\n\n"
        "C_leaf_proxy = visited_objects * (quantized record + hash + id + framing)"
    )
    d.paragraph(
        "固定宽度和 canonical Huffman routing 表示取较小者。leaf proxy 只用于廉价排序候选；"
        "最终选中方案必须运行完整 progressive leaf VO，报告不得用 proxy 替代。"
    )
    d.paragraph("2.7 深度为何会自动停止", style="Heading2")
    d.paragraph(
        "每增加一层会减少叶对象访问，但会新增 codec/model、residual blocks、metadata 和 Merkle proofs。"
        "搜索器直接比较完整目标，因此第三或第四层只有在总 VO 下降且 Recall 达标时才被保留。"
        "当前是有限深度候选选择，不是数学意义上的任意深度全局最优。"
    )
    d.paragraph("2.8 五档认证 probe policy", style="Heading2")
    d.paragraph(
        "选出树后，围绕默认 probe tuple 生成全局 scale×逐层 tilt 候选。"
        "在 120 条独立 calibration queries 上，为五个成本档选择预算内 Recall 最高的 tuple。"
    )
    d.table(
        ["策略字段", "存储内容"],
        [
            ["policy version", "档位定义、k、distance/codec 版本"],
            ["tier e", "例如 {0.75,0.90,1.00,1.15,1.30} 的离散编号"],
            ["probe tuple", "该档位的 (p₀,…,pL−1)"],
            ["commitment", "策略表哈希纳入 index root 或签名元数据"],
        ],
        widths=[2300, 6700],
        font_size=8.5,
    )
    d.paragraph(
        "两层五档表按当前序列化估算为 64 B，三层为 84 B。客户端缓存时不进入每查询 VO；"
        "无状态重传最多增加约 0.082 KiB。"
    )
    d.paragraph("2.9 Commitment 结构", style="Heading2")
    d.bullet("索引根绑定 dataset/version、tree shape、codec 参数、policy table 和 root routing commitment。")
    d.bullet("每个内部节点 descriptor 绑定 child count、child residual-code block 和局部 Merkle root。")
    d.bullet("每个 fine leaf descriptor 绑定对象 list/root 和 progressive certificate 参数。")
    d.bullet("它不是“每个量化 level 额外建立一棵互不相关的 Merkle tree”，而是由父 descriptor 递归绑定子块。")
    d.paragraph("2.10 构建伪代码", style="Heading2")
    d.code(
        "BUILD-AUTOVO-INDEX(X, target, budgets):\n"
        "  candidates = []\n"
        "  for Kfine in fine_candidates(X):\n"
        "    lists, fine = balanced_hkmeans(X, Kfine)\n"
        "    for depth, fanout, codecs in structural_grid:\n"
        "      T = build_residual_parents_bottom_up(fine, depth, fanout)\n"
        "      encode_root_and_residuals(T, codecs)\n"
        "      p = search_probes_on_calibration(T, target, budgets)\n"
        "      candidates += evaluate_VO_proxy(T, p)\n"
        "  Tstar, p_anchor = best_feasible(candidates)\n"
        "  policy = build_five_tier_probe_table(Tstar, p_anchor)\n"
        "  build_nested_commitments(Tstar, policy)\n"
        "  run_complete_VO_evaluator(Tstar, policy)\n"
        "  return Tstar, policy, signed_index_root"
    )


def add_query_protocol(d: DocBuilder, figures):
    d.paragraph("3. 在线查询协议", style="Heading1")
    d.paragraph("3.1 用户接口", style="Heading2")
    d.code("QUERY(q, k, efVO_tier, index_version)")
    d.paragraph(
        "用户不提交每层 pc/pm/pf，也不提交动态队列预算。客户端和服务器从已认证 policy table 得到同一个 probe tuple。"
        "档位越高，总体上允许更大的 VO 和延迟，以换取更高 Recall；单层 probe 不要求逐档单调，因为预算可以在层间重新分配。"
    )
    d.paragraph("3.2 Stagewise top-p", style="Heading2")
    d.image(figures["query"], 6.75, "图 3　两层教学查询。每层做一次普通 top-p；第二层是在已选父节点孩子的并集上整体取 top-p。")
    d.code(
        "STAGEWISE-QUERY(q, k, tier e):\n"
        "  p = authenticated_policy[e]\n"
        "  S0 = top_p0( quantized_distance(q, all_root_codes) )\n"
        "  decoded = decode(S0)\n"
        "  for level l = 1 ... L-1:\n"
        "      U = union of children of selected parents\n"
        "      score each child with parent-relative residual ADC\n"
        "      Sl = global_top_pl(U)\n"
        "      decode selected centers for next level\n"
        "  visit object lists named by S(L-1)\n"
        "  compute exact object distances and retain top-k\n"
        "  return result, tau, routing_trace, leaf_trace"
    )
    d.paragraph("3.3 为什么第二层是 global top-p", style="Heading2")
    d.paragraph(
        "若对每个父节点分别取固定数量，访问量会随父节点数量相乘，并把预算锁死在 fanout 上。"
        "当前协议把所有已选父节点的孩子合成候选集合，再取一个全局 p_l，使逐层总访问数可直接由 policy 控制。"
    )
    d.paragraph("3.4 查询结束条件", style="Heading2")
    d.paragraph(
        "当前没有 best-first 的动态 LB 停止条件。routing 在执行完 policy 指定的最后一层 top-p 后结束；"
        "对象搜索在所有所选 leaf lists 扫描完成后结束。然后 exact top-k heap 给出 τ。"
    )
    d.code(
        "routing_stop := last routing level completed\n"
        "object_stop  := every object in selected leaf lists is processed"
    )
    d.paragraph("3.5 查询与 VO 生成是分开的", style="Heading2")
    d.paragraph(
        "是分开的。查询首先产生不可变 trace：每层 selected node IDs、选中 leaf lists、精确结果 IDs 和 τ。"
        "VO generator 随后根据 trace 回到认证索引，收集必须的 center/residual blocks、metadata、Merkle multiproofs"
        " 和 leaf certificates。这样搜索热路径不需要边搜边序列化证明。"
    )
    d.table(
        ["阶段", "主要输入", "主要输出"],
        [
            ["ANN search", "q、policy[e]、量化索引、对象 lists", "selected IDs、top-k、τ、trace"],
            ["VO generation", "trace、Merkle material、leaf quantizer", "routing VO、leaf VO、framing"],
            ["Client verification", "q、e、result、VO、trusted root", "accept / reject"],
        ],
        widths=[1900, 3800, 3300],
        font_size=8.5,
    )


def add_verification_protocol(d: DocBuilder, figures):
    d.paragraph("4. 客户端验证协议", style="Heading1")
    d.image(figures["verify"], 6.0, "图 4　客户端验证流程。没有优先队列 replay；主要额外计算是量化距离和每层 top-p 比较。")
    d.paragraph("4.1 VO 的逻辑组成", style="Heading2")
    d.table(
        ["VO 部分", "包含内容", "验证目标"],
        [
            ["Header", "index/policy version、k、tier、各层 selected count、τ", "请求和 transcript 一致"],
            ["Routing parameters", "codec models/format、必要的 root codes", "可确定性重算量化距离"],
            ["Routing blocks", "已展开父节点的完整 child residual blocks", "没有在该父节点下隐藏孩子"],
            ["Routing metadata", "selected IDs、parent IDs、position、framing", "层级关系和排序确定"],
            ["Merkle multiproofs", "root/child block 的 sibling hashes", "所有 routing bytes 属于承诺索引"],
            ["Leaf certificates", "summary/base/refine/exact payload、对象 IDs、局部 proofs", "访问叶内未返回对象不能击败 τ"],
            ["Result objects", "返回对象及精确距离所需内容", "重算 top-k 和 τ"],
        ],
        widths=[1900, 3800, 3300],
        font_size=8.0,
    )
    d.paragraph("4.2 Routing top-p 验证", style="Heading2")
    d.paragraph(
        "验证者先从 policy[e] 得到 p。根层重算所有认证 root codes 的量化 score，检查 selected root IDs"
        " 正好是确定性排序下的前 p₀ 个。对下一层，验证所选父节点 child blocks 的 commitment，"
        "重算其 residual scores，并检查 selected children 是并集中的全局前 p_l 个。"
    )
    d.code(
        "VERIFY-TOP-P(candidates U, selected S, p):\n"
        "  require |S| = p\n"
        "  authenticate every candidate descriptor required by the stage\n"
        "  recompute key(u) = (quantized_score(q,u), node_id)\n"
        "  require sorted(S) = first_p(sorted(U))"
    )
    d.paragraph(
        "实现可用“最大 selected key ≤ 最小 rejected key”加 selected membership 检查等价验证，"
        "但在存在相等 score 时必须把 node_id tie-break 纳入 key。"
    )
    d.paragraph("4.3 Leaf/object 验证", style="Heading2")
    d.paragraph(
        "对所选 leaf lists，客户端验证对象 commitment。progressive certificate 先用低位宽 summary 给出安全下界；"
        "靠近 τ 的对象按需提升位宽或返回 lossless tail/精确对象。对返回结果重算精确距离并重建 top-k。"
    )
    d.code(
        "for object x in every visited leaf:\n"
        "  authenticate x or its progressive certificate\n"
        "  if x is not returned:\n"
        "      require certified_lower_bound(q,x) > tau\n"
        "for returned object r:\n"
        "  recompute exact_distance(q,r)\n"
        "require returned set is exact top-k within all visited leaves"
    )
    d.paragraph("4.4 验证的精确语义", style="Heading2")
    d.paragraph(
        "客户端证明的是：服务端按照认证量化路由 policy 选择了正确的叶，并在这些叶中返回了精确 top-k。"
        "未被 probe 的叶只需证明它们在量化 routing 排序中被排除；协议不声称其真实对象距离一定大于 τ。"
        "因此完整性是确定的，Recall 仍由 calibration/test 实验衡量。"
    )
    d.paragraph("4.5 验证伪代码", style="Heading2")
    d.code(
        "VERIFY(q, k, tier e, result R, VO, trusted_root):\n"
        "  authenticate index version, codecs and policy table\n"
        "  p = policy[e]\n"
        "  for each routing level l:\n"
        "      authenticate required root/child blocks and metadata\n"
        "      recompute residual ADC scores\n"
        "      verify deterministic global top-p_l selection\n"
        "  authenticate selected leaf roots and object certificates\n"
        "  verify exact top-k inside the union of visited leaves\n"
        "  verify tau, result IDs and transcript framing\n"
        "  accept iff every check succeeds"
    )
    d.paragraph("4.6 客户端复杂度", style="Heading2")
    d.table(
        ["项目", "复杂度/开销"],
        [
            ["策略解析", "O(L)，表查找；缓存时 0 query bytes"],
            ["Routing 计算", "与 VO 中认证的候选 center/residual 数线性，使用相同 SQ/PQ ADC"],
            ["排序检查", "可排序 O(M log M)，或 selection/阈值检查约 O(M)"],
            ["Merkle hashes", "与 multiproof sibling 和已认证 blocks 数线性"],
            ["Leaf 验证", "与 visited objects 的证书层级和 exact tail 数线性"],
            ["动态状态", "不需要跨层 best-first priority queue"],
        ],
        widths=[2600, 6400],
        font_size=8.5,
    )


def add_examples(d: DocBuilder):
    d.paragraph("5. 索引构建例子", style="Heading1")
    d.paragraph("5.1 教学数据", style="Heading2")
    d.paragraph(
        "假设有 N=32 个二维对象，fine clustering 得到 16 个叶簇，每簇约 2 个对象；"
        "目标为 Recall@3≥0.99。下面字节数用于解释 cost model，不是正式实验数据。"
    )
    d.table(
        ["候选", "结构", "probes", "Cal Recall", "Routing VO", "Leaf VO", "总 VO", "结论"],
        [
            ["A", "Flat 16", "6", "0.991", "3.4 KiB", "1.8 KiB", "5.2 KiB", "可行"],
            ["B", "4→16", "2→3", "0.992", "1.4 KiB", "1.1 KiB", "2.5 KiB", "选择"],
            ["C", "2→4→16", "1→2→3", "0.992", "2.0 KiB", "0.9 KiB", "2.9 KiB", "拒绝额外层"],
        ],
        widths=[700, 1300, 1300, 1300, 1500, 1300, 1300, 1300],
        font_size=7.8,
    )
    d.paragraph(
        "候选 C 比 B 少 0.2 KiB 叶对象，但多 0.6 KiB routing/model/proof，因此总 VO 反而更大。"
        "这正是“是否继续加层”由完整 VO trade-off 决定的直觉。"
    )
    d.paragraph("5.2 Residual 编码示例", style="Heading2")
    d.table(
        ["节点", "中心", "父节点", "存储内容"],
        [
            ["C0", "(1.0, 1.0)", "root", "SQ4 absolute code Q(1.0,1.0)"],
            ["F0", "(0.8, 1.1)", "C0", "PQ residual Q(−0.2,+0.1)"],
            ["F1", "(1.4, 0.9)", "C0", "PQ residual Q(+0.4,−0.1)"],
            ["C1", "(4.0, 1.2)", "root", "SQ4 absolute code Q(4.0,1.2)"],
            ["F4", "(3.8, 1.0)", "C1", "PQ residual Q(−0.2,−0.2)"],
        ],
        widths=[1000, 1800, 1600, 4600],
        font_size=8.5,
    )
    d.paragraph("5.3 为选中树建立五档策略", style="Heading2")
    d.table(
        ["efVO tier", "probe tuple", "解释"],
        [
            ["0", "1→2", "最小 VO，适合探索性查询"],
            ["1", "1→3", "略多 fine leaves"],
            ["2（默认）", "2→3", "满足 Recall 目标的稳定档"],
            ["3", "2→5", "提高边界查询覆盖"],
            ["4", "3→7", "高 Recall/高 VO 档"],
        ],
        widths=[1800, 2300, 4900],
        font_size=8.6,
    )
    d.paragraph(
        "策略表和结构一起承诺。查询时用户只提交 tier=2，客户端无需从 VO 接收一个大模型，"
        "也无需相信服务器自行选择的 probe tuple。"
    )
    d.paragraph("5.4 真实 SIFT100K 结构", style="Heading2")
    d.table(
        ["项目", "正式结果"],
        [
            ["自动结构", "256→4,096→65,536"],
            ["codec", "SQ4→PQ24→PQ16"],
            ["默认 probes", "102→653→1,254"],
            ["Recall@100", "0.99138"],
            ["完整总 VO", "583.34 KiB"],
            ["ANN 查询 / VO 生成", "3.316 ms / 2.227 ms"],
            ["索引构建 / probe policy 搜索", "6.32 s / 73.19 s"],
        ],
        widths=[3000, 6000],
        font_size=8.8,
    )

    d.paragraph("6. 查询与验证例子", style="Heading1")
    d.paragraph("6.1 查询输入", style="Heading2")
    d.code("q = (1.2, 1.0), k = 3, efVO tier = 2, policy[2] = (2,3)")
    d.paragraph("6.2 根层 top-2", style="Heading2")
    d.table(
        ["Root", "认证量化 score", "排序", "选择"],
        [
            ["C0", "0.12", "1", "是"],
            ["C1", "0.20", "2", "是"],
            ["C2", "0.78", "3", "否"],
            ["C3", "1.05", "4", "否"],
        ],
        widths=[1800, 2600, 1800, 2800],
        font_size=8.7,
    )
    d.paragraph(
        "验证者认证四个 root codes，并检查 max(selected key)=0.20 小于 min(rejected key)=0.78；"
        "若 score 相等则同时比较 node_id。"
    )
    d.paragraph("6.3 Fine 层 global top-3", style="Heading2")
    d.table(
        ["Fine", "父节点", "认证 residual score", "选择"],
        [
            ["F0", "C0", "0.08", "是"],
            ["F4", "C1", "0.16", "是"],
            ["F1", "C0", "0.31", "是"],
            ["F5", "C1", "0.42", "否"],
            ["F2", "C0", "0.66", "否"],
            ["F6", "C1", "0.84", "否"],
            ["F3", "C0", "0.90", "否"],
            ["F7", "C1", "0.99", "否"],
        ],
        widths=[1800, 1800, 3000, 2400],
        font_size=8.4,
    )
    d.paragraph(
        "验证者先验证 C0/C1 的完整 child blocks，然后在八个孩子的并集上重算 score。"
        "top-3 为 F0、F4、F1，selected 最大 score 0.31，小于 rejected 最小 score 0.42。"
    )
    d.paragraph("6.4 精确对象搜索", style="Heading2")
    d.table(
        ["Leaf", "对象及精确距离", "处理"],
        [
            ["F0", "id11:0.10，id12:0.40", "id11 入结果"],
            ["F4", "id41:0.14，id42:0.51", "id41 入结果"],
            ["F1", "id21:0.23，id22:0.62", "id21 入结果"],
        ],
        widths=[1600, 4300, 3100],
        font_size=8.7,
    )
    d.paragraph(
        "返回 R={id11,id41,id21}，τ=0.23。leaf certificates 证明访问叶中的 id12/id42/id22"
        " 距离下界或精确距离均大于 τ。"
    )
    d.paragraph("6.5 客户端最终接受条件", style="Heading2")
    d.table(
        ["检查", "本例结果"],
        [
            ["policy commitment", "tier=2 确实映射到 (2,3)"],
            ["root top-p", "C0/C1 是认证 key 的前 2"],
            ["fine top-p", "F0/F4/F1 是认证 key 的前 3"],
            ["Merkle commitments", "所有 root/child/leaf bytes 均属于 signed index"],
            ["leaf completeness", "访问叶内未返回对象不能击败 τ=0.23"],
            ["result order", "精确距离排序为 0.10、0.14、0.23"],
            ["结论", "接受：正确执行 tier=2 ANN policy"],
        ],
        widths=[3000, 6000],
        font_size=8.7,
    )
    d.paragraph(
        "注意：客户端没有证明 F2–F7 中真实对象一定比 0.23 更远；它证明这些叶按照已承诺的"
        "量化 stagewise top-p policy 被正确排除。这正是 ANN Recall 与密码学执行正确性的分界。",
        bold=True, color="7B241C", border="C0392B",
    )


def add_results(d: DocBuilder, figures):
    d.paragraph("7. 当前实验结果", style="Heading1")
    d.image(figures["tradeoff"], 6.7, "图 5　五档 policy 在四组数据上的 Recall–VO 和 Recall–latency 权衡。横轴以默认档归一化。")
    d.paragraph("7.1 默认档完整协议结果", style="Heading2")
    d.table(
        ["数据", "结构 / probes", "Recall@100", "Routing", "Leaf", "总 VO", "查询", "VO 生成"],
        [
            ["SIFT20K", "512→16384 / 133→360", "0.99154", "210.27 KiB", "53.65 KiB", "263.92 KiB", "1.093 ms", "0.651 ms"],
            ["SIFT50K", "512→32768 / 102→721", "0.99016", "278.11 KiB", "127.48 KiB", "405.59 KiB", "1.338 ms", "1.346 ms"],
            ["SIFT100K", "256→4096→65536 / 102→653→1254", "0.99138", "380.71 KiB", "202.63 KiB", "583.34 KiB", "3.316 ms", "2.227 ms"],
            ["HierGauss50K", "64→1024→16384 / 5→30→100", "0.99178", "44.79 KiB", "15.95 KiB", "60.73 KiB", "0.159 ms", "0.246 ms"],
        ],
        widths=[1300, 2600, 1200, 1300, 1300, 1300, 1200, 1200],
        font_size=7.2,
    )
    d.paragraph("所有四组完整 leaf proof 检查均为 object_violations=0。", bold=True)
    d.paragraph("7.2 SIFT100K 五档实际映射", style="Heading2")
    d.table(
        ["tier", "probes", "Recall@100", "总 VO", "查询"],
        [
            ["0.75", "72→460→883", "0.97810", "446.51 KiB", "2.313 ms"],
            ["0.90", "71→585→1124", "0.98714", "526.20 KiB", "2.791 ms"],
            ["1.00", "102→653→1254", "0.99138", "583.34 KiB", "3.316 ms"],
            ["1.15", "95→784→1505", "0.99494", "664.32 KiB", "3.763 ms"],
            ["1.30", "95→784→1926", "0.99684", "729.90 KiB", "4.046 ms"],
        ],
        widths=[1300, 2600, 1900, 1900, 1300],
        font_size=8.7,
    )
    d.paragraph(
        "根层 probe 并非逐档单调，例如 72→71→102→95；这是允许的。更高预算可以把更多资源转移到"
        "更有效的中/细层。策略表认证的是整个 tuple，用户看到的总体 Recall/VO/延迟保持有意义的单参数权衡。"
    )
    d.paragraph("7.3 构建成本", style="Heading2")
    d.table(
        ["数据", "所选索引构建", "额外五档策略搜索", "策略表大小"],
        [
            ["SIFT20K", "1.10 s", "7.62 s", "64 B"],
            ["SIFT50K", "2.62 s", "10.14 s", "64 B"],
            ["SIFT100K", "6.32 s", "73.19 s", "84 B"],
            ["HierGauss50K", "0.84 s", "2.67 s", "84 B"],
        ],
        widths=[2200, 2300, 2800, 1700],
        font_size=8.7,
    )
    d.paragraph(
        "73 s 是未缓存 162 个三层候选中心 score 的原型成本，适合静态索引的一次性离线校准；"
        "高频更新场景应缓存共享 score、使用坐标搜索或减少候选。"
    )
    d.paragraph("7.4 为什么不用连续预测模型", style="Heading2")
    d.paragraph(
        "实验过 anchored quadratic probe model。它在 SIFT 默认档接近最优，但 HierGauss 低档的层间"
        "资源分配出现跳变：0.9 档 Recall 仅 0.94968，而五档表为 0.98082。"
        "因此最终选择 64–84 B 的离散表，而不是继续增加模型复杂度。"
    )


def add_limits(d: DocBuilder):
    d.paragraph("8. 适用性、限制与论文表述", style="Heading1")
    d.paragraph("8.1 当前可以支持的结论", style="Heading2")
    d.bullet("有限候选空间内可自动选择不同 fine 数、1–4 层结构、fanout、codec 和默认 probes。")
    d.bullet("五档 efVO policy 提供一个简单、可认证、无需队列重放的 Recall–VO–延迟参数。")
    d.bullet("默认档相对原固定 probe AutoVO 的 Recall 最大变化为 −0.00076，VO 最大增加 2.50%。")
    d.bullet("策略元数据极小；缓存后不会增加每查询 VO。")
    d.paragraph("8.2 尚不能支持的结论", style="Heading2")
    d.bullet("当前不是任意深度、任意节点分支的全局最优 dendrogram/collapse 算法。")
    d.bullet("Recall 来自 120 条 calibration queries 和 500 条独立 test queries，不能保证单条 adversarial query。")
    d.bullet("只有 SIFT 一个标准真实数据集和一个合成层次数据集，仍需 GIST/Deep/Glove。")
    d.bullet("尚未完成独立 parser、恶意 transcript 注入和端到端 adversarial verifier。")
    d.bullet("预算 tier 基于 proxy，不是严格的实际 VO 字节上限；硬上限需要保守完整 VO 标定。")
    d.paragraph("8.3 推荐论文术语", style="Heading2")
    d.paragraph(
        "建议把查询参数方法称为 authenticated calibrated probe policy，而不是 Recall prediction model。"
        "前者准确表达了贡献：极小的认证映射、固定逐层 top-p、可验证执行和经验 Recall trade-off。",
        bold=True, color="1F4E79", border="2E75B6",
    )
    d.paragraph("8.4 推荐下一步实验", style="Heading2")
    d.table(
        ["优先级", "实验", "目的"],
        [
            ["P0", "实现独立 verifier + 恶意 selected IDs/blocks/tau 注入", "验证 protocol soundness，而不仅是字节核算"],
            ["P0", "GIST/Deep/Glove、多随机种子", "验证外部适用性和稳定性"],
            ["P1", "calibration queries 数量消融", "量化 Recall 统计误差与策略搜索成本"],
            ["P1", "候选 score 缓存和 coordinate search", "降低 SIFT100K 的 73 s policy 搜索"],
            ["P2", "任意深度递归候选 + VO 改善早停", "把有限结构网格推进为更一般的建树算法"],
        ],
        widths=[1000, 4100, 3900],
        font_size=8.3,
    )
    d.paragraph("9. 实现与复现文件", style="Heading1")
    d.table(
        ["文件", "作用"],
        [
            ["auto_vo_tree.cpp", "1–4 层 residual 结构、probe guard 和预算约束搜索"],
            ["compact_probe_allocator.cpp", "五档 policy、stagewise 查询、完整 VO 评测和连续模型消融"],
            ["budgeted_residual_vo.cpp", "通用 residual tree builder、codec 和 trace 基础结构"],
            ["complete_routing_vo.cpp", "routing codec、leaf protocol 和显式 seed 支持"],
            ["ADAPTIVE_RESIDUAL_VO_EXPERIMENT_20260901.md", "自适应结构选择实验"],
            ["AUTHENTICATED_EFVO_TIER_EXPERIMENT_20260901.md", "五档参数正式实验"],
            ["compact_allocator_table_*.csv", "四组正式 per-tier 原始结果"],
        ],
        widths=[3900, 5100],
        font_size=8.2,
    )
    d.paragraph("最终摘要", style="Heading2")
    d.paragraph(
        "当前协议的关键优点是分工清楚：AutoVO 离线决定树；efVO tier 决定每层访问规模；"
        "查询产生 trace；VO generator 根据 trace 收集证明；客户端重算固定 top-p 和叶内 top-k。"
        "它避开了 best-first 队列验证的复杂性，同时保留一个类似 IVF nprobe 的用户调节旋钮。",
        bold=True, color="17365D",
    )


def footer_xml_current():
    return '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:ftr xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"
 xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<w:p><w:pPr><w:jc w:val="center"/></w:pPr>
<w:r><w:rPr><w:color w:val="7F8C8D"/><w:sz w:val="18"/></w:rPr>
<w:t>Current VeriANN Protocol  ·  </w:t></w:r>
<w:r><w:fldChar w:fldCharType="begin"/></w:r>
<w:r><w:instrText xml:space="preserve"> PAGE </w:instrText></w:r>
<w:r><w:fldChar w:fldCharType="end"/></w:r>
</w:p></w:ftr>'''


def core_xml_current():
    now = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    return f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
 xmlns:dc="http://purl.org/dc/elements/1.1/"
 xmlns:dcterms="http://purl.org/dc/terms/"
 xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
<dc:title>当前可验证 ANN：索引构建、查询与验证协议</dc:title>
<dc:subject>AutoVO residual hierarchy and authenticated efVO probe policy</dc:subject>
<dc:creator>Codex / ANN_project</dc:creator>
<cp:lastModifiedBy>Codex / ANN_project</cp:lastModifiedBy>
<dcterms:created xsi:type="dcterms:W3CDTF">{now}</dcterms:created>
<dcterms:modified xsi:type="dcterms:W3CDTF">{now}</dcterms:modified>
</cp:coreProperties>'''


def build_docx(figures):
    d = DocBuilder()
    add_cover(d)
    add_overview(d, figures)
    add_index_construction(d, figures)
    add_query_protocol(d, figures)
    add_verification_protocol(d, figures)
    add_examples(d)
    add_results(d, figures)
    add_limits(d)

    with zipfile.ZipFile(OUT, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types_xml())
        z.writestr("_rels/.rels", root_rels_xml())
        z.writestr("word/document.xml", d.build_document_xml())
        z.writestr("word/styles.xml", styles_xml())
        z.writestr("word/settings.xml", settings_xml())
        z.writestr("word/footer1.xml", footer_xml_current())
        z.writestr("word/_rels/document.xml.rels", rels_xml(d.doc_rels))
        z.writestr("docProps/core.xml", core_xml_current())
        z.writestr("docProps/app.xml", app_xml())
        for name, image_path in d.images:
            z.write(image_path, f"word/media/{name}")


def main():
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    figures = {
        "system": ASSET_DIR / "01_system_pipeline.png",
        "tree": ASSET_DIR / "02_residual_tree.png",
        "query": ASSET_DIR / "03_stagewise_query_example.png",
        "verify": ASSET_DIR / "04_verification_flow.png",
        "tradeoff": ASSET_DIR / "05_tradeoff_curves.png",
    }
    fig_system_pipeline(figures["system"])
    fig_residual_tree(figures["tree"])
    fig_query_example(figures["query"])
    fig_verification_flow(figures["verify"])
    fig_tradeoff(figures["tradeoff"])
    build_docx(figures)
    print(f"generated {OUT}")
    print(f"size {OUT.stat().st_size} bytes")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Generate a concise Chinese Word document for the two-stage VeriANN design."""

from __future__ import annotations

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
ASSET_DIR = BASE / "figures_two_stage_solution"
OUT = BASE / "Two_Stage_Verifiable_ANN_Solution_CN.docx"
NS_W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
NS_R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"


def fig_two_bottlenecks(path: Path):
    fig, ax = setup_ax((14, 6.5))
    ax.text(0.5, 0.94, "Two Orthogonal VO Bottlenecks", ha="center",
            fontsize=19, weight="bold", color="#17365D")
    boxes = [
        (0.03, "Authenticated IVF", "many ambiguous leaves\n+high-precision payload", "#FDEDEC", "#B03A2E"),
        (0.27, "Stage I", "progressive quantization\nreduce bytes / object", "#EBF5FB", "#2874A6"),
        (0.51, "Stage II", "residual hierarchy\nreduce visited objects", "#E8F8F5", "#148F77"),
        (0.75, "Complete VO", "routing + proof + leaves\noptimized jointly", "#FEF9E7", "#B7950B"),
    ]
    for i, (x, title, sub, fc, ec) in enumerate(boxes):
        box(ax, (x, 0.48), (0.20, 0.26), f"{title}\n\n{sub}", fc=fc, ec=ec, fontsize=11)
        if i < len(boxes) - 1:
            arrow(ax, (x + 0.202, 0.61), (boxes[i + 1][0] - 0.006, 0.61), lw=1.8)
    box(ax, (0.16, 0.14), (0.68, 0.16),
        "VO_total = VO_routing + VO_authentication + N_visited x Bytes_per_object",
        fc="#F8F9F9", ec="#5D6D7E", fontsize=12)
    ax.text(0.5, 0.055,
            "Stage I attacks payload precision; Stage II attacks candidate volume.",
            ha="center", fontsize=10, color="#566573")
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def fig_index(path: Path):
    fig, ax = setup_ax((14, 7.5))
    ax.text(0.5, 0.95, "Residual Routing + Progressive Leaf Certificates",
            ha="center", fontsize=18, weight="bold", color="#17365D")

    box(ax, (0.41, 0.79), (0.18, 0.10), "Root codebook\nabsolute coarse codes",
        fc="#EBF5FB", ec="#2874A6", fontsize=10)
    mids = [(0.12, "Residual group A"), (0.41, "Residual group B"), (0.70, "Residual group C")]
    for x, label in mids:
        box(ax, (x, 0.58), (0.18, 0.10), label + "\nchild residual codes",
            fc="#E8F8F5", ec="#148F77", fontsize=9.5)
        arrow(ax, (0.50, 0.79), (x + 0.09, 0.685), color="#5D6D7E")

    leaves = [(0.04, "L1"), (0.23, "L2"), (0.405, "L3"), (0.595, "L4"), (0.77, "L5")]
    parents = [0.21, 0.21, 0.50, 0.79, 0.79]
    for (x, label), px in zip(leaves, parents):
        box(ax, (x, 0.38), (0.15, 0.09), f"{label}: posting list\nobject IDs + codes",
            fc="#FCF3CF", ec="#B7950B", fontsize=8.8)
        arrow(ax, (px, 0.58), (x + 0.075, 0.475), color="#7F8C8D", lw=1.2)

    box(ax, (0.09, 0.10), (0.24, 0.15), "Base summary\nsmall, always returned",
        fc="#EBF5FB", ec="#2874A6", fontsize=10)
    box(ax, (0.38, 0.10), (0.24, 0.15), "Refinement residual\nreturned only if ambiguous",
        fc="#F4ECF7", ec="#7D3C98", fontsize=10)
    box(ax, (0.67, 0.10), (0.24, 0.15), "Exact/lossless tail\nlast resort",
        fc="#FDEDEC", ec="#B03A2E", fontsize=10)
    arrow(ax, (0.33, 0.175), (0.38, 0.175), color="#7D3C98")
    arrow(ax, (0.62, 0.175), (0.67, 0.175), color="#B03A2E")
    ax.text(0.5, 0.03,
            "Merkle commitments bind codebooks, residual blocks, posting lists and refinement chunks.",
            ha="center", fontsize=10, color="#566573")
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def fig_parameter_finding(path: Path):
    fig, ax = setup_ax((14, 7.0))
    ax.text(0.5, 0.95, "Recall-Constrained Structure and Probe Selection",
            ha="center", fontsize=18, weight="bold", color="#17365D")
    items = [
        (0.03, "Candidate hierarchy", "log-spaced cluster\nresolutions"),
        (0.24, "Find probes", "bisection to minimum\nRecall-feasible probes"),
        (0.45, "Estimate cost", "complete routing VO\n+ leaf VO proxy"),
        (0.66, "Prune / refine", "current: beam\nproposed: VO bounds"),
        (0.84, "Commit", "best feasible tree\nand tier table"),
    ]
    colors = ["#EBF5FB", "#E8F8F5", "#FEF9E7", "#F4ECF7", "#D5F5E3"]
    edges = ["#2874A6", "#148F77", "#B7950B", "#7D3C98", "#1E8449"]
    widths = [0.16, 0.16, 0.16, 0.16, 0.13]
    for i, ((x, title, sub), w) in enumerate(zip(items, widths)):
        box(ax, (x, 0.53), (w, 0.23), f"{title}\n\n{sub}", fc=colors[i], ec=edges[i], fontsize=9.5)
        if i < len(items) - 1:
            arrow(ax, (x + w + 0.002, 0.645), (items[i + 1][0] - 0.005, 0.645))

    box(ax, (0.12, 0.16), (0.34, 0.18),
        "Feasibility\nRecall >= target\nlatency/build <= budget",
        fc="#F8F9F9", ec="#5D6D7E", fontsize=11)
    box(ax, (0.54, 0.16), (0.34, 0.18),
        "Proposed bound rule\nprune if maximum VO saving\n<= minimum authentication cost",
        fc="#FDEDEC", ec="#B03A2E", fontsize=10.5)
    ax.text(0.71, 0.09, "Design complete; implementation/measurement pending",
            ha="center", fontsize=9.5, color="#B03A2E", weight="bold")
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def footer_xml():
    return f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:ftr xmlns:w="{NS_W}" xmlns:r="{NS_R}"><w:p><w:pPr><w:jc w:val="center"/></w:pPr>
<w:r><w:rPr><w:color w:val="7F8C8D"/><w:sz w:val="18"/></w:rPr><w:t>Two-Stage Verifiable ANN  ·  </w:t></w:r>
<w:r><w:fldChar w:fldCharType="begin"/></w:r><w:r><w:instrText xml:space="preserve"> PAGE </w:instrText></w:r><w:r><w:fldChar w:fldCharType="end"/></w:r>
</w:p></w:ftr>'''


def core_xml():
    now = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    return f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
<dc:title>两阶段低 VO 可验证 ANN 方案</dc:title><dc:subject>Progressive quantization, residual hierarchy and VO-aware parameter selection</dc:subject>
<dc:creator>Codex / ANN_project</dc:creator><cp:lastModifiedBy>Codex / ANN_project</cp:lastModifiedBy>
<dcterms:created xsi:type="dcterms:W3CDTF">{now}</dcterms:created><dcterms:modified xsi:type="dcterms:W3CDTF">{now}</dcterms:modified>
</cp:coreProperties>'''


def add_cover(d: DocBuilder):
    d.paragraph("两阶段低 VO 可验证 ANN 方案", style="Title")
    d.paragraph("多级渐进量化 + Merkle 认证 + Residual Hierarchy + 参数自动选择", style="Subtitle")
    d.paragraph("方案定位", style="Heading2")
    d.paragraph(
        "用户的概括基本正确。更精确地说：第一阶段降低每个被证明对象的字节数；第二阶段降低必须访问并证明的对象数量。两者共同最小化完整 VO，而不是两个彼此独立的技巧。",
        border="2E75B6", after=150,
    )
    d.table(
        ["模块", "解决的问题", "当前状态"],
        [
            ["多级渐进量化 + Merkle", "单个候选对象/中心返回过多高精度数据", "已实现并实验"],
            ["Residual 多层路由", "被访问的 fine lists 与叶对象过多", "已实现并实验"],
            ["结构与 probes 自动搜索", "层数、fanout、每层 probe 缺少统一选择", "已实现并实验"],
            ["VO 上下界剪枝", "减少结构搜索成本并增强算法表达", "已设计；尚未实现验证"],
        ],
        widths=[2100, 4300, 2300], font_size=9,
    )
    d.paragraph("术语说明", style="Heading2")
    d.bullet("“workload-aware”在当前原型中是 query-distribution-aware：使用独立 calibration queries 表征查询分布，不依赖线上访问频率日志。")
    d.bullet("系统认证的是“承诺索引上的既定 ANN 查询过程及其返回结果”，不是未经访问区域上的全局 exact-kNN。")
    d.bullet("Recall 约束由独立校准集统计保证；密码学验证保证服务器没有偏离已承诺的索引、参数和执行流程。")
    d.image(ASSET_DIR / "01_two_bottlenecks.png", width_inches=6.65,
            caption="图 1  两个正交的 VO 瓶颈与统一目标")


def add_stage_one(d: DocBuilder):
    d.paragraph("1. 第一阶段：降低单对象证明字节", style="Heading1")
    d.paragraph("问题", style="Heading2")
    d.paragraph(
        "在可验证 IVF 中，客户端必须确认未进入结果的对象确实更远。若低精度摘要无法安全区分，服务器就要返回更高精度码，极端情况下返回原始/无损向量。因此高 VO 的首要来源不是普通 ANN 的距离计算，而是认证消歧所需的数据精度。"
    )
    d.paragraph("索引设计", style="Heading2")
    d.bullet("为中心、residual 和叶对象构造嵌套的渐进量化表示：base summary → refinement residual → exact/lossless tail。")
    d.bullet("低比特摘要先给出可验证距离区间；只有区间跨越当前第 k 距离阈值时才返回下一段 refinement。")
    d.bullet("Merkle Tree 对 codebook、对象 ID、量化块、refinement chunk 和叶块布局作统一承诺，防止服务器替换或遗漏数据。")
    d.bullet("各层精度与块布局由 calibration queries 估计其触发概率，并以期望完整 VO 为目标选择。")
    d.paragraph("验证直觉", style="Heading2")
    d.code("若 certified_lower_bound(q, x) > tau_k，则 x 不可能进入当前 top-k，无需返回更高精度。")
    d.paragraph(
        "这一阶段显著降低 Bytes_per_object，但不会根本改变 N_visited；如果查询仍进入许多叶簇，累计叶 VO 仍可能很大。"
    )


def add_stage_two(d: DocBuilder):
    d.paragraph("2. 第二阶段：降低被访问对象数量", style="Heading1")
    d.paragraph("Residual 多层树", style="Heading2")
    d.paragraph(
        "在 coarse 与最终 fine posting lists 之间加入一层或多层 residual codebook。子中心不再重复编码绝对位置，而是编码其相对父中心的残差；这通常以更少字节提供更细的局部路由。树保持规则、分层和平衡，便于批量认证和重放。"
    )
    d.bullet("第 0 层：绝对 coarse centers，快速定位大区域。")
    d.bullet("第 l 层：在已选父节点内比较 child residual codes，选择全局 top-p_l 节点。")
    d.bullet("叶层：只扫描被选 fine lists；对候选对象执行渐进量化证明，必要时才返回无损尾部。")
    d.image(ASSET_DIR / "02_index.png", width_inches=6.65,
            caption="图 2  Residual 路由树与叶端渐进认证的组合")
    d.paragraph("为什么不是固定两层", style="Heading2")
    d.paragraph(
        "增加一层会减少下一层候选和叶对象，但也会增加中心码、认证路径与比较开销。算法只在预计节省的下游 VO 大于新增路由/认证成本时保留该层；因此小数据集可能选择两层，复杂或更大数据集可能选择三层。"
    )


def add_parameter_finding(d: DocBuilder):
    d.paragraph("3. 层数、fanout 与 probes 的自动选择", style="Heading1")
    d.paragraph("统一优化问题", style="Heading2")
    d.code("min_T,p  E[VO_complete(T,p)]")
    d.code("s.t. Recall(T,p) >= R_target,  Latency <= B_query,  BuildCost <= B_build")
    d.paragraph(
        "T 决定层数与每层 cluster resolution；p=(p0,...,pL) 决定每层进入下一层的节点数。目标必须计算完整 VO：routing codes、Merkle paths、叶摘要、refinement 与必要的 exact tails。"
    )
    d.paragraph("当前已实现的搜索", style="Heading2")
    d.bullet("在对数尺度上生成候选 cluster resolutions；从浅结构开始，逐层插入有意义的中间分辨率。")
    d.bullet("对每个候选结构，用独立 calibration queries 通过单调二分寻找满足目标 Recall 的最小 probe 向量，而不是手工指定比例。")
    d.bullet("以 exact routing bytes + conservative leaf-VO proxy 排序候选；保留少量优胜结构继续扩展，最终用完整 VO 实测复核。")
    d.bullet("输出少量 tier 表（如 economy / balanced / high-recall），用户只选择一个一维档位；档位、probes 与 Merkle root 一起承诺。")
    d.paragraph("计划中的 VO 上下界剪枝", style="Heading2")
    d.paragraph(
        "对一次“插入中间层/增大 fanout”的候选，估计最大可能节省 U_save 与必需新增认证成本 L_add。若 U_save ≤ L_add，或该分支的乐观下界已不优于当前最优解，则安全停止扩展。核心直觉是“最大可省 VO 都付不起最小新增门成本，就没有继续试的必要”。"
    )
    d.code("optimistic_cost(g) = cost(parent) + L_add(g) - U_save(g)")
    d.paragraph(
        "重要：这一上下界规则目前是算法设计，尚未集成到原型并完成跨数据集测量；因此本文档不把它写成已有实验结论。",
        border="C00000",
    )
    d.image(ASSET_DIR / "03_parameter_finding.png", width_inches=6.65,
            caption="图 3  Recall 约束下的结构与 probe 自动选择；红色部分为待验证改进")


def add_query_and_verification(d: DocBuilder):
    d.paragraph("4. 查询与验证协议", style="Heading1")
    d.paragraph("服务器查询", style="Heading2")
    d.bullet("输入 q、k 和 tier；从已承诺 tier 表读取每层 probes p0,...,pL。")
    d.bullet("逐层计算距离：先选全局 top-p0 coarse，再在其子节点中选全局 top-p1，直至 fine lists。")
    d.bullet("扫描被选 posting lists，得到 ANN top-k；查询轨迹记录各层候选、分数、选择边界和叶访问集合。")
    d.bullet("查询结束后根据轨迹生成 VO。查询逻辑与 VO 打包可分开实现，但 VO 必须完整覆盖查询决策。")
    d.paragraph("客户端验证", style="Heading2")
    d.bullet("验证索引版本、Merkle root、tier 与 probe 向量均属于已承诺配置。")
    d.bullet("认证每层被访问的父块及其完整 child block，重算量化距离与稳定 tie-breaking，确认 top-p 选择正确。")
    d.bullet("认证被选 posting lists；用渐进距离区间排除非结果对象，对返回 top-k 重算最终距离和排序。")
    d.bullet("因此客户端不需要 best-first priority queue；验证仍是规则的逐层 top-p 重放，量化区间逻辑保持简单。")
    d.paragraph("保证边界", style="Heading2")
    d.table(
        ["性质", "如何保证"],
        [
            ["完整性/防篡改", "Merkle commitment 与域分离哈希"],
            ["查询执行正确", "客户端重放每层 top-p、tie-break 与叶扫描"],
            ["访问叶内 top-k 正确", "渐进区间证明 + 必要的无损尾部"],
            ["Recall 达标", "独立 calibration/test 的统计约束；不是密码学逐查询保证"],
            ["全局 exact-kNN", "不承诺；除非遍历全部未访问区域或加入更强的几何排除证明"],
        ],
        widths=[2500, 6200], font_size=9,
    )


def add_example_and_results(d: DocBuilder):
    d.paragraph("5. 简化例子与当前结果", style="Heading1")
    d.paragraph("构建与查询例子", style="Heading2")
    d.paragraph(
        "以 50K 向量为例，候选结构包括 512→32768、32→512→32768 等。校准发现增加 32-cluster 根层虽然多付一小段 routing proof，却能显著减少需要比较的 512-level 节点；在 Recall≥0.99 约束下，自动选出 32→512→32768，并找到 probes 15→123→983。查询时只按这三个全局 top-p 门依次推进，最终扫描 983 个 fine lists，而不是返回全部细粒度中心和大量叶对象。"
    )
    d.paragraph("跨数据集实验快照", style="Heading2")
    d.table(
        ["数据集", "自动结构", "probes", "Recall@100", "Routing KiB", "Leaf KiB", "总 VO KiB", "Query ms", "Build s"],
        [
            ["SIFT20K", "256→16384", "74→360", "0.99076", "202.79", "53.64", "256.42", "0.777", "1.09"],
            ["SIFT50K", "32→512→32768", "15→123→983", "0.99198", "238.43", "161.70", "400.13", "1.256", "2.05"],
            ["SIFT100K", "256→65536", "54→863", "0.99002", "445.07", "152.45", "597.51", "1.557", "5.50"],
            ["HierGauss50K", "128→2048→16384", "10→23→100", "0.99250", "44.31", "15.92", "60.23", "0.190", "0.80"],
        ],
        widths=[1150, 1650, 1450, 1050, 1050, 900, 1050, 900, 750], font_size=7.6,
    )
    d.paragraph(
        "上述自动结构均达到 Recall@100≥0.99，记录中的约束违例数为 0。结果表明方法在规则 SIFT 与层次高斯数据上均可工作，但这仍是原型规模实验；论文结论还需要扩大到更大 N、更多维度/分布，并与手工最优、Flat/PQ-IVF、两层 residual 等基线统一比较。"
    )
    d.paragraph("如何读 VO", style="Heading2")
    d.bullet("Routing VO 随访问中心/节点增加；层数过深时它会反噬收益。")
    d.bullet("Leaf VO 反映被扫描对象数量与渐进量化消歧开销；第一阶段主要压低该项的单位成本。")
    d.bullet("因此选择结构时必须最小化二者之和，不能只追求少扫描叶或只追求更小量化码。")


def add_summary(d: DocBuilder):
    d.paragraph("6. 论文方案的简洁表述", style="Heading1")
    d.paragraph("核心命题", style="Heading2")
    d.paragraph(
        "我们将可验证 ANN 的高 VO 分解为“候选数量”和“单候选认证精度”两个乘性瓶颈，并提出统一的 proof-aware residual index：用查询分布感知的渐进量化降低单对象字节，用多层 residual routing 降低被访问对象数，再在 Recall、延迟与构建预算约束下联合选择层数、fanout 和 probes。",
        border="2E75B6",
    )
    d.paragraph("建议的三项贡献", style="Heading2")
    d.bullet("Proof-aware progressive quantization：只为真正模糊的对象逐级补充精度，并由 Merkle commitment 绑定。")
    d.bullet("Residual authenticated routing：用低字节局部残差门逐层过滤，减少进入叶层的对象数量。")
    d.bullet("Recall-constrained VO optimization：自动选择结构与 probes；下一步以可证明的 VO 上下界取代经验 beam，降低构建搜索成本。")
    d.paragraph("当前最准确的结论", style="Heading2")
    d.paragraph(
        "现有实验支持“前两项与自动搜索能够工作，并获得低 VO、低毫秒级查询延迟和可接受构建成本”。但不能宣称 VO 上下界剪枝已经降低构建成本，因为该部分尚待实现与消融。论文下一步的关键实验正是：跨规模验证 bounds 的剪枝率、搜索加速比、与穷举/beam 的最优性差距，以及最终 VO/Recall 是否保持。"
    )


def build_docx():
    d = DocBuilder()
    add_cover(d)
    add_stage_one(d)
    add_stage_two(d)
    add_parameter_finding(d)
    add_query_and_verification(d)
    add_example_and_results(d)
    add_summary(d)

    with zipfile.ZipFile(OUT, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types_xml())
        z.writestr("_rels/.rels", root_rels_xml())
        z.writestr("word/document.xml", d.build_document_xml())
        z.writestr("word/styles.xml", styles_xml())
        z.writestr("word/settings.xml", settings_xml())
        z.writestr("word/footer1.xml", footer_xml())
        z.writestr("word/_rels/document.xml.rels", rels_xml(d.doc_rels))
        z.writestr("docProps/core.xml", core_xml())
        z.writestr("docProps/app.xml", app_xml())
        for name, image_path in d.images:
            z.write(image_path, f"word/media/{name}")


def main():
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    fig_two_bottlenecks(ASSET_DIR / "01_two_bottlenecks.png")
    fig_index(ASSET_DIR / "02_index.png")
    fig_parameter_finding(ASSET_DIR / "03_parameter_finding.png")
    build_docx()
    print(f"generated {OUT}")
    print(f"size {OUT.stat().st_size} bytes")


if __name__ == "__main__":
    main()
